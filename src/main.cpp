#include "globals.h"
#include "wifiHelper.h"
#include "ntpHelper.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "ledHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "ESP32OTAPull.h"

// threat condition
int alarm_level = 0;



unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;

void printValues();
void readValues();


void setup() {
  Serial.begin(115200);
  while(!Serial);
  Serial.printf("Board: %s \n", ARDUINO_BOARD);
  Serial.printf("Version: %s \n", PROGRAM_VERSION);

  wifi::printMac();


  ntp::initNTP();
  sunriseH::initCo2Sensor();
  bme::initBME();
  leds::initLEDS();  //in globals.h
  wifi::initWifi();
  mqtt::initMQTT();
    

}

void loop() { 
  unsigned long currentTime = millis();

  // take and publish measurements
  if(currentTime - previousTimer_1 >= measurementDelay){
    previousTimer_1 = currentTime;

    readValues();
    printValues();
    // ntp::printLocalTime();     // it will take some time to sync time :)
    // Serial.println(ntp::getTime());
    mqtt::mqttPublish();
    leds::trigger_leds(sunriseH::co2_fc);
  }

  // check for updates and install.
  if(currentTime - previousTimer_2 >= updateDelay){
    previousTimer_2 = currentTime;
    ota::checkUpdate();
  }
  
}



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
}