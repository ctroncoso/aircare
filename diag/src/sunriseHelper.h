// sunriseHelper.h — Senseair Sunrise CO2 sensor wrapper (declarations).
#pragma once

#include "globals.h"
#include <Wire.h>
#include "sunrise_i2c.h"

namespace sunriseH
{
    inline sunrise co2sensor;

    inline uint16_t co2_fc = 0;
    inline uint16_t co2_uc = 0;
    inline uint16_t co2_fu = 0;
    inline uint16_t co2_uu = 0;

    bool initCo2Sensor();
}