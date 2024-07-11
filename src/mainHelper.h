#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include "wifiHelper.h"
#include "bmeHelper.h"
#include "sunriseHelper.h"





void printValues();
void readValues();
CO2_Condition getCO2_State(uint16_t co2_level);
String state2string();


void readValues(){
    bme::temp     = bme::bme.readTemperature();
    bme::pressure = bme::bme.readPressure() / 100.0F;
    bme::altitude = bme::bme.readAltitude(SEALEVELPRESSURE_HPA);
    bme::humidity = bme::bme.readHumidity();
    delay(100);
    sunriseH::co2_fc = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
    sunriseH::co2_uc = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_COMPENSATED);
    sunriseH::co2_fu = sunriseH::co2sensor.readCO2(CO2_FILTERED_UNCOMPENSATED);
    sunriseH::co2_uu = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_UNCOMPENSATED);
}

CO2_Condition getCO2_State(uint16_t co2_level){
    if      (co2_level < CO2_LOW)     return CO2_Condition::Green;
    else if (co2_level < CO2_HIGH )   return CO2_Condition::Yellow;
    else                              return CO2_Condition::Red;
}

void printValues() {
    doc["t"] = bme::temp;
    doc["p"] = bme::pressure;
    doc["a"] = bme::altitude;
    doc["h"] = bme::humidity;
    doc["co2_fc"] = sunriseH::co2_fc;
    doc["co2_uc"] = sunriseH::co2_uc;
    doc["co2_fu"] = sunriseH::co2_fu;
    doc["co2_uu"] = sunriseH::co2_uu;
    doc["time"] = ntp::getTime();
    doc["mac"] = WiFi.macAddress();
    serializeJson(doc, serializedString);
    Serial.println(serializedString);
    Serial.printf("Current State: %s \r\n", state2string());
}

String state2string(){
  switch (getCO2_State(sunriseH::co2_fc))
  {
  case CO2_Condition::Green:  return "Green";
  case CO2_Condition::Yellow: return "Yellow";
  case CO2_Condition::Red:    return "Red";
  default: return "";
  }
}