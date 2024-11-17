# BirdNetPi on a Cheap Yellow Display (CYD)

BirdNet-Pi is a program to identify birds based on their calls, running on a Raspberry Pi. It is able to recognize bird sounds from a USB microphone or sound card in realtime. It runs a local web server that shows what birds have been heard. The web site has many features, but if you just want to know what birds are around, pulling out your phone for a quick check is not ideal.

This code runs on a Cheap Yellow Display to show recent information from a BirdNet installation. While it is designed for a CYD, the code could be adapted to run on any ESP32 with a different display.

This project requires that you have a BirdNet installation. I use [Nachtzuster's version](https://github.com/Nachtzuster/BirdNET-Pi). BirdNET-Pi is built on the [BirdNET](https://github.com/kahst/BirdNET-Analyzer) framework by [@kahst](https://github.com/kahst).

This display uses the notification system built in to BirdNet-Pi to receive mqtt messages and display them. 

![Sample Screen](/assets/sample%20display%20screen.jpg)

## Requirements

You'll need an installation BirdNet-Pi and a Cheap Yellow Display (CYD).

CYD (part ESP32-2432S028R) is an ESP32 with a a built-in 2.8" LCD display (240x320) touchscreen. More information is available [here](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) and from [Random Nerds](https://randomnerdtutorials.com/projects-esp32/). 

## Dependencies

This project requires the following libraries.

**[TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)**
Arduino and PlatformIO IDE compatible TFT library optimized for the Raspberry Pi Pico (RP2040), STM32, ESP8266 and ESP32 that supports different driver chips.

**[XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)**
Touchscreen Arduino Library for XPT2046 Touch Controller Chip. Even though TFT_eSPI provides some support for touchscreens, it doesn't work on the CYD, so you need this for touchscreen functions. **Note:** If you are using PlatformIO, you must configure the library in the platformio.ini by pointing to the official [repo](https://github.com/PaulStoffregen/XPT2046_Touchscreen.git) or manually downloading the lib. The library included by PlatformIO is out of date.

**[LVGL](https://github.com/lvgl/lvgl)**
Embedded graphics library to create UIs for any MCU, MPU and display type.

**[WifiManager](https://github.com/tzapu/WiFiManager)**
ESP8266/ESP32 WiFi Connection manager with web captive portal.

**[WifiManagerTz](https://github.com/tobozo/WiFiManagerTz)**
An NTP/Timezone extension for WiFiManager.

**[PicoMQTT](https://github.com/mlesniew/PicoMQTT)**
ESP MQTT client and broker library.

## Visual Studio Code/Platformio vs Arduino IDE
The source is configured to use platformio.ini to configure settings for TFT_eSPI.  To configure these settings under Arduino IDE, copy the [User_Setup.h](https://raw.githubusercontent.com/RuiSantosdotme/ESP32-TFT-Touchscreen/main/configs/User_Setup.h) from Random Nerds to your TFT_eSPI folder under your Arduino\libraries. Details are at [Random Nerds CYD Tutorial](https://randomnerdtutorials.com/cheap-yellow-display-esp32-2432s028r/). For my CYD, I had to add ` #define TFT_INVERSION_ON`.

## Getting Started

1. Install and configure BirdNet-Pi. BirdNet-Pi is not a preconfigured images, you need to install it onto a running (clean) Raspberry Pi. See detail [here](https://github.com/Nachtzuster/BirdNET-Pi).
2. Install this code onto a CYD. The platformio.ini should help get this working under VSC and PlatformIO. You can use Adruino IDE with the caveats listed above.
3. Boot the CYD. It should start in WiFiManager config mode, acting as an access point.

  ![Wifi Config Display](/assets/Wifi%20Config%20Display.jpg) 

4. Configure the network settings on the CYD.
    1. Connect to the CYD's wifi network (default network name: BirdNetDisplay, no password). 
    2. Point your browser at the display's IP address. It should be 192.168.4.1.

    ![Wifi Manager Screen](/assets/wifi_manager_main_screen.jpg)

    3. Under 'Setup', configure the display's hostname. Default: BirdNetDisplay  This will be used to configure BirdNet-Pi to send messages to the display. Each display on the network should have a unique name. After you save your changes, use the browser back button to return to the WifiManager config screen.

    ![Hostname Config Screen](/assets/hostname_parameter_screen.jpg)

    4. Under 'Setup Clock', configure the timezone and time sync settings. After you submit your changes, use the browser back button to return to the WifiManager config screen.

    ![Timezone Config Screen](/assets/time_settings_screen.jpg)
    
    5. Under Configure Wifi, set WifiManager to connect to your WiFi setup. After you save changes the display will exit config mode. **Do this step last. When you complete this step, the display will exit config mode and you will not be able to set the hostname, clock, or timezone until you restart config mode.**

    ![WiFi Config Screen](/assets/wifi_parameters_screen.jpg)

    6. When the device is configured and ready, it should show this screen.

    ![No birds yet](/assets/no%20birds%20screen.jpg)

4. Configure BirdNet to send messages to the display.
    1. On the main screen, click on "Tools" on the right side of the menu. The default username is "birdnet", with no password. You may have changed this when you configured BirdNet-Pi.
    2. Click on "Settings" on the left side of the screen.
    3. In the "Notifications" box, enter `mqtt://birdnetdisplay.local/birdnetpi`.  The "birdnetdisplay" must match the hostname value you entered at step 4 above (under network setup) plus ".local". The "birdnetpi" can be anything (no spaces please). This is the the MQTT topic. The display code shows all messages and does not filter on this value.  Add one line for each CYD.
    4. In the "Notification Body" section, enter `CN=$comname;SN=$sciname;TM=$time;CF=$confidencepct;RS=$reason`. This encodes the information for parsing by the display. If you send a text notification with this encoding before there are any birds recorded, BirdNet sends an incomplete message and the CYD will not update.
    5. Check the boxes for the kinds of notices you want to display.  Do not send the weekly report; the display will not process it correctly.

    ![BirdNetPi Notificaiton Screen](/assets/birdnetpi_notification_screen.png)
    
    6. Click "Send Test Notification". The text in the Notification Body should appear on the screen. Do NOT skip this step. 
    7. Click "Update settings".


## Using the Display
Birdnet-Pi will send notifications to the display based on the configuration of the Notifications section of the Settings screen. Each notification shows:
* Common Name
* How long ago the display received the notification.
* The confidence percentage, shown in the lower right corner.
* The reason for the notification if it is anything other than a simple detection. This could a "Test Message", first detection of that species for the day, etc.

Scientific Name is included in the notice, but is not displayed.

Notifications rotate through the 10 most recent species every 4 seconds. If a notice is received for a bird in rotation, the notification time is updated for that bird. Each notice expires after 24 hours.

Touching the screen brings up two buttons. One button toggles night mode on/off. The second button opens a screen that shows the current network configuration information, with the option to enter wifi configuration mode. 

The display will exit wifi configuration mode after 2 minutes.

The built-in light sensor controls teh backlight level. 

3d printable cases are available [here](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/3dModels).  I used [this](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main/3dModels/Markus_CYD_Simple_Case).



## Acknowledgements

It's an Open Source world, where we benefit from the hard work of so many others. Without those libraries, this would never have happened. Special thanks to everyone who makes BirdNet-Pi work and the people at Random Nerds for getting me stared with a CYD. Shout out to the AI who did some of the grunt work.




