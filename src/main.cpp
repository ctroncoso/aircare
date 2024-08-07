#include "globals.h"
#include "ntpHelper.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "ledHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"
#include "mainHelper.h"
#include "wifiManagerHelper.h"
#include "OneButton.h"

#include "ESP32OTAPull.h"

// threat condition
int alarm_level = 0;
OneButton button(GPIO_NUM_0);

void handleMultiClick() {
  int clicks = button.getNumberClicks();
  switch (clicks)
  {
  case 3:
    Serial.println("starting OTA");
    ota::checkUpdate();
    break;
  default:
    break;
  }
  Serial.println(clicks);

}


void configModeCallback(WiFiManager *wm);
void startWifiPortal();

void setup() {
  Serial.begin(115200);
  while(!Serial);


  leds::initLEDS();  //in globals.h

  pinMode(rlPin1, OUTPUT);
  pinMode(rlPin2, OUTPUT);

  digitalWrite(rlPin1,LOW);
  digitalWrite(rlPin2, LOW);

  delay(10000);

  digitalWrite(rlPin1,HIGH);
  digitalWrite(rlPin2, HIGH);

  co2_State = CO2_Condition::Unknown;
  button.attachDoubleClick(startWifiPortal);
  button.attachMultiClick(handleMultiClick);
  //button.attachMultiClick(ota::checkUpdate,)


  
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
  button.tick();
  unsigned long currentTime = millis();

  measurementTick();

  // check for updates and install.
  updateTick();

}

void startWifiPortal(){
  Serial.println("Portal Triggered...");
  wm.startConfigPortal(PORTAL_NAME);
  ESP.restart();
}

