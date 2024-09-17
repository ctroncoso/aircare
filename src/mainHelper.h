#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include "bmeHelper.h"
#include "sunriseHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"





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
    if      (co2_level < CO2_LOW  )   return CO2_Condition::Green;
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
    doc["uptime"] = millis();
    doc["mac"] = WiFi.macAddress();
    doc["rl1"] = int(rl1State);
    doc["rl2"] = int(rl2State);
    doc["fw"] = PROGRAM_VERSION ;
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


    //-------------- Relay Activation section ----------------
    bool filterState;

    // check if day is mon-fri between 8am and 6:30pm (time in UTC-3)
    struct tm dt = ntp::getTM();
    //int mday = dt.tm_mday;
    int dow = dt.tm_wday;
    int hour = dt.tm_hour;
    int minute = dt.tm_min;
    int hourmin = hour*100+minute;

    Serial.print("DOW is: "); Serial.println(dow);
    Serial.print("Hourmin is:"); Serial.println(hourmin);


    if (dow >=1 && dow <=5 && hourmin >= 1100 && hourmin < 2130)
    {    
      filterState = true;
    } else {
      filterState = false;
    }

    if (dt.tm_mon == 8 && dt.tm_mday >= 18 && dt.tm_mday <= 20)
    {
      Serial.println("Exception: relay will be off today");
      filterState = filterState && false;
    }
    

    if (filterState)
    {
      Serial.println("fan and UV ACTIVATED");
      digitalWrite(rlPin1,LOW);  // turn on fan
      digitalWrite(rlPin2,LOW);  // turn on UV
      rl1State=1;
      rl2State=1;
    } else {
      Serial.println("fan and UV DEACTIVATED");
      digitalWrite(rlPin1,HIGH);  // turn off fan
      digitalWrite(rlPin2,HIGH);  // turn off UV
      rl1State=0;
      rl2State=0;
    }
    //----------------------------------

    readValues();
    co2_State = getCO2_State(sunriseH::co2_fc);
    printValues();
    // ntp::printLocalTime();     // it will take some time to sync time :)
    // Serial.println(ntp::getTime());
    mqtt::mqttPublish("/cleanair/sensor", serializedString); // serializedString

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