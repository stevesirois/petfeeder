// -------------------------------------------------------------------------------------------
// Petfeeder V4.1
//
// Author : Steve Sirois
// Release: GA
// Date   : April 2021
//
// -------------------------------------------------------------------------------------------
#include "HX711.h"
#include <Servo.h>
#include <ArduinoBearSSL.h>
#include <WiFiNINA.h>
#include <Arduino_JSON.h>
#include <TimeLib.h>

#include "secrets.h" // Enter your sensitive data in secrets.h

//#define DEBUG // Comment DEBUG for production
#include "DebugUtils.h"
#define LOAD_CELL // Load cell connected, for DEBUG purpose
#define SERVOS    // Servos connected, for DEBUG purpose

#define DOUT    2 // HX711 Data pin
#define CLK     3 // HX711 Clock pin
#define SERVLC  4 // Load cell servo
#define SERVFG  5 // Food grabber servo
#define LED    10 // Switch led of the 5500
#define SWT     9 // Manual feed switch

#define CALIBRATION_FACTOR 10900.0 // Must be tune! Use DEBUG mode with w scanInput

#define TOP   168 // Load cell servo angle when weigh food
#define DOWN  75  // Load cell servo angle when dropping food

#define START 40  // Food grabber servo starting angle (don't go to far back!)
#define STOP  180 // Food grabber servo ending angle to drop food to weight station below

// Global objects
HX711         scale;
Servo         servo;
WiFiClient    wifiClient;            // Used for the TCP socket connection
BearSSLClient sslClient(wifiClient); // Used for SSL/TLS connection, integrates with ECC508

const char ssid[] = SECRET_SSID;
const char pass[] = SECRET_PASS;

const int timeZone = -4;  // Eastern Daylight Time (USA)

volatile boolean problemFlag;       // set possible jam or low food
const int maxNumberNoFoodDrop = 5;  // set the maximum of attempt to delivere food without success

// Enter your feeding schedule here.
// Format: { hour,minute,weight }.
// Hour is in 24 hours format and weight is in gram.
const int schedule[][3] = {
  { 6,30,5 },
  { 7,15,6 },
  { 8,00,6 },
  { 16,00,6 },
  { 16,45,6 },
  { 17,30,6 },
  { 20,00,6 },
  { 20,45,6 },
  { 21,30,6 },
  { 22,00,1 }
};

// -------------------------------------------------------------------------------------------
// -----------------------------------------SETUP---------------------------------------------
// -------------------------------------------------------------------------------------------
void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial);
  #endif
  DEBUG_PRINT("Started...");

  // Setup LED pin
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  #ifdef LOAD_CELL
    // Init load cell
    scale.begin(DOUT, CLK);
    scale.set_scale(CALIBRATION_FACTOR);
    scale.tare();
  #endif

  #ifdef SERVOS
    // Bring all servo to initial position
    resetServo(SERVLC, TOP, 544, 2400);
    resetServo(SERVFG, START, 500, 2500);
  #endif

  // Switch for manual feed
  pinMode(SWT, INPUT);

  // Set a callback to get the current time
  // used to validate the servers certificate
  ArduinoBearSSL.onGetTime(getTime);

  // Connect to Internet to get time, that's it.
  connectWiFi();

  // Setup NTP related stuff
  setTime(getNTPTime());
  setSyncProvider(getNTPTime);
  setSyncInterval(300);

}
// -------------------------------------------------------------------------------------------
// -----------------------------------------LOOP----------------------------------------------
// -------------------------------------------------------------------------------------------
void loop() {

  // Still connected to the external world?
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINT("Wifi lost :-(");
    WiFi.disconnect();
    connectWiFi();
  }

  manualFeed(6.0); // check for manual feed, if so, serve 6 gram

  feedNow(); // Check the schedule

  #ifdef DEBUG
    scanInput(); // This ONLY for debug purpose
  #endif

  if (problemFlag) {
    digitalWrite(LED, HIGH);
    do {
      manualClear();
    } while (true);
  }

  blink(); // If everything fine, just blink that led

}
// -------------------------------------------------------------------------------------------
// -----------------------------------------FUNCIONS------------------------------------------
// -------------------------------------------------------------------------------------------

void scanInput() {
  char rx_byte = 0;

  if (Serial.available() > 0) {    // is a character available?
    rx_byte = Serial.read();       // get the character
    float w;

    switch (rx_byte) {
      case 'd':
        DEBUG_PRINT("Dump...");
        dumpFood();
        break;
      case 'g':
        DEBUG_PRINT("Grab...");
        grabFood();
        break;
      case 'w':
        DEBUG_PRINT("Weigth...");
        w = readScale();
        DEBUG_PRINT(w);
        break;
      case 't':
        DEBUG_PRINT("Tare...");
        scale.tare();
        break;
      default:
        break;
    }
  }
}

