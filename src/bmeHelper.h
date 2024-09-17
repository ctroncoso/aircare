#pragma once

#include "globals.h"
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

namespace bme{
  // BME 
  Adafruit_BME280 bme; // I2C
  float temp;
  float pressure;
  float altitude; 
  float humidity; 

  void initBME();   


  void initBME(){
    unsigned status;
    
    status = bme.begin(0x76);
    if (!status) {
      Serial.println("No se encontró un bme280. Verifique cableado.");
      Serial.print("SensorID es: 0x"); Serial.println(bme.sensorID(),16);
      while (1) delay(10); //TODO Change delay to mqtt alert, wait and reboot.
    }
    bme::bme.setSampling(Adafruit_BME280::MODE_NORMAL,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::FILTER_OFF   );        
    mqtt::publishEvent(INFO, "BME280|INIT_OK|BME280 found and initialized.");
    leds::blinkLed(ledPinY,5);
    delay(1000);
  }
}