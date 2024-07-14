#pragma once

#include "globals.h"
#include "sunriseHelper.h"



namespace leds{
    void initLEDS(){
        pinMode(ledPinR, OUTPUT);
        pinMode(ledPinY, OUTPUT);
        pinMode(ledPinG, OUTPUT);    
    }

    void clearLeds(){
        digitalWrite(ledPinR, LOW);
        digitalWrite(ledPinY, LOW);
        digitalWrite(ledPinG, LOW);
    }
    void setLedOnCO2Condition(CO2_Condition co2_State){
        u_int8_t riskLevel;
        clearLeds();
        switch (co2_State)
        {
        case CO2_Condition::Green:
            digitalWrite(ledPinG, HIGH);
            break;
        case CO2_Condition::Yellow :
            digitalWrite(ledPinY, HIGH);
            break;
        case CO2_Condition::Red:
            digitalWrite(ledPinR, HIGH);
            break;
        case CO2_Condition::Unknown:
            break;
        default:
            break;
        }
    }    

    void blinkLed(int led, int count){
        clearLeds();
        while(count != 0){
            digitalWrite(led,HIGH);
            delay(100);
            digitalWrite(led,LOW);
            delay(100);
            count--;
        }
        setLedOnCO2Condition(co2_State);
    }
}