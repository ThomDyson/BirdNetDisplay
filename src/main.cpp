/*  Rui Santos & Sara Santos - Random Nerd Tutorials
Based in part on code from
https://RandomNerdTutorials.com/esp32-tft-lvgl/
https://RandomNerdTutorials.com/cyd-lvgl/
    Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files. The above copyright notice
and this permission notice shall be included in all copies or substantial
portions of the Software.

Install the "lvgl" library version 9.2 by kisvegabor to interface with the TFT
Display - https://lvgl.io/
Install the "TFT_eSPI" library by Bodmer to interface with the TFT Display -
https://github.com/Bodmer/TFT_eSPI
Install the "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen -
https://github.com/PaulStoffregen/XPT2046_Touchscreen - Note: this library
doesn't require further configuration
Install the WiFimanagerTZ library from
https://github.com/tobozo/WiFiManagerTz/tree/master
*/

/* *************************************************

TO DO

forward declare all functions
set screen rotation a wifi confi param so usb cable can be on either side.
set more params (display duration, max entries, expiration time) as wifi config params

*************************************************
*/

/*
For my display I needed to uncomment
#define TFT_INVERSION_ON
in the TFT_esPI User_setup.h
*/
#include <ESPmDNS.h>
#include <PicoMQTT.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiManagerTz.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>
#include <time.h>

// wifi setup info
char myHostname[40]           = "BirdNetDisplay"; // default value later stored in preferences
char configPortalpassword[40] = "";               // password to access the access point in WiFo config mode
Preferences myPreferences;
WiFiManager wifiMan;
// Add a custom parameter for entering the hostname via the WiFiManager portal
bool wifiConfigModeOn = false;
bool wifiConfirmed    = false;
WiFiManagerParameter hostnameParam( "hostname", "Enter Hostname", myHostname, 40 );

unsigned int startTime         = millis();
unsigned int wifiPortalTimeOut = 120;
// mqtt
#if __has_include( "config.h" )
#include "config.h"
#endif
PicoMQTT::Server mqttServer;

// Touchscreen pins
#define XPT2046_IRQ 36  // T_IRQ
#define XPT2046_MOSI 32 // T_DIN
#define XPT2046_MISO 39 // T_OUT
#define XPT2046_CLK 25  // T_CLK
#define XPT2046_CS 33   // T_CS

SPIClass touchscreenSPI = SPIClass( VSPI );
XPT2046_Touchscreen touchscreen( XPT2046_CS, XPT2046_IRQ );
// display driver setup

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320

constexpr int LIGHT_SENSOR_PIN = 34; // Pin connected to the light sensor
constexpr int BACKLIGHT_PIN =
    21; // Pin connected to the display backlight (PWM capable)

TFT_eSPI tft = TFT_eSPI(); // Initialize your display

// bird data elements
constexpr int MAX_NOTICES                     = 10;
constexpr int NOTICE_ELEMENTS                 = 5;
char notices[MAX_NOTICES][NOTICE_ELEMENTS][2] = {};
constexpr int ENTRY_MAX_HOURS                 = 24; // how long a bird shuold be listed before it expires from display
int birdCount                                 = 0;
int curDisplay                                = 0;
const int MATCH_NOT_FOUND                     = 99;
unsigned long previousLoopTime                = 0;    // Store the last time the refresh was run
const long refreshInterval                    = 4000; // determines how long a bird stays on screen. in Milliseconds

// holds the data for each sighting
struct dataLayout {
  char CN[75] = "";           // comon name
  char SN[75] = "";           // scientific name
  int CF      = 0;            // confidece factor
  time_t TM   = time( NULL ); // time of sighting
  char RS[30] = "";           // reasonfor notice - detection, first time today,etc
};

dataLayout *birds[MAX_NOTICES];

// Declare global screen objects
lv_obj_t *wifiConfirmScreen;
lv_obj_t *Birdscreen;
lv_obj_t *wifiScreen;
lv_obj_t *loadingScreen;
lv_obj_t *CNLabel;
lv_obj_t *CFLabel;
lv_obj_t *TMLabel;
lv_obj_t *RSLabel;
lv_obj_t *BottomLabel;
lv_obj_t *WiFiScreenTitle;
lv_obj_t *WiFiScreenLabel1;
lv_obj_t *WiFiScreenLabel2;
lv_obj_t *WiFiScreenLabel3;
lv_obj_t *WiFiScreenLabel4;
lv_obj_t *label_hour;
lv_obj_t *label_minute;
lv_obj_t *label_second;

lv_obj_t *wifiConfirmLabel;
lv_obj_t *label_wifiNoButton;
lv_obj_t *label_wifiYesButton;
lv_obj_t *wifiYesButton;
lv_obj_t *wifiNoButton;

lv_obj_t *wifiButton; // needs to be global for the call back to work?
lv_obj_t *nightmodeButton;
lv_timer_t *button_cleanup_timer;
lv_color_t lvColorBlue      = lv_color_hex( 0x00008B );
lv_color_t lvColorDark      = lv_color_hex( 0x21130D );
lv_color_t lvColorLight     = lv_color_hex( 0xEEEEE4 );
lv_color_t lvColorGreen     = lv_color_hex( 0x1B5E20 );
lv_color_t lvColorOrange    = lv_color_hex( 0xFFA500 );
lv_color_t lvColorLightGrey = lv_color_hex( 0xD3D3D3 );
lv_style_t timeStyle;
lv_style_t RSStyle;

bool nightmode = false;
char timeDescription[50]; // Buffer for the time description

// Format the time into strings
char hour_str[3];
char minute_str[3];
char second_str[3];
static unsigned long lastUpdate = 0;

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;
int text_height = 0;

#define DRAW_BUF_SIZE ( SCREEN_WIDTH * SCREEN_HEIGHT / 10 * ( LV_COLOR_DEPTH / 8 ) )
uint32_t draw_buf[DRAW_BUF_SIZE / 4];

// Get the Touchscreen data
void touchscreen_read( lv_indev_t *indev, lv_indev_data_t *data ) {
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z)
  if ( touchscreen.tirqTouched() && touchscreen.touched() ) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and
    // height
    x           = map( p.x, 200, 3700, 1, SCREEN_WIDTH );
    y           = map( p.y, 240, 3800, 1, SCREEN_HEIGHT );
    z           = p.z;
    data->state = LV_INDEV_STATE_PRESSED;
    // Set the coordinates
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// function to save the host name into esp32 preferences
void saveHostname() {
  myPreferences.begin( "wifi", false );              // Namespace for your preferences
  myPreferences.putString( "hostname", myHostname ); // Save the hostname
  myPreferences.end();
  WiFi.setHostname( myHostname );
}

