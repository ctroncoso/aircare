// bmeHelper.h — BME280 temperature/pressure/humidity sensor (declarations).
#pragma once

#include "globals.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

namespace bme
{
    inline Adafruit_BME280 bme; // I2C (inline so multiple TUs are safe)
    float temp;
    float pressure;
    float altitude;
    float humidity;

    bool initBME();
}