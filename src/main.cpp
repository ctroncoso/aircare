#include "globals.h"
#include "ntpHelper.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "ledHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"
#include "mainHelper.h"
#include "wifiManagerHelper.h"


#include "ESP32OTAPull.h"

// threat condition
int alarm_level = 0;



unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;


void configModeCallback(WiFiManager *wm);

void setup() {
  Serial.begin(115200);
  while(!Serial);

  leds::initLEDS();  //in globals.h
  co2_State = CO2_Condition::Unknown;
  pinMode(0, INPUT_PULLUP);

  
  Serial.printf("Board: %s \n", ARDUINO_BOARD);
  Serial.printf("Version: %s \n", PROGRAM_VERSION);
  Serial.print("MAC: ");
  Serial.println( String(WiFi.macAddress()));

  wifiM::initWifiM();


  ntp::initNTP();
  sunriseH::initCo2Sensor();
  bme::initBME();
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

  if ( digitalRead(0) == LOW ) {
    delay(4000);
    if ( digitalRead(0) == LOW ) {
      Serial.println("Portal Triggered...");
      wm.startConfigPortal(PORTAL_NAME);
      ESP.restart();
    }
  }
}
