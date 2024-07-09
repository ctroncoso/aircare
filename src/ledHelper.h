#pragma once

#include "globals.h"
#include "sunriseHelper.h"

namespace leds{
    void initLEDS(){
        pinMode(ledPinR, OUTPUT);
        pinMode(ledPinY, OUTPUT);
        pinMode(ledPinG, OUTPUT);    
    }

    void trigger_leds(){
    u_int8_t riskLevel;
    digitalWrite(ledPinR, LOW);
    digitalWrite(ledPinY, LOW);
    digitalWrite(ledPinG, LOW);
    if (sunriseH::co2_fc < 700)     { digitalWrite(ledPinY, HIGH); } 
    if (sunriseH::co2_fc >= 700 && 
        sunriseH::co2_fc <800 )     { digitalWrite(ledPinG, HIGH); } 
    if (sunriseH::co2_fc >=800)     { digitalWrite(ledPinR, HIGH); } 
    
        
        // ledcWrite(ledChannelR, 1);
        // ledcWrite(ledChannelY, 2);
        // ledcWrite(ledChannelG, 14);
    }    
}