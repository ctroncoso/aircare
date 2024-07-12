#include "globals.h"
#include "wifiHelper.h"
#include "ntpHelper.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "ledHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"
#include "mainHelper.h"

#include <ArduinoJson.h>
#include <WiFi.h>

#include "ESP32OTAPull.h"

// threat condition
int alarm_level = 0;



unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;




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
    co2_State = getCO2_State(sunriseH::co2_fc);
    printValues();
    // ntp::printLocalTime();     // it will take some time to sync time :)
    // Serial.println(ntp::getTime());
    mqtt::mqttPublish();
    leds::setLedOnCO2Condition(co2_State);
  }

  // check for updates and install.
  if(currentTime - previousTimer_2 >= updateDelay){
    previousTimer_2 = currentTime;
    ota::checkUpdate();
  }
  
}