// function to read the host name from  esp32 preferences
void loadHostname() {
  myPreferences.begin( "wifi", true ); // Open the namespace in read mode
  if ( myPreferences.isKey( "hostname" ) ) {
    myPreferences.getString( "hostname" )
        .toCharArray( myHostname, sizeof( myHostname ) );
    Serial.print( "Loaded hostname from preferences: " );
  } else {
    Serial.print( "Loaded hostname as DEFAULT VALUE: " );
  }
  myPreferences.end();
  // Use the loaded hostname in your code
  Serial.println( myHostname );
}

// function for callback from WiFiManager when credentials are saved to save any
// params set in WiFi Config to preferences.
void wifiMan_Save_Params_Callback() {
  strncpy( myHostname, hostnameParam.getValue(), sizeof( myHostname ) - 1 );
  Serial.println( "saving hostname from params" );
  Serial.println( hostnameParam.getValue() );
  saveHostname();
  // lv_scr_load( Birdscreen );
}

// function to print WiFi info to the serial output for debugging
void wifiInfo() {
  struct tm timeinfo;
  getLocalTime( &timeinfo );
  // can contain gargbage on esp32 if wifi is not ready yet
  Serial.println( &timeinfo, "%A, %B %d %Y %H:%M:%S" );
  Serial.println( "[WIFI] WIFI_INFO DEBUG" );
  WiFi.printDiag( Serial );
  Serial.println( "[WIFI] MODE: " + ( String ) ( wifiMan.getModeString( WiFi.getMode() ) ) );
  Serial.println( "[WIFI] SAVED: " + ( String ) ( wifiMan.getWiFiIsSaved() ? "YES" : "NO" ) );
  Serial.println( "[WIFI] SSID: " + ( String ) wifiMan.getWiFiSSID() );
  Serial.println( "[WIFI] PASS: " + ( String ) wifiMan.getWiFiPass() );
  // Serial.println("[WIFI] HOSTNAME: " + (String)WiFi.getHostname());
  Serial.println( "TZ " + ( String ) ( WiFiManagerNS::TZ::tzName ) );
}

// function to apply night mode on/off to an LVGL object
void process_object_nightmode( lv_obj_t *obj ) {
  if ( lv_obj_check_type( obj, &lv_label_class ) ) {
    // If it's a label, do something specific
    // if in a button and in night mode OR not in a button and not in lightmode
    if ( ( lv_obj_check_type( lv_obj_get_parent( obj ), &lv_button_class ) && nightmode ) ||
         ( !lv_obj_check_type( lv_obj_get_parent( obj ), &lv_button_class ) && !nightmode ) ) {
      lv_obj_set_style_text_color( obj, lvColorDark, LV_PART_MAIN );
    } else {
      lv_obj_set_style_text_color( obj, lvColorLight, LV_PART_MAIN );
    }
  } else if ( lv_obj_check_type( obj, &lv_button_class ) ) {
    if ( nightmode ) {
      lv_obj_set_style_bg_color( obj, lvColorLight, LV_PART_MAIN );
    } else {
      lv_obj_set_style_bg_color( obj, lvColorBlue, LV_PART_MAIN );
    }

  } else if ( lv_obj_check_type( obj, &lv_textarea_class ) ) {
    // If it's a text area, handle it separately (if needed)
    Serial.println( "Found a text area!" );
    // Add your code here for text areas
  } else {
    // Handle other object types if needed
    const lv_obj_class_t *class_ptr = lv_obj_get_class( obj );
    Serial.printf( "Found another object type: %s\n", *class_ptr );
  }
}

// function to recursively send each child obj for processing in night mode
void process_child_objects( lv_obj_t *Parentobj ) {
  uint32_t child_count =
      lv_obj_get_child_count( Parentobj ); // Get the number of children
  for ( uint32_t i = 0; i < child_count; i++ ) {
    lv_obj_t *child = lv_obj_get_child( Parentobj, i ); // Get the first child
    process_object_nightmode( child );                  // Process the current object
    process_child_objects( child );                     // process any grandchildren
  }
}

// function to step through the objects on a screen
// starts the process of handling nightmode on/off
void process_screen_objects_night( lv_obj_t *thisScreen ) {
  if ( nightmode ) {
    lv_obj_set_style_bg_color( thisScreen, lvColorDark, LV_PART_MAIN );
  } else {
    lv_obj_set_style_bg_color( thisScreen, lvColorLight, LV_PART_MAIN );
  }
  process_child_objects( thisScreen ); // Process all objects on the screen
}

// function to handle callback when nightmode button is pressed
void nightmode_button_touch_callback( lv_event_t *e ) {
  nightmode = !nightmode;
  process_screen_objects_night( Birdscreen );
  process_screen_objects_night( wifiConfirmScreen );
  process_screen_objects_night( wifiScreen );
}

// function to handle callback when wifi button is pressed
void wifiButton_Touch_callback( lv_event_t *e ) {
  lv_scr_load( wifiConfirmScreen );
  wifiInfo();
  char info[200];

  // Format SSID, Hostname, and IP Address into the buffer
  snprintf( info, sizeof( info ), "SSID: %s\nHostname: %s\nIP Address: %s\nTimezone: %s", WiFi.SSID().c_str(), WiFi.getHostname(), WiFi.localIP().toString().c_str(), WiFiManagerNS::TZ::tzName );

  // Update the LVGL label text with the WiFi info
  lv_label_set_text( BottomLabel, info );
}

/* ****************** THIS IS THE BIRD DATA SECTION */
// function to count valid entries in the birds array
int countBirds() {
  int counter = 0;
  for ( int i = 0; i < MAX_NOTICES; ++i ) {
    if ( strlen( birds[i]->CN ) > 5 ) {
      counter++;
    }
  }
  return counter;
}

// function to check if bird exists in the array, based on common name
int isBirdInArray( dataLayout *knownBirds[], int size, const char *newCN ) {
  for ( int i = 0; i < size; ++i ) {
    if ( strcmp( knownBirds[i]->CN, newCN ) == 0 ) {
      Serial.print( knownBirds[i]->CN );
      Serial.print( " is  " );
      Serial.println( newCN );
      return i; // CN exists in the array
    } else {
      Serial.print( knownBirds[i]->CN );
      Serial.print( " is not " );
      Serial.println( newCN );
    }
  }
  return MATCH_NOT_FOUND; // CN does not exist in the array
}

