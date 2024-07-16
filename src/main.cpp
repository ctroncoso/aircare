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

  measurementTick();

  // check for updates and install.
  updateTick();

  if ( digitalRead(0) == LOW ) {
    delay(4000);
    if ( digitalRead(0) == LOW ) {
      Serial.println("Portal Triggered...");
      wm.startConfigPortal(PORTAL_NAME);
      ESP.restart();
    }
  }
}
