#pragma once

#include "globals.h"
#include "sunriseHelper.h"



namespace leds{
    void initLEDS();
    void clearLeds();
    void setLedOnCO2Condition(CO2_Condition co2_State);
    void blinkLed(int led, int count, bool restoreCondition=true);

    void initLEDS(){
        pinMode(ledPinR, OUTPUT);
        pinMode(ledPinY, OUTPUT);
        pinMode(ledPinG, OUTPUT);    
        clearLeds();
    }

    void clearLeds(){
        digitalWrite(ledPinR, LOW);
        digitalWrite(ledPinY, LOW);
        digitalWrite(ledPinG, LOW);
    }

    void setLedOnCO2Condition(CO2_Condition co2_State){
        clearLeds();
        switch (co2_State){
            case CO2_Condition::Green   :   digitalWrite(ledPinG, HIGH); break;
            case CO2_Condition::Yellow  :   digitalWrite(ledPinY, HIGH); break;
            case CO2_Condition::Red     :   digitalWrite(ledPinR, HIGH); break;
            case CO2_Condition::Unknown :   break;
            default                     :   break;
        }
    }    

    void blinkLed(int led, int count, bool restoreCondition){
        clearLeds();
        while(count != 0){
            digitalWrite(led,HIGH);
            delay(100);
            digitalWrite(led,LOW);
            delay(200);
            count--;
        }
        if (restoreCondition) setLedOnCO2Condition(co2_State);
    }

    void flipLed(int led){
        digitalWrite(led, !digitalRead(led));
    }
}