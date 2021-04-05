# Arduino Software

For this project, I used Arduino IDE v1.8.14 (hourly build 2021/03/09 09:25) to get the lastest Wifi NINA firmare (1.4.3) available at the time for my board (Nano 33 IOT). The reason is that those board doesn't have, out of the box, a very stable Wifi connection. So, you really need to get the latest possible firmware for this to work.

## Setup steps

1. Update Wifi firmware of the board
2. Update the secrets.h file
3. Put code in DEBUG (**#define DEBUG**)
4. Adjust timezone and feeding schedule
5. Comment the **feedNow()** function ( ==> // feedNow() ) in the loop section
6. Build and upload program to the board
7. Using the **scanInput()** function...
   1. Adjust the servo motors angles to match the program configuration
   2. Adjust the **CALIBRATION_FACTOR** using known weight
   3. Use manual feed with real food to final check the system
8. Comment the **#define DEBUG** (==> // #define DEBUG ) and uncomment the feedNow() function
9. Build and upload program to the board
10. Connect the petfeeder and watch your pet be amazed!

## Debugging tips

The comments in the code are self explanatory but it worth mentionning that the flag **#define DEBUG** control how the program will operate. When this definition is in place, all **DEBUG_PRINT** statements are writing info to the serial monitor and the program will start ONLY if the serial monitor is active.

Obviously, this statement should be commented when the feeder operate in "normal" mode.

Also worth noting is that, in DEBUG mode, if your are testing de **grabFood()** function with actual food in the feeder, be sure to power the board with an external source because the standard USB port will not be able to provide the 2 ampere (A) that the Miuzei servo can pull under load. Otherwise, you will experiment multi reset of the board and/or erratic servo movements.
