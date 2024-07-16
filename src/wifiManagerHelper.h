#pragma once

#include "globals.h"
#include "ledHelper.h"
#include <WiFiManager.h>

namespace wifiM{

    void configModeCallback(WiFiManager *wm) {
        leds::blinkLed(ledPinG, 4, true);
    }

    void initWifiM(){
        wm.setAPCallback(configModeCallback);
        wm.setConfigPortalTimeout(PORTAL_TIMEOUT);
        bool res = wm.autoConnect(PORTAL_NAME); 

        if(!res) {
            Serial.println("Failed to connect. Waiting 3 minutes and restarting.");
            for (int i = 0; i <= 3; i++) {
                leds::blinkLed(ledPinR,2);
                delay(500);
            }
            delay(180000);  // wait 3 minutes
            ESP.restart();
        } 
        else {
            //if you get here you have connected to the WiFi    
            Serial.println("connected...yeey :)");
        }
    }
}