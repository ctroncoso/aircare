// sunriseHelper.cpp — Senseair Sunrise CO2 sensor wrapper implementation.
#include "sunriseHelper.h"
#include "mqttHelper.h"  // mqtt::mqttPump() — keep keepalive alive during blocking init

namespace sunriseH
{
    bool initCo2Sensor()
    {
        if (!co2sensor.initSunrise())
        {
            return false; // initialization failed
        }
        delay(100);
        co2sensor.setMeasurementPeriod(measurementDelay / 1000);
        co2sensor.setNbrSamples(16);
        co2sensor.setMeasurementMode(CONTINOUS);
        co2sensor.resetSensor();

        int attempts = 20;
        while (co2sensor.readErrorStatus() >> 7 && (attempts > 0))
        {
            Serial.println("Esperando primera medicion...");
            attempts--;
            mqtt::mqttPump(); // keep the MQTT keepalive flowing while we block
            delay(500);
        }
        if (attempts == 0)
        {
            return false; // I2C bus issues
        }
        return true;
    }
}