// bmeHelper.h — BME280 temperature/pressure/humidity sensor (declarations).
#pragma once

#include "globals.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

namespace bme
{
    inline Adafruit_BME280 bme; // I2C (inline so multiple TUs are safe)
    inline float temp = 0;
    inline float pressure = 0;
    inline float altitude = 0;
    inline float humidity = 0;

    bool initBME();
}