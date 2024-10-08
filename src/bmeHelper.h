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

  bool initBME();   


  bool initBME(){
    unsigned status;
    
    status = bme.begin(0x76);
    if (!status) {
      Serial.println("No se encontró un bme280. Verifique cableado.");
      Serial.print("SensorID es: 0x"); Serial.println(bme.sensorID(),16);
      return false;
    }
    bme::bme.setSampling(Adafruit_BME280::MODE_NORMAL,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::SAMPLING_X16,
              Adafruit_BME280::FILTER_OFF   );        
    return true;
  }
}