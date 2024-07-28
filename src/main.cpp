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

void handleMultiClick()
{
  int clicks = button.getNumberClicks();
  switch (clicks)
  {
  case 3:
    Serial.println("starting OTA");
    ota::checkUpdate();
    break;
  default:
    Serial.println(clicks);
    break;
  }
}

void configModeCallback(WiFiManager *wm);
void startWifiPortal();

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  
  #ifdef DEBUG
    Serial.println("--- DEBUG ENABLED");
  #endif

  co2_State = CO2_Condition::Unknown;
  button.attachDoubleClick(startWifiPortal);
  button.attachMultiClick(handleMultiClick);
  // button.attachMultiClick(ota::checkUpdate,)

  Serial.printf("Board: %s \n", ARDUINO_BOARD);
  Serial.printf("Version: %s \n", PROGRAM_VERSION);
  Serial.print("MAC: ");
  Serial.println(String(WiFi.macAddress()));

  wifiM::initWifiM();               // Start Wifi Manager. Attempt to connect or run local AP configuration mode.
  ntp::initNTP();                   // Sinchronize time and date
  sunriseH::initCo2Sensor();        // Connect and initialize CO2 sensor
  bme::initBME();                   // Connect and initialize Temp/Presure/Humidity sensor
  mqtt::initMQTT();                 // Initialize connection to MQTT Broker.
  leds::initLEDS(); // in globals.h

  pinMode(rlPin1, OUTPUT);
  pinMode(rlPin2, OUTPUT);


  testRelay();


}

void loop()
{
  unsigned long currentTime = millis();

  button.tick();
  measurementTick();
  updateTick(); // check for updates and install.
}

void startWifiPortal()
{
  Serial.println("Portal Triggered...");
  wm.startConfigPortal(PORTAL_NAME);
  ESP.restart();
}
