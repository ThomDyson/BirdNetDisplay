// based on example code from  https://github.com/Bodmer/TJpg_Decoder
#pragma once

// Fetch a file from the URL given and save it in LittleFS
// Return 1 if a web fetch was needed or 0 if file already exists
bool getImageFile( String url, String filename ) {
  Serial.println( "Downloading " + filename + " from " + url );

  // Check WiFi connection
  if ( ( WiFi.status() == WL_CONNECTED ) ) {
    Serial.print( "[HTTP] begin...\n" );
    HTTPClient http;
    http.begin( url );
    Serial.print( "[HTTP] GET...\n" );
    // Start connection and send HTTP header
    int httpCode = http.GET();
    if ( httpCode == 200 ) {
      fs::File imageFileHandle = LittleFS.open( filename, "w+" );
      if ( !imageFileHandle ) {
        Serial.println( "file open failed" );
        return false;
      }
      // HTTP header has been send and Server response header has been handled
      Serial.printf( "[HTTP] GET... code: %d\n", httpCode );

      if ( httpCode == HTTP_CODE_OK ) {
        int total = http.getSize();         // Get length of document (is -1 when Server sends no Content-Length header)
        int streamLength   = total;

        uint8_t buff[128] = { 0 };   // Create buffer for read
        WiFiClient *stream = http.getStreamPtr();

        while ( http.connected() && ( streamLength > 0 || streamLength == -1 ) ) {
          // Get available data size
          size_t size = stream->available();
          if ( size ) {
            int c = stream->readBytes( buff, ( ( size > sizeof( buff ) ) ? sizeof( buff ) : size ) );   // Read up to buffer size
            // Write it to file
            imageFileHandle.write( buff, c );
            // Calculate remaining bytes
            if ( streamLength > 0 ) {
              streamLength -= c;
            }
          }
          yield();
        }
      
        Serial.println();
        Serial.print( "[HTTP] connection closed or file end.\n" );
      }
      imageFileHandle.close();
    } else {
      Serial.print( "[HTTP] GET... failed, error: " );
      Serial.println(httpCode);
      return false;
    }
    http.end();
  }
  return true;
}