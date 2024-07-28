#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include "bmeHelper.h"
#include "sunriseHelper.h"





void printValues();
void readValues();
CO2_Condition getCO2_State(uint16_t co2_level);
String state2string();
void testRelay();


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

/**
 * @brief Measurement cycle function.
 * 
 * This function is triggered every <measurementDelay>
 * Place all your measuremen and reporting rutines in this block 
 * in the order they are to be executed. 
 */
void measurementTick(){
    unsigned long currentTime = millis();
  if(currentTime - previousTimer_1 >= measurementDelay){
    previousTimer_1 = currentTime;

    readValues();
    co2_State = getCO2_State(sunriseH::co2_fc);
    printValues();
    // ntp::printLocalTime();     // it will take some time to sync time :)
    // Serial.println(ntp::getTime());
    mqtt::mqttPublish();
    leds::setLedOnCO2Condition(co2_State);
  }

}
/**
 * @brief Update cycle funtion.
 * 
 * This function is triggered every <updateDelay>
 * 
 */
void updateTick(){
  unsigned long currentTime = millis();
  if(currentTime - previousTimer_2 >= updateDelay){
    previousTimer_2 = currentTime;
    ota::checkUpdate();
  }
}

void testRelay()
{
  digitalWrite(rlPin1, LOW);
  digitalWrite(rlPin2, LOW);

  delay(10000);

  digitalWrite(rlPin1, HIGH);
  digitalWrite(rlPin2, HIGH);
}