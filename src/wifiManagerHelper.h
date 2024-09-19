#pragma once

#include "globals.h"
#include <WiFiManager.h>

namespace wifiM{

    void configModeCallback(WiFiManager *wm) {
        leds::blinkLed(ledPinG, 4, true);
    }

    bool initWifiM(){
        wm.setAPCallback(configModeCallback);
        wm.setConfigPortalTimeout(PORTAL_TIMEOUT);

        int attempts = 5;
        bool res = false;
        while (attempts > 0 && !res)
        {
            res = wm.autoConnect(PORTAL_NAME);
            if (!res)
            {
                delay(60000);
                attempts--;
            }
        }
        return res;
    }
}