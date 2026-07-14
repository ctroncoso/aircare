// sunriseHelper.h — Senseair Sunrise CO2 sensor wrapper (declarations).
#pragma once

#include "globals.h"
#include <Wire.h>
#include "sunrise_i2c.h"

namespace sunriseH
{
    inline sunrise co2sensor;

    uint16_t co2_fc;
    uint16_t co2_uc;
    uint16_t co2_fu;
    uint16_t co2_uu;

    bool initCo2Sensor();
}