// function to find the oldest entry in the array of birds
// that's the one to update when we get a new entry
int findOldestEntry( dataLayout *birds[], int size ) {
  int oldestIndex   = -1;
  time_t oldestTime = time( nullptr ); // Initialize with current time
  for ( int i = 0; i < size; ++i ) {
    if ( birds[i]->TM < oldestTime ) {
      oldestTime  = birds[i]->TM;
      oldestIndex = i;
    }
  }
  return oldestIndex; // Return index of the oldest entry
}

// Function to get the descriptive time difference
void getTimeAgoDescription( time_t pastTime, char *buffer, size_t bufferSize ) {
  time_t now = time( NULL ); // Get current time
  double seconds =
      difftime( now, pastTime ); // Calculate the difference in seconds

  if ( seconds < 60 ) {
    strncpy( buffer, "just now", bufferSize );
  } else if ( seconds < 3600 ) { // Less than an hour
    int minutes = seconds / 60;
    if ( minutes == 1 ) {
      snprintf( buffer, bufferSize, "%d minute ago", minutes );
    } else {
      snprintf( buffer, bufferSize, "%d minutes ago", minutes );
    }
  } else if ( seconds < 86400 ) { // Less than 24 hours
    int hours = seconds / 3600;
    if ( hours == 1 ) {
      snprintf( buffer, bufferSize, "%d hour ago", hours );
    } else {
      snprintf( buffer, bufferSize, "%d hours ago", hours );
    }
  } else { // More than 24 hours (yesterday)
    strncpy( buffer, "yesterday", bufferSize );
  }
}

// function to display the birds on the screen
void displayBirds( const int *whichbird ) {
  unsigned long currentMillis =
      millis(); // Get the current time in milliseconds
  char countStatus[12] = "";
  char num_str[3]      = "";
  int tempInt          = 0;
  int displayCount     = 0;
  char tempTitle[100]  = "";
  char temptextCF[4];
  bool foundNonBlank = false;
  // Check if refresh interval have passed
  if ( currentMillis - previousLoopTime >= refreshInterval ) {
    previousLoopTime = currentMillis; // Save the current time for the next interval

    int currentIndex = *whichbird;
    int loopcounter  = 0;
    // need to find the next non blank entry
    while ( loopcounter < MAX_NOTICES ) {            // Make only one pass through the array
      if ( strlen( birds[currentIndex]->CN ) > 5 ) { // Check if title is long enough to be valid. very basic error checking
        foundNonBlank = true;
        break; // Return the first valid title found
      }
      currentIndex = ( currentIndex + 1 ) % MAX_NOTICES; // Move to the next index and wrap around if needed
      loopcounter++;
    }

    Serial.print( "." );
    if ( foundNonBlank ) {
      if ( strlen( birds[currentIndex]->CN ) > 5 ) { // if the name is too short, skip the entry as potentially bad data
        displayCount = currentIndex + 1;
        strncat( countStatus, " (", 3 );
        snprintf( num_str, sizeof( num_str ), "%d", displayCount );
        strncat( countStatus, num_str, ( sizeof( countStatus ) - strlen( countStatus ) - 1 ) );
        strncat( countStatus, " of ", 5 );
        tempInt = countBirds();
        snprintf( num_str, sizeof( num_str ), "%d", tempInt );
        strncat( countStatus, num_str, ( sizeof( countStatus ) - strlen( countStatus ) - 1 ) );
        strncat( countStatus, ")", 2 );
        Serial.println( countStatus );

        getTimeAgoDescription( // Get the time description
            birds[currentIndex]->TM, timeDescription,
            sizeof( timeDescription )
        );

        Serial.print( "Time to show " );
        Serial.print( currentIndex + 1 );
        Serial.print( " - CN: " );
        Serial.print( birds[currentIndex]->CN );
        Serial.print( " - SN: " );
        Serial.print( birds[currentIndex]->SN );
        Serial.print( " - RS: " );
        Serial.print( birds[currentIndex]->RS );
        Serial.print( " - TM: " );
        Serial.print( "New time=" );
        Serial.print( timeDescription );
        Serial.print( " - CF: " );
        Serial.println( birds[currentIndex]->CF );

        if ( strlen( birds[currentIndex]->CN ) > 40 ) {
          lv_obj_set_style_text_font( CNLabel, &lv_font_montserrat_14, LV_PART_MAIN | LV_STATE_DEFAULT ); // Set smaller font
        } else {
          lv_obj_set_style_text_font( CNLabel, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT ); // Set default font
        }

        lv_label_set_text( CNLabel, birds[currentIndex]->CN );
        lv_label_set_text( CFLabel, itoa( birds[currentIndex]->CF, temptextCF, 10 ) );

        strncat( tempTitle, timeDescription, 99 );
        strncat( tempTitle, countStatus, ( sizeof( tempTitle ) - strlen( tempTitle ) - 1 ) );
        lv_label_set_text( TMLabel, tempTitle );

        if ( strcasecmp( birds[currentIndex]->RS, "detection" ) == 0 ) {
          lv_label_set_text( RSLabel, "  " );
          /*
          lv_style_set_text_color(
              &RSStyle,
              lv_color_black()
          );
          */
        } else if ( strcasecmp( birds[currentIndex]->RS, "New Species Detection" ) == 0 ) {
          lv_label_set_text( RSLabel, LV_SYMBOL_EYE_OPEN );
        } else if ( strcasecmp( birds[currentIndex]->RS, "First time today" ) == 0 ) {
          lv_label_set_text( RSLabel, LV_SYMBOL_EYE_OPEN );
        } else {
          lv_label_set_text( RSLabel, birds[currentIndex]->RS );
        }
      }
      curDisplay = currentIndex;
    }
    curDisplay++;
    if ( curDisplay >= MAX_NOTICES ) {
      curDisplay = 0;
    }
  }
}

// Function to check and remove birds older than 24 hours
void removeOldEntries( dataLayout *birds[], int arraySize ) {
  time_t now        = time( NULL ); // Get the current time
  bool removedEntry = false;
  for ( int i = 0; i < arraySize; ++i ) {
    if  ( birds[i]->CN[0] != '\0' ) {
      if ( difftime( now, birds[i]->TM ) > ( ENTRY_MAX_HOURS * 3600 ) ) { // If the time difference is more than the configured value
        // Reset the bird entry
        Serial.print( "Removing bird entry at index: " );
        Serial.print( birds[i]->TM );
        Serial.println( i );
        memset( birds[i]->CN, 0, sizeof( birds[i]->CN ) );
        memset( birds[i]->SN, 0, sizeof( birds[i]->SN ) );
        birds[i]->CF = 0;
        birds[i]->TM = 0; // Set time to 0 to indicate an empty entry
        memset( birds[i]->RS, 0, sizeof( birds[i]->RS ) );
      }
  }
  }
  // Check if all entries have been removed
  bool allEmpty = true;
  for ( int i = 0; i < MAX_NOTICES; ++i ) {
    if ( birds[i] != nullptr ) {
      allEmpty = false;
      break;
    }
  }
  if ( allEmpty && removedEntry ) {
    Serial.println( "No more bird entries left!" );
  }
}

