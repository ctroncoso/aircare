// bmeHelper.cpp — BME280 sensor implementation.
#include "bmeHelper.h"

namespace bme
{
    bool initBME()
    {
        unsigned status;
        status = bme.begin(0x76);
        if (!status)
        {
            Serial.println("No se encontró un bme280. Verifique cableado.");
            Serial.print("SensorID es: 0x");
            Serial.println(bme.sensorID(), 16);
            return false;
        }
        bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                        Adafruit_BME280::SAMPLING_X16,
                        Adafruit_BME280::SAMPLING_X16,
                        Adafruit_BME280::SAMPLING_X16,
                        Adafruit_BME280::FILTER_OFF);
        return true;
    }
}