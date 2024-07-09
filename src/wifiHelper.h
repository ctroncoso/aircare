#pragma once

#include <WiFi.h>
#include "wifiCredentials.h"

namespace wifi{
 
    /* 
    Create a file "wifiCredentials.h" with the following code in it:

        namespace wifi
        {
            const char* ssid = ""; 
            const char* password = "";    
        } // namespace wifi

    Fill in SSID and password. 
    */ 


    WiFiClient espClient;  // wifi 


    void initWifi(){

        WiFi.begin(ssid, password);

        Serial.print("Conectando a Wifi.");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("");
        Serial.println("Connected.");        
    }
}