void blink()
{
  const uint32_t LED_DELAY = 1000;
  static enum { LED_TOGGLE, WAIT_DELAY } state = LED_TOGGLE;
  static uint32_t timeLastTransition = 0;

  switch (state)
  {
    case LED_TOGGLE:   // toggle the LED
      digitalWrite(LED, !digitalRead(LED));
      timeLastTransition = millis();
      state = WAIT_DELAY;
      break;

    case WAIT_DELAY:   // wait for the delay period
      if (millis() - timeLastTransition >= LED_DELAY)
        state = LED_TOGGLE;
      break;

    default:
      state = LED_TOGGLE;
      break;
  }
}

void feedNow() {
  // Scan the schedule
  const int HOUR = 0;
  const int MIN = 1;
  const int WEIGHT = 2;
  static int lastServing = 0;

  for ( int r = 0; r < (sizeof(schedule) / sizeof(schedule[0])) ; ++r ) {
    if ( schedule[r][HOUR] == hour() && schedule[r][MIN] == minute() && lastServing != hour() * 60 + minute()) {
      DEBUG_PRINT("feed:" + String(schedule[r][WEIGHT]));
      lastServing = hour() * 60 + minute();
      feed((float)schedule[r][WEIGHT]);
    }
  }
}

void deBounce (int state) {
  unsigned long now = millis ();
  do
  {
    // on bounce, reset time-out
    if (digitalRead (SWT) == state)
      now = millis ();
  }
  while (digitalRead (SWT) == state ||
    (millis () - now) <= 20); // 20 mills. debounce timing

}

void resetServo(int serv, int initpos, int minpw, int maxpw) {

  servo.attach(serv, minpw, maxpw);
  servo.write(initpos);
  delay(1000);
  servo.detach();

}

void grabFood() {
  // Please note: the servo has a pulse width signal of 500 to 2500 μsec;
  // when the input signal is 500usec, the angle is 0 degrees, and at 2500 μsec, the angle is 270 degrees.
  // Waterproof performance: IP66

  int pos;

  servo.attach(SERVFG, 500, 2500);

  for (pos = START; pos <= STOP; pos++) {
    servo.write(pos);
    delay(60);
  }

  for (pos = STOP; pos >= START; pos--) {
    servo.write(pos);
    delay(60);
  }

  servo.detach();

}

void dumpFood() {
  int pos;

  servo.attach(SERVLC);

  for (pos = TOP; pos >= DOWN; pos--) {
    servo.write(pos);
    delay(60);
  }

  delay(2000);

  for (pos = DOWN; pos <= TOP; pos++) {
    servo.write(pos);
    delay(60);
  }

  servo.detach();
}

float readScale() {
  return round(scale.get_units() * 1.0);
}

void manualFeed(float fs) {
  if (digitalRead (SWT) == LOW) {
    deBounce(LOW);
    feed(fs);
  }
}

void manualClear() {
  if (digitalRead (SWT) == LOW) {
    deBounce(LOW);
    dumpFood();
    scale.tare();
  }
}
void feed(float targetWeight) {
  int noChangeCnt = 0;
  float weight = 0.0;
  float previousWeight = readScale();

  digitalWrite(LED, HIGH);
  do {
    grabFood();
    weight = readScale();
    DEBUG_PRINT(weight);
    if (previousWeight == weight) {
      noChangeCnt++;
    } else {
      previousWeight = weight;
      noChangeCnt = 0;
    }
  } while (readScale() < targetWeight && noChangeCnt != maxNumberNoFoodDrop);

  dumpFood();
  scale.tare();

  digitalWrite(LED, LOW);
  if (noChangeCnt == maxNumberNoFoodDrop)
    problemFlag = true;
}

unsigned long getTime() {
  // get the current time from the WiFi module
  return WiFi.getTime();
}

time_t getNTPTime() {
  // get the current time from the WiFi module
  time_t t = 0L;

  do {
    // Needed because there's a delay in the query of NTP server in the init phase
    t = getTime();
  } while ( t == 0L);

  DEBUG_PRINT("------------getNTPTime-------------------");

  return t + timeZone * SECS_PER_HOUR;

}

void connectWiFi() {
  DEBUG_PRINT("Connecting to wifi...");
  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    // failed, retry
    DEBUG_PRINT("retrying wifi...");
    delay(1000);
  }
  DEBUG_PRINT("Connected to wifi...");
}
