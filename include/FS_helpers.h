#pragma once
#include <FS.h>
#include <LittleFS.h>

void list_LittleFS( void ) {
  Serial.println( F( "\r\nListing LittleFS files:" ) );
  static const char line[] PROGMEM = "=================================================";
  Serial.println( FPSTR( line ) );
  Serial.println( F( "  File name                              Size" ) );
  Serial.println( FPSTR( line ) );
  fs::File rootHandle = LittleFS.open( "/", "r" );
  if ( !rootHandle ) {
    Serial.println( F( "Failed to open directory" ) );
    return;
  }
  if ( !rootHandle.isDirectory() ) {
    Serial.println( F( "Not a directory" ) );
    return;
  }

  fs::File fileHandle = rootHandle.openNextFile();
  while ( fileHandle ) {
    if ( fileHandle.isDirectory() ) {
      Serial.print( "DIR : " );
      const char *fileName = fileHandle.name();
      Serial.print( fileName );
    } else {
      const char *fileName = fileHandle.name();
      Serial.printf( "   %s", fileName );
      // File path can be 31 characters maximum in LittleFS
      int spaces = 33 - strlen( fileName ); // Tabulate nicely
      if ( spaces < 1 )
        spaces = 1;
      while ( spaces-- )
        Serial.print( " " );
      String fileSize = ( String ) fileHandle.size();
      spaces          = 10 - fileSize.length(); // Tabulate nicely
      if ( spaces < 1 )
        spaces = 1;
      while ( spaces-- )
        Serial.print( " " );
      Serial.println( fileSize + " bytes" );
    }
    fileHandle = rootHandle.openNextFile();
  }

  Serial.println( FPSTR( line ) );
  Serial.println();
  delay( 1000 );
}

// add a line to the metadata file for this file 
bool add_metadata( String imagefileName, const char *metadataFileName ) {
  // Save metadata
  File metaDataFileHandle = LittleFS.open( metadataFileName, "a+" );
  if ( metaDataFileHandle ) {
    metaDataFileHandle.println( imagefileName );
    metaDataFileHandle.close();
    return true;
  }
  return false;
}

void list_metadata_file( const char *filename ) {
  if ( LittleFS.exists( filename ) == true ) {
    File fileHandle = LittleFS.open( filename, "r" );
    if ( !fileHandle ) {
      Serial.println( "Failed to open  metadata file" );
      return;
    }
    Serial.println( "Reading meta data file:" );
    while ( fileHandle.available() ) {
      String line = fileHandle.readStringUntil( '\n' ); // Read each line
      Serial.println( line );
    }

    fileHandle.close();
  } else {
    Serial.println( "Could not find metadata file" );
  }
}

// function to remove the oldest entry. since files are added to the metadata file in order
//  the first entry is the oldest. We don't bother to update times when a bird is seen if we have the image
bool delete_oldest_file( const char *metadataFileName ) {
  Serial.println( "deleting older files to make space" );
  // list_metadata_file( metadataFileName );

  String oldestFile = "";
  String line;
  String fileName;

  File metaDataFileHandle = LittleFS.open( metadataFileName, "r" );
  if ( !metaDataFileHandle ) {
    Serial.println( "Failed to open metadata file" );
    return false;
  }

  if ( metaDataFileHandle.available() ) { // Parse metadata and find the oldest file
    oldestFile = metaDataFileHandle.readStringUntil( '\n' );
    oldestFile.trim();
  }
  Serial.println( "" );
  metaDataFileHandle.close();
  //  Delete the oldest file and update metadata
  if ( oldestFile != "" ) {
    Serial.println( "Deleting oldest file: " + oldestFile );
    LittleFS.remove( oldestFile );
    if ( LittleFS.exists( "/temp_meta.txt" ) ) {
      LittleFS.remove( "/temp_meta.txt" );
    }

    // Update metadata file
    File tempMetaHandle      = LittleFS.open( "/temp_meta.txt", "a" );
    metaDataFileHandle = LittleFS.open( metadataFileName, "r" );
    while ( metaDataFileHandle.available() ) {
      line = metaDataFileHandle.readStringUntil( '\n' );
      line.trim();
      if ( line.startsWith( oldestFile ) ) {
        Serial.print( "removing  data for " );
        Serial.println( line );
      } else {
        tempMetaHandle.println( line );
      }
    }
    metaDataFileHandle.close();
    tempMetaHandle.close();
    LittleFS.remove( metadataFileName );
    LittleFS.rename( "/temp_meta.txt", metadataFileName );

  } else {
    Serial.println( "No files to delete" );
    return false;
  }
  return true;
}

void delete_all_jpg_files() {
  File rootHandle = LittleFS.open( "/" );
  File fileHandle = rootHandle.openNextFile();
  String fileName;
  String fullName;

  while ( fileHandle ) {
    fileName = fileHandle.name();
    fullName = "/" + fileName;
    if ( fileName.endsWith( ".jpg" ) ) {
      Serial.print( "Deleting file: " );
      Serial.print( fullName );
      fileHandle.close();
      if ( LittleFS.remove( fullName ) ) {
        Serial.println( " Deleted successfully" );
      } else {
        Serial.println( " Failed to delete" );
      }
    }
    fileHandle = rootHandle.openNextFile();
  }
}

void clean_up_jpg_files( const char *metadataFileName ) {
  Serial.println( "Testing meta file exists" );
  if ( LittleFS.exists( metadataFileName ) == true ) {
    Serial.println( "Confirmed  meta file exists" );
  } else {
    Serial.println( "No meta data file, deleting all jpgs" );
    delete_all_jpg_files();
  }
}



// Function to delete JPEG files not listed in the metadata file
void delete_unlisted_jpg_files( const char *metadataFileName ) {
  std::vector<String> FileList;
  String fileToDelete;
  File metaDataFileHandle = LittleFS.open( metadataFileName, "r" );
  if ( !metaDataFileHandle ) {
    Serial.println( "Failed to open metadata file." );
    return;
  }
  // get this list of tracked files
  while ( metaDataFileHandle.available() ) {
    String line = metaDataFileHandle.readStringUntil( '\n' );
    line.trim(); // Remove trailing newline or whitespace
    if ( !line.isEmpty() ) {
      FileList.push_back( line );
    }
  }
  metaDataFileHandle.close();

  File rootHandle       = LittleFS.open( "/" );
  File fileHandle = rootHandle.openNextFile();

  while ( fileHandle ) { // Step 2: Iterate through the filesystem and find all JPEG files
    String fileName = fileHandle.name();
    fileHandle.close();

    if ( fileName.endsWith( ".jpg" ) ) {
      // Check if the filename is in the validFiles list
      fileToDelete = fileName;
      bool isValid = false;
      for ( const auto &thisFile : FileList ) {
        if ( thisFile == "/" + fileName ) {
          isValid = true;
          break;
        }
      }

      // Step 3: Delete the file if not valid
      if ( !isValid ) {
        if ( LittleFS.remove( "/" + fileName ) ) {
          Serial.print( "File deleted successfully.  " );
          Serial.println( fileToDelete );
        } else {
          Serial.print( "Failed to delete file.  " );
          Serial.println( fileToDelete );
        }
      }
    }
    fileHandle = rootHandle.openNextFile(); // Move to the next file
  }
}
