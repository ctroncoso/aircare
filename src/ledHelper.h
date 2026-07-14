// ledHelper.h — status LED indicators (declarations).
#pragma once

#include "core/board.h"   // ledPinR/Y/G, CO2_Condition
#include "globals.h"      // co2_State

namespace leds
{
    bool initLEDS();
    void POSTBlinks();
    void clearLeds();
    void setLedOnCO2Condition(CO2_Condition co2_State);
    void blinkLed(int led, int count, bool restoreCondition = true);
    void flipLed(int led);
}