// function to validate the mqtt message has the right number of elements to parse
int countEquals( const char *str ) {
  int count = 0;
  while ( *str != '\0' ) {
    if ( *str == '=' ) { // Check if the current character is '='
      count++;
    }
    str++;
  }
  return count;
}

/* ***************** THIS IS THE SCREEN PROCESSING SECTION */

// function to get the stored SSID and display it during startup
void start_loadingScreen() {
  tft.setTextColor( TFT_GREEN, TFT_BLACK );
  tft.setTextSize( 1 );
  WiFi.mode( WIFI_STA ); // force station mode so we can read the stored SSID
  String storedSSID = wifiMan.getWiFiSSID();
  if ( storedSSID.isEmpty() ) {
    Serial.print( "No stored SSID found. Entering wifi config mode" );
    tft.drawString( "No stored wifi info found.", 10, 20, 4 );
    tft.drawString( "Starting wifi config mode.", 10, 50, 4 );
  } else {
    Serial.printf( "Found stored SSID %s", storedSSID );
    tft.drawString( "Connecting to wifi on", 10, 20, 4 );
    tft.drawString( storedSSID, 10, 60, 4 );
  }
}

void create_wifiStartScreen() {
  wifiScreen         = lv_obj_create( NULL );
  char tempText[100] = "";
  WiFiScreenTitle    = lv_label_create( wifiScreen );
  lv_label_set_long_mode( WiFiScreenTitle, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( WiFiScreenTitle, 300 );
  lv_obj_set_style_text_align( WiFiScreenTitle, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_set_style_text_font( WiFiScreenTitle, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_obj_align( WiFiScreenTitle, LV_ALIGN_TOP_MID, 0, 20 );
  lv_label_set_text( WiFiScreenTitle, "WiFi Configuration Mode" );

  WiFiScreenLabel1 = lv_label_create( wifiScreen );
  lv_label_set_long_mode( WiFiScreenLabel1, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( WiFiScreenLabel1, 300 );
  lv_obj_set_style_text_font( WiFiScreenLabel1, &lv_font_montserrat_16, LV_PART_MAIN );
  lv_obj_set_style_text_align( WiFiScreenLabel1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_align( WiFiScreenLabel1, LV_ALIGN_CENTER, 0, -40 );
  lv_label_set_text( WiFiScreenLabel1, "Connect to the display to configure the network. " );

  WiFiScreenLabel2 = lv_label_create( wifiScreen );
  lv_label_set_long_mode( WiFiScreenLabel2, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( WiFiScreenLabel2, 300 );
  lv_obj_set_style_text_font( WiFiScreenLabel2, &lv_font_montserrat_16, LV_PART_MAIN );
  lv_obj_set_style_text_align( WiFiScreenLabel2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_align( WiFiScreenLabel2, LV_ALIGN_CENTER, 0, 10 );
  lv_label_set_text( WiFiScreenLabel2, "" );

  WiFiScreenLabel3 = lv_label_create( wifiScreen );
  lv_label_set_long_mode( WiFiScreenLabel3, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( WiFiScreenLabel3, 300 );
  lv_obj_set_style_text_font( WiFiScreenLabel3, &lv_font_montserrat_16, LV_PART_MAIN );
  lv_obj_set_style_text_align( WiFiScreenLabel3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_align( WiFiScreenLabel3, LV_ALIGN_CENTER, 0, 60 );
  lv_label_set_text( WiFiScreenLabel3, "" );

  WiFiScreenLabel4 = lv_label_create( wifiScreen );
  lv_label_set_long_mode( WiFiScreenLabel4, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( WiFiScreenLabel4, 300 );
  lv_obj_set_style_text_font( WiFiScreenLabel4, &lv_font_montserrat_14, LV_PART_MAIN );
  lv_obj_set_style_text_align( WiFiScreenLabel4, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_align( WiFiScreenLabel4, LV_ALIGN_BOTTOM_MID, 0, 0 );
  lv_label_set_text( WiFiScreenLabel4, "  " );
}

// function to set the flag so doWiFiManager goes into config mode
//  dowifiManager is in the loop
void wifi_yes_button_callback() {
  wifiConfirmed = true;
}

// Function to delete the nightmode and wifi buttons, uses LVGL time callback function
void button_cleanup_timer_callback( lv_timer_t *t ) {
  if ( nightmodeButton ) {
    lv_obj_del_async( nightmodeButton ); // Delete the button
    nightmodeButton = NULL;              // Clear the pointer
  }
  if ( wifiButton ) {
    lv_obj_del_async( wifiButton ); // Delete the button
    wifiButton = NULL;              // Clear the pointer
  }
}

// Event handler for the bird screen touch
void birdscreen_touch_callback( lv_event_t *screenEvent ) {
  if ( lv_event_get_code( screenEvent ) == LV_EVENT_CLICKED ) { // Check for a click event
    if ( !wifiButton ) {

      wifiButton = lv_button_create( Birdscreen ); // Create a button to switch to wifi config confirmation screen
      lv_obj_set_size( wifiButton, 50, 50 );
      lv_obj_align( wifiButton, LV_ALIGN_BOTTOM_LEFT, 5, -10 );
      lv_obj_set_style_shadow_width( wifiButton, 0, LV_PART_MAIN );
      if ( nightmode ) {
        lv_obj_set_style_text_color( wifiButton, lvColorDark, LV_PART_MAIN );
        lv_obj_set_style_bg_color( wifiButton, lvColorLight, LV_PART_MAIN );
      } else {
        lv_obj_set_style_text_color( wifiButton, lvColorLight, LV_PART_MAIN );
        lv_obj_set_style_bg_color( wifiButton, lvColorBlue, LV_PART_MAIN );
      }

      lv_obj_t *label_wifiButton = lv_label_create( wifiButton );
      lv_obj_set_style_text_font( label_wifiButton, &lv_font_montserrat_24, LV_PART_MAIN ); // Change font to a larger size
      lv_label_set_text( label_wifiButton, LV_SYMBOL_WIFI );
      lv_obj_add_event_cb( wifiButton, wifiButton_Touch_callback, LV_EVENT_CLICKED, NULL ); // Add event callback to the button
    }

    if ( !nightmodeButton ) { // Create a button to switch to night mode and back
      nightmodeButton = lv_button_create( Birdscreen );
      lv_obj_set_size( nightmodeButton, 50, 50 );
      lv_obj_align( nightmodeButton, LV_ALIGN_LEFT_MID, 5, 0 );
      lv_obj_set_style_shadow_width( nightmodeButton, 0, LV_PART_MAIN );

      lv_obj_t *label_nightmodeButton = lv_label_create( nightmodeButton );
      lv_obj_set_style_text_font( label_nightmodeButton, &lv_font_montserrat_24, LV_PART_MAIN ); // Change font to a larger size

      lv_label_set_text( label_nightmodeButton, LV_SYMBOL_EYE_OPEN );
      if ( nightmode ) {
        lv_obj_set_style_text_color( nightmodeButton, lvColorDark, LV_PART_MAIN );
        lv_obj_set_style_bg_color( nightmodeButton, lvColorLight, LV_PART_MAIN );
      } else {
        lv_obj_set_style_text_color( nightmodeButton, lvColorLight, LV_PART_MAIN );
        lv_obj_set_style_bg_color( nightmodeButton, lvColorBlue, LV_PART_MAIN );
      }
      lv_obj_add_event_cb( nightmodeButton, nightmode_button_touch_callback, LV_EVENT_CLICKED, NULL ); // Add event callback to the button

      button_cleanup_timer = lv_timer_create( button_cleanup_timer_callback, 10000, NULL ); // Create a timer to delete the button after 10 seconds
      lv_timer_set_repeat_count( button_cleanup_timer, 1 );                                 // one and done on the timer, no repeat
    } else {
      lv_timer_reset( button_cleanup_timer );
    }
  }
}

// Function to create the main bird display screen
void create_Birdscreen() {
  Birdscreen = lv_obj_create( NULL ); // Create a new screen (parent is NULL)

  // Add a label to Birdscreen
  CNLabel = lv_label_create( Birdscreen );
  lv_label_set_long_mode( CNLabel, LV_LABEL_LONG_WRAP );
  lv_obj_set_width( CNLabel, 300 );
  lv_obj_set_style_text_align( CNLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_set_style_text_font( CNLabel, &lv_font_montserrat_28, LV_PART_MAIN );
  lv_obj_align( CNLabel, LV_ALIGN_TOP_MID, 0, 20 );
  lv_label_set_text( CNLabel, "No birds to display yet." );

  lv_style_init( &timeStyle );
  TMLabel = lv_label_create( Birdscreen );
  lv_obj_set_style_text_font( TMLabel, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_obj_add_style( TMLabel, &timeStyle, 0 );
  lv_obj_align( TMLabel, LV_ALIGN_CENTER, 0, -20 );
  // lv_label_set_text(TMLabel, LV_SYMBOL_EYE_OPEN);
  lv_label_set_text( TMLabel, "" );

  RSLabel = lv_label_create( Birdscreen );
  lv_obj_set_style_text_font( TMLabel, &lv_font_montserrat_22, LV_PART_MAIN );
  lv_obj_align( RSLabel, LV_ALIGN_CENTER, 0, 30 );
  lv_label_set_text( RSLabel, "" );

  CFLabel = lv_label_create( Birdscreen );
  lv_obj_set_style_text_font( CFLabel, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_obj_align( CFLabel, LV_ALIGN_BOTTOM_RIGHT, -5, -5 );
  lv_label_set_text( CFLabel, "" );

  lv_obj_add_event_cb( Birdscreen, birdscreen_touch_callback, LV_EVENT_CLICKED, NULL );
}

// Function to create the wifi confirmation screen
void create_wifiConfirmScreen() {
  wifiConfirmScreen = lv_obj_create( NULL );

  wifiConfirmLabel = lv_label_create( wifiConfirmScreen );
  lv_label_set_long_mode( wifiConfirmLabel, LV_LABEL_LONG_WRAP );
  lv_obj_align( wifiConfirmLabel, LV_ALIGN_TOP_MID, 0, 30 );
  lv_obj_set_width( wifiConfirmLabel, 300 );
  lv_obj_set_style_text_align( wifiConfirmLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN );
  lv_obj_set_style_text_font( wifiConfirmLabel, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_label_set_text( wifiConfirmLabel, "Do you want to change the WiFi setup?" );

  wifiYesButton       = lv_button_create( wifiConfirmScreen );
  label_wifiYesButton = lv_label_create( wifiYesButton );
  lv_obj_remove_flag( wifiYesButton, LV_OBJ_FLAG_PRESS_LOCK );
  // lv_obj_align(wifiYesButton, LV_ALIGN_CENTER,
  // lv_obj_get_width(wifiYesButton) - 10, 20);
  lv_obj_align( wifiYesButton, LV_ALIGN_CENTER, -lv_obj_get_width( wifiYesButton ) - 60, 20 );
  lv_obj_set_width(
      wifiYesButton,
      LV_SIZE_CONTENT
  ); // The button width will adjust to the text content
  lv_obj_set_height( wifiYesButton, LV_SIZE_CONTENT );
  lv_obj_set_style_text_font( label_wifiYesButton, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_label_set_text( label_wifiYesButton, "Yes" );
  lv_obj_add_event_cb(
      wifiYesButton,
      []( lv_event_t *e ) {
        wifi_yes_button_callback(); // Load wifi config mode screen when the button is clicked
      },
      LV_EVENT_CLICKED, NULL
  );

  wifiNoButton       = lv_button_create( wifiConfirmScreen );
  label_wifiNoButton = lv_label_create( wifiNoButton );
  lv_obj_remove_flag( wifiNoButton, LV_OBJ_FLAG_PRESS_LOCK );
  lv_obj_align( wifiNoButton, LV_ALIGN_CENTER, lv_obj_get_width( wifiNoButton ) + 50, 20 );
  lv_obj_set_style_text_font( label_wifiNoButton, &lv_font_montserrat_20, LV_PART_MAIN );
  lv_obj_set_width(
      wifiNoButton,
      LV_SIZE_CONTENT
  ); // The button width will adjust to the text content
  lv_obj_set_height( wifiNoButton, LV_SIZE_CONTENT );
  lv_label_set_text( label_wifiNoButton, "No" );

  BottomLabel = lv_label_create( wifiConfirmScreen );
  lv_obj_set_style_text_font( BottomLabel, &lv_font_montserrat_16, LV_PART_MAIN );
  lv_obj_align( BottomLabel, LV_ALIGN_BOTTOM_MID, 0, 0 );
  lv_label_set_long_mode( BottomLabel, LV_LABEL_LONG_WRAP );
  lv_label_set_text( BottomLabel, "" );
  lv_obj_add_event_cb(
      wifiNoButton,
      []( lv_event_t *e ) {
        lv_scr_load( Birdscreen ); // Load bird screen when the button is clicked
      },
      LV_EVENT_CLICKED, NULL
  );
}

void doWifiManager() {
  int remainingTime = millis() - startTime;
  int roundedValue  = static_cast<int>( round( ( wifiPortalTimeOut - remainingTime / 1000 ) ) );
  // if config mode is on, test the time out status.
  if ( wifiConfigModeOn ) {
    wifiMan.process();
    // check for timeout
    if ( remainingTime > ( wifiPortalTimeOut * 1000 ) ) {
      Serial.println( "portaltimeout" );
      wifiMan.stopConfigPortal();
      wifiConfigModeOn = false;
      wifiConfirmed    = false;
      lv_scr_load( Birdscreen );
    } else {
      Serial.printf( "Waitingon wifi portal to time out %d \n", remainingTime );
      lv_label_set_text_fmt(
          WiFiScreenLabel4,
          " Device will exit configuration mode and resume in %d seconds. ", roundedValue
      );
    }
  }

  if ( wifiConfirmed && !wifiConfigModeOn ) { // is configuration mode requested, but not on yet?
    wifiConfigModeOn = true;
    startTime        = millis();
    char line2[80]   = "Network - Password :\n ";
    char line3[80]   = "Host/IP : ";
    Serial.println( "Button Pressed, Starting Config Portal" );
    wifiMan.setConfigPortalBlocking( false );
    if ( strlen( configPortalpassword ) == 0 ) {
      wifiMan.startConfigPortal( myHostname );
      strncat( line2, myHostname, sizeof( line2 ) - strlen( line2 ) - 1 );
      strncat( line2, " - no password ", sizeof( line2 ) - strlen( line2 ) - 1 );
    } else {
      wifiMan.startConfigPortal( myHostname, configPortalpassword );
      strncat( line2, myHostname, sizeof( line2 ) - strlen( line2 ) - 1 );
      strncat( line2, " / ", sizeof( line2 ) - strlen( line2 ) - 1 );
      strncat( line2, configPortalpassword, sizeof( line2 ) - strlen( line2 ) - 1 );
    }
    strncat( line3, myHostname, sizeof( line3 ) - strlen( line3 ) - 1 );
    strncat( line3, " : ", sizeof( line3 ) - strlen( line3 ) - 1 );
    strncat( line3, WiFi.softAPIP().toString().c_str(), sizeof( line3 ) - strlen( line3 ) - 1 );

    wifiInfo();
    lv_scr_load( wifiScreen );
    lv_label_set_text( WiFiScreenLabel2, "" );
    lv_label_set_text( WiFiScreenLabel3, "" );
    lv_label_set_text( WiFiScreenLabel2, line2 );
    lv_label_set_text( WiFiScreenLabel3, line3 );
    lv_task_handler(); // let the GUI do its work
  }
}

// function to read the light sensor and set the backlight
void updateBacklighting() {
  int lightLevel       = analogRead( LIGHT_SENSOR_PIN );
  int targetBrightness = map( lightLevel, 0, 1500, 255, 10 );    // Adjust to scale and reverse the range
  targetBrightness     = constrain( targetBrightness, 10, 255 ); // Double check it's within PWM limits
  analogWrite( BACKLIGHT_PIN, targetBrightness );                // Set the backlight brightness
}
// function to initialize the LGVL setup
void init_LGVL() {
  String LVGL_Arduino = String( "LVGL Library Version: " ) + lv_version_major() +
                        "." + lv_version_minor() + "." + lv_version_patch();
  Serial.println( LVGL_Arduino );
  // Start LVGL
  lv_init();

  // Create a display object
  lv_display_t *disp;
  // Initialize the TFT display using the TFT_eSPI library
  disp = lv_tft_espi_create( SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof( draw_buf ) );
  lv_display_set_rotation( disp, LV_DISPLAY_ROTATION_90 );

  // Initialize an LVGL input device object (Touchscreen)
  lv_indev_t *inputDev = lv_indev_create();
  lv_indev_set_type( inputDev, LV_INDEV_TYPE_POINTER );
  // Set the callback function to read Touchscreen input
  lv_indev_set_read_cb( inputDev, touchscreen_read );
}

void mqttMessageCallback( const char *topic, char *payload ) {
  // payload might be binary, but PicoMQTT guarantees that it's zero-terminated
  Serial.printf( "Received message in topic '%s': %s\n", topic, payload );
  // split the mqtt messagge into elements
  //  https://stackoverflow.com/questions/57949764/in-c-split-name-value-pairs-into-arrays
  char *KVPpointers[NOTICE_ELEMENTS][2] = {};
  char *token;
  char notFound[]     = "";
  bool newBird        = false;
  int matchingBird    = MATCH_NOT_FOUND;
  int kvp             = 0;
  int kvpCNkey        = -1;
  int oldestBirdIndex = 0;
  // and a flag that indicates whether it is the key or value we are parsing
  // key when kvpState=0, value when kvpState=1
  constexpr int ISA_KEY   = 0;
  constexpr int ISA_VALUE = 1;
  int kvpState            = ISA_KEY; // the key should be first

  int validationCount = countEquals( payload ); // messages should be in the form  CN=$comname;SN=$sciname;TM=$time;CF=$confidencepct;RS=$reason

  if ( validationCount != 5 ) { // if we cannot parse the data, manually create an entry. Not going to try to match against an existing entry
    Serial.printf( "Incorrectly formated mqtt mesage received. '%s' \n", payload );
    oldestBirdIndex = findOldestEntry( birds, MAX_NOTICES );
    if ( oldestBirdIndex == -1 ) {
      oldestBirdIndex = 0;
    }
    strncpy( birds[oldestBirdIndex]->CN, payload, 74 );
    strncpy( birds[oldestBirdIndex]->SN, "none", 5 );
    strncpy( birds[oldestBirdIndex]->RS, "", 20 );
    birds[oldestBirdIndex]->TM = time( nullptr );
    birds[oldestBirdIndex]->CF = 0;

    curDisplay = oldestBirdIndex;

  } else {

    char *thisdata = strstr( payload, "\n" ) + strlen( "\n" );
    // we do this first to set the target variable. future calls reference the
    // output of the previous call
    KVPpointers[kvp][kvpState] = strtok( thisdata, "=" );

    while ( kvp < NOTICE_ELEMENTS && KVPpointers[kvp][kvpState] != NULL ) { // loop through splitting it up and saving the pointers

      if ( kvpState == ISA_VALUE ) { // update kvpState and key_value
        kvp++;
        kvpState                   = ISA_KEY;
        KVPpointers[kvp][kvpState] = strtok( NULL, "=" );
      } else {                                                  // if (kvpState == ISA_KEY)
        if ( strcmp( KVPpointers[kvp][ISA_KEY], "CN" ) == 0 ) { // Common Name is needed to check for new sightings
          kvpCNkey = kvp;
        }
        kvpState = ISA_VALUE;

        token = strtok( NULL, ";" ); // test to see if there is a value for the key. empty values pull in the next key equals
        if ( strchr( token, '=' ) == NULL ) {
          KVPpointers[kvp][kvpState] = token;
        } else {
          KVPpointers[kvp][kvpState] = notFound;
        }
      }
    }
  }

  if ( kvpCNkey > -1 ) { // if we  have a common name, process the sighting, otherwise skip it
    // test if new sighting. do not add duplicate birds to the display array
    matchingBird = isBirdInArray( birds, MAX_NOTICES, KVPpointers[kvpCNkey][ISA_VALUE] );
    if ( matchingBird == MATCH_NOT_FOUND ) {
      newBird = true;
    } else {
      newBird = false;
    }

    // now it should all be parsed
    // if it is not in the current array of birds, we add it to the array
    if ( newBird ) {
      oldestBirdIndex = findOldestEntry( birds, MAX_NOTICES );
      if ( oldestBirdIndex == -1 ) {
        oldestBirdIndex = 0;
      }
      for ( int i = 0; i < kvp; i++ ) {
        printf( "\n Match key[%d] = %s\nvalue[%d] = %s\n", i, KVPpointers[i][ISA_KEY], i, KVPpointers[i][ISA_VALUE] );
        if ( strcmp( KVPpointers[i][ISA_KEY], "CN" ) == 0 ) {
          strncpy( birds[oldestBirdIndex]->CN, KVPpointers[i][ISA_VALUE], 74 );
        } else if ( strcmp( KVPpointers[i][ISA_KEY], "SN" ) == 0 ) {
          strncpy( birds[oldestBirdIndex]->SN, KVPpointers[i][ISA_VALUE], 74 );
        } else if ( strcmp( KVPpointers[i][ISA_KEY], "RS" ) == 0 ) {
          strncpy( birds[oldestBirdIndex]->RS, KVPpointers[i][ISA_VALUE], 30 );
          Serial.print( " Setting reason as " );
          Serial.println( KVPpointers[i][ISA_VALUE] );
        } else if ( strcmp( KVPpointers[i][ISA_KEY], "TM" ) == 0 ) {
          birds[oldestBirdIndex]->TM = time( nullptr );
        } else if ( strcmp( KVPpointers[i][ISA_KEY], "CF" ) == 0 ) {
          birds[oldestBirdIndex]->CF = atoi( KVPpointers[i][ISA_VALUE] );
        } else {
          printf( "\nkey[%d] = %s\nvalue[%d] = %s\n", i, KVPpointers[i][ISA_KEY], i, KVPpointers[i][ISA_VALUE] );
        }
      }
      curDisplay = oldestBirdIndex;
    } else { // not a new bird so update the time stamp of the existing entry
      for ( int i = 0; i < kvp; i++ ) {
        if ( strcmp( KVPpointers[i][ISA_KEY], "TM" ) == 0 ) {
          birds[matchingBird]->TM = time( nullptr );
        }
      }
      curDisplay = matchingBird;
    }
    // Output the copied data from the struct array
    for ( int i = 0; i < MAX_NOTICES; ++i ) {
      Serial.print( "Entry " );
      Serial.print( i + 1 );
      Serial.print( " - CN: " );
      Serial.print( birds[i]->CN );
      Serial.print( " - SN: " );
      Serial.print( birds[i]->SN );
      Serial.print( " - RS: " );
      Serial.print( birds[i]->RS );
      Serial.print( " - TM: " );
      Serial.print( ctime( &birds[i]->TM ) );
      Serial.print( " - CF: " );
      Serial.println( birds[i]->CF );
    }
    // end of call back on mqtt recv.
  } else {
    Serial.println( "did not see a valid name" );
  } // did we get a valid common name
}

void WiFi_AP_Mode_Callback( WiFiManager *myWiFiManager ) {
  Serial.println( "[CALLBACK] configModeCallback fired" );
  char line2[80] = "Network : ";
  char line3[80] = "Host/IP : ";
  if ( strlen( configPortalpassword ) == 0 ) {
    wifiMan.startConfigPortal( myHostname );
    strncat( line2, myHostname, sizeof( line2 ) - strlen( line2 ) - 1 );
    strncat( line2, "\n no password ", sizeof( line2 ) - strlen( line2 ) - 1 );
  } else {
    Serial.println( "starting wifiman config mode" );
    wifiMan.startConfigPortal( myHostname, configPortalpassword );
    strncat( line2, myHostname, sizeof( line2 ) - strlen( line2 ) - 1 );
    strncat( line2, " / ", sizeof( line2 ) - strlen( line2 ) - 1 );
    strncat( line2, configPortalpassword, sizeof( line2 ) - strlen( line2 ) - 1 );
  }
  strncat( line3, myHostname, sizeof( line3 ) - strlen( line3 ) - 1 );
  strncat( line3, "  :  ", sizeof( line3 ) - strlen( line3 ) - 1 );
  strncat( line3, WiFi.softAPIP().toString().c_str(), sizeof( line3 ) - strlen( line3 ) - 1 );
  wifiInfo();
  lv_scr_load( wifiScreen );
  lv_label_set_text( WiFiScreenLabel2, line2 );
  lv_label_set_text( WiFiScreenLabel3, line3 );
  lv_task_handler(); // let the GUI do its work
}

// Function to update the clock
void update_clock() {
  // the loop timer calls this, so we don't need LV to do the checking on timing
  time_t now = time( nullptr );
  if ( now > 1700000000 ) { // do not display the clock until we get ntp time
    struct tm *tm_info = localtime( &now );
    int hour           = tm_info->tm_hour;
    if ( hour == 0 ) {
      hour = 12; // Midnight
    } else if ( hour > 12 ) {
      hour -= 12;
    }

    lv_obj_set_style_text_font( label_minute, &lv_font_montserrat_40, 0 );
    snprintf( hour_str, sizeof( hour_str ), "%02d", hour );
    snprintf( minute_str, sizeof( minute_str ), "%02d", tm_info->tm_min );
    snprintf( second_str, sizeof( second_str ), "%02d", tm_info->tm_sec );
    lv_label_set_text( label_hour, hour_str );
    lv_label_set_text( label_minute, minute_str );
    lv_label_set_text( label_second, second_str );
  }
}

// function to setup the clock display at the bottom of the birdscreen
void setup_clock() {
  label_hour   = lv_label_create( Birdscreen );
  label_minute = lv_label_create( Birdscreen );
  label_second = lv_label_create( Birdscreen );

  lv_obj_set_style_text_font( label_hour, &lv_font_montserrat_40, 0 );
  lv_obj_set_style_text_font( label_minute, &lv_font_montserrat_34, 0 );
  lv_obj_set_style_text_font( label_second, &lv_font_montserrat_40, 0 );

  lv_obj_align( label_hour, LV_ALIGN_BOTTOM_MID, -70, 0 );
  lv_obj_align( label_minute, LV_ALIGN_BOTTOM_MID, 0, 0 );
  lv_obj_align( label_second, LV_ALIGN_BOTTOM_MID, 70, 0 );

  lv_label_set_text( label_hour, "" );
  lv_label_set_text( label_minute, "Setting the clock" );
  lv_label_set_text( label_second, "" );

  // Initial call to set the clock
  update_clock();
}

// function to setup wifi manager and timezone support
bool setup_wifi() {
  Serial.println( "Entering setup_wifi" );
  bool connectionResult;
  wifiMan.addParameter( &hostnameParam ); // Add this parameter to the WiFiManager portal
  hostnameParam.setValue( myHostname, sizeof( myHostname ) );
  wifiMan.setSaveParamsCallback( wifiMan_Save_Params_Callback ); // if the config page saves params.
  wifiMan.setAPCallback( WiFi_AP_Mode_Callback );
  wifiMan.setConnectTimeout( 30 ); // 30 seconds until autoconnect timeout

  //  ******** Time Zone support
  WiFiManagerNS::init( &wifiMan, nullptr );
  // /!\ make sure "custom" is listed there as it's required to pull the "Setup Clock" button
  std::vector<const char *> menu = { "wifi", "info", "custom", "param", "sep", "restart", "exit" };
  wifiMan.setMenu( menu );
  //  ****** end Time zone support setup

  String storedSSID = wifiMan.getWiFiSSID(); // Attempt to retrieve stored SSID
  if ( storedSSID.isEmpty() ) {              // if there is no stored SSID, do not try to autoconnect
    Serial.println( "No SSID stored found in wifi setup function" );
    Serial.println( storedSSID );
    wifiMan.startConfigPortal( myHostname );
    connectionResult = true;
  } else {
    Serial.print( "Stored SSID: " );
    Serial.println( storedSSID );
    if ( strlen( configPortalpassword ) == 0 ) {
      connectionResult = wifiMan.autoConnect( myHostname ); // not password protected AP and hostname is used as temp wifi name
    } else {
      connectionResult = wifiMan.autoConnect( myHostname, configPortalpassword ); //  password protected ap, hostname is used as temp wifi name
    }
  }

  Serial.println( "Leaving Wifi Setup" );
  return connectionResult;
}

void setup() {
  Serial.begin( 115200 );

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin( XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS );
  touchscreen.begin( touchscreenSPI );
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might
  // need to set the rotation to 0: touchscreen.setRotation(0);
  touchscreen.setRotation( 2 );
  tft.init();
  tft.fillScreen( TFT_BLACK );
  tft.setTextColor( TFT_WHITE );
  tft.setRotation( 1 );
  tft.setTextSize( 2 );

  start_loadingScreen();
  init_LGVL();
  // Create the screens
  create_Birdscreen();
  create_wifiConfirmScreen();
  create_wifiStartScreen();
  setup_clock();

  // set initial nightmode/not nightmode config
  process_screen_objects_night( Birdscreen );
  process_screen_objects_night( wifiConfirmScreen );
  process_screen_objects_night( wifiScreen );

  // start up wifi, use stored params if available
  bool wifiResult;
  wifiConfigModeOn = true; // default to wificonfig mode until autoconnect works
  loadHostname();
  wifiResult = setup_wifi();

  if ( !wifiResult ) { // we didn't get a valid wifi autoconnect
    Serial.println( "Failed to connect" );
    tft.println( "Wifi Manager failed to startup correctly. Device will restart "
                 "in 1 minute." );
    delay( 60000 ); // let this time pass
    ESP.restart();
  } else {
    WiFiManagerNS::configTime();
    wifiConfigModeOn = false;
    // if you get here you have connected to the WiFi
    WiFi.setHostname( myHostname );
    // Set the hostname for mDNS
    if ( !MDNS.begin( myHostname ) ) {
      Serial.println( "Error setting up mDNS responder. Unable to continue" );
      while ( 1 ) {
        delay( 1000 );
      }
    }

    Serial.print( "mDNS responder started with hostname: " );
    Serial.println( myHostname ); // Access the device at http://hostname.local
    // Print the IP address
    Serial.print( "IP address: " );
    Serial.println( WiFi.localIP() );

    for ( int i = 0; i < MAX_NOTICES; ++i ) {
      birds[i] = new dataLayout(); // Allocate memory for each bird entry
    }

    // Subscribe to a topic and attach a callback
    mqttServer.subscribe( "#", []( const char *topic, char *payload ) {
      mqttMessageCallback( topic, payload );
    } );
    mqttServer.begin();

    // Load the first screen at the start
    lv_scr_load( Birdscreen );
  }
}

void loop() {
  int loopduration = millis() - lastUpdate;
  if ( loopduration >= 1000 ) { // Update every second
    lastUpdate = millis();
    update_clock(); // Update the clock
  }
  lv_tick_inc( 25 );  // tell LVGL how much time has passed
  lv_task_handler();  // let the GUI do its work
  delay( 25 );        // let this time pass
  lv_timer_handler(); // LVGL needs to hanlde refreshes

  mqttServer.loop(); // give the mqqt process a chance to update the bird data
  if ( !wifiConfigModeOn ) {
    displayBirds( &curDisplay );
  }
  doWifiManager();
  updateBacklighting();
  removeOldEntries( birds, MAX_NOTICES );
}