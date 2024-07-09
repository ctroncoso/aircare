#pragma once

#include "globals.h"
#include "sunriseHelper.h"

#define CO2_LOW (700)
#define CO2_HIGH (800)

namespace leds{
    void initLEDS(){
        pinMode(ledPinR, OUTPUT);
        pinMode(ledPinY, OUTPUT);
        pinMode(ledPinG, OUTPUT);    
    }

    void trigger_leds(uint16_t co2_level){
    u_int8_t riskLevel;
    digitalWrite(ledPinR, LOW);
    digitalWrite(ledPinY, LOW);
    digitalWrite(ledPinG, LOW);
    if (co2_level < CO2_LOW)     { digitalWrite(ledPinY, HIGH); } 
    if (co2_level >= CO2_LOW && 
        co2_level < CO2_HIGH )     { digitalWrite(ledPinG, HIGH); } 
    if (co2_level >= CO2_HIGH)     { digitalWrite(ledPinR, HIGH); } 
    
        
        // ledcWrite(ledChannelR, 1);
        // ledcWrite(ledChannelY, 2);
        // ledcWrite(ledChannelG, 14);
    }    
}