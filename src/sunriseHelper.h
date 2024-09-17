#pragma once

#include "globals.h"
#include <Wire.h>
#include "sunrise_i2c.h"

namespace sunriseH {
  
  sunrise co2sensor;
  
  uint16_t co2_fc;
  uint16_t co2_uc;
  uint16_t co2_fu;
  uint16_t co2_uu;
  

  void initCo2Sensor(){
    co2sensor.initSunrise();
    co2sensor.setMeasurementPeriod(measurementDelay/1000);
    co2sensor.setNbrSamples(16);
    co2sensor.setMeasurementMode(CONTINOUS);
    co2sensor.resetSensor();
    
    while(co2sensor.readErrorStatus() >> 7){
      Serial.println("Esperando primera medicion...");  
      delay(500);
    }
    // leds::blinkLed(ledPinY,4);
    // delay(1000);    
  }
}
