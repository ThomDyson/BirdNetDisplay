#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include "FS_helpers.h"
#include "Web_Fetch.h"
// Function to look up a URL by key value. 
//data file format is Bird Name,https://www.com
String lookup_bird_URL( const char *dataFileName, const char *searchBird ) {
  String line = "";
  File datafileHandle = LittleFS.open( dataFileName, "r" );
  if ( !datafileHandle ) {
    Serial.println( "Failed to open file for reading" );
    return "XX";
  }
  while ( datafileHandle.available() ) {
    line = datafileHandle.readStringUntil( '\n' );
    int commaIndex = line.indexOf( ',' );
    if ( commaIndex == -1 ) {
      continue; // Skip lines without a comma
    }
    // Extract the bird name and URL fields
    String key = line.substring( 0, commaIndex );
    String URL = line.substring( commaIndex + 1 );
    // Trim whitespace or newlines
    key.trim();

    // Check if key matches the search term
    if ( key.equalsIgnoreCase( searchBird ) ) {
      datafileHandle.close();
      URL.trim();
      return URL;
    }
  }
  datafileHandle.close();
  return ""; // Return empty if not found
}

// function to clean up the bird name to use as a filename
String format_filename( String input ) {
  input.replace( " ", "_" ); // Replace all spaces
  input.toLowerCase();
  return "/" + input + ".jpg"; // Add leading '/' and '.jpg'
}

void cleanup_file_system_space( const char *metadataFileName ) {
  bool cleanupGood = true;
  // list_metadata_file( metadataFileName );
  // Get file system information
  size_t totalBytes = LittleFS.totalBytes(); // Total space
  size_t freeBytes  = totalBytes - LittleFS.usedBytes();
  Serial.print( "Free file space " );
  Serial.println( freeBytes );

  size_t lowFileSpaceLimit   = totalBytes * 0.2;
  size_t clearFileSpaceLimit = totalBytes * .5; // this multiplier needs to be larger than the number above
  Serial.print( "low limit " );
  Serial.print( lowFileSpaceLimit );
  Serial.print( "     Clear limit" );
  Serial.println( clearFileSpaceLimit );
  if ( freeBytes < lowFileSpaceLimit )
    while ( freeBytes < clearFileSpaceLimit && cleanupGood ) {
      delay( 1000 );
      cleanupGood = delete_oldest_file( metadataFileName );
      freeBytes   = totalBytes - LittleFS.usedBytes(); // Calculate free space
    }
  list_LittleFS();
  list_metadata_file( metadataFileName );
}

// function to lookup bird info and get file name
String get_image_info( const char *birdName, const char *dataFilePath, const char *metaDataFileName ) {
  bool haveValidURL    = false;
  bool haveImage       = false;
  String imageFileName = format_filename( birdName );

  if ( LittleFS.exists( imageFileName ) == true ) { // If it exists then no need to fetch it
    Serial.println( "Found " + imageFileName );
    haveImage = true;
  } else {
    String imageURL = lookup_bird_URL( dataFilePath, birdName ); // Look up the URL for the given bird name
    if ( imageURL.equals( "XX" ) ) {
      Serial.println( "Could not open bird lookup file." );
    } else {
      if ( imageURL.isEmpty() ) {
        Serial.print( birdName );
        Serial.println( " Bird not found" );
      } else {
        Serial.printf( "URL for '%s': %s\n", birdName, imageURL.c_str() );
        haveValidURL = true;
      }
    }

    if ( haveValidURL ) {
      cleanup_file_system_space( metaDataFileName ); // mak sure there is room of any download
      bool loaded_ok = getImageFile( imageURL, imageFileName );
      if ( loaded_ok ) {
        haveImage = true;
        Serial.println( "Adding data to metadata file" );
        if ( !add_metadata( imageFileName, metaDataFileName ) ) { // if we can't add the file to the meta data tracker, delete the file
          Serial.println( "Unable to add file to metadata" );
          LittleFS.remove( imageFileName );
          haveImage = false;
        };
      }
    } else {
      Serial.println( "No valid URL found" );
    }
  }
  if ( haveImage ) {
    return imageFileName;
  } else {
    return "";
  }
}
