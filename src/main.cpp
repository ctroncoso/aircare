#include "globals.h"
#include "ledHelper.h"
#include "wifiManagerHelper.h"
#include "mqttHelper.h"
#include "mainHelper.h"
#include "ntpHelper.h"
#include "time.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "otaHelper.h"
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

  Serial.printf("Board: %s \n", ARDUINO_BOARD);
  Serial.printf("Version: %s \n", PROGRAM_VERSION);
  Serial.print("MAC: ");
  Serial.println(String(WiFi.macAddress()));


/**
 * Initializacion everything
 * 
 */
  leds::initLEDS();                 // in globals.h
  int wifi_level;

  // Start Wifi Manager. Attempt to connect or run local AP configuration mode.
  if (!wifiM::initWifiM())
  {
    delay(180000);
    ESP.restart();
  }
  else
  {
    wifi_level = WiFi.RSSI();
  }

  // Initialize connection to MQTT Broker.
  if (mqtt::initMQTT())
  {
    mqtt::publishEvent(INFO, "BOOT|" + String(esp_reset_reason()) + "|Boot with reason");
    mqtt::publishEvent(INFO, "WIFI_RSSI|"+String(wifi_level)+"|WiFi connection strength.");
    mqtt::publishEvent(INFO, "MQTT|CONNECTED|MQTT conection established.");
    mqtt::client.subscribe("AirCare/inCommands/broadcast");
    mqtt::client.subscribe(String("AirCare/inCommands/"+WiFi.macAddress()).c_str());
    mqtt::publishEvent(INFO, "MQTT_SUSCRIBE|AirCare/inCommands/broadcast|MQTT suscribed to broadcast.");
    mqtt::publishEvent(INFO, "MQTT_SUSCRIBE|AirCare/inCommands/"+WiFi.macAddress()+"|MQTT suscribed to own mac.");

    leds::blinkLed(ledPinY, 2);
    delay(1000);
  }

  

  // Check for updates immediately
  if (!ota::checkUpdate())
  {
    mqtt::publishEvent(INFO, "UPDT|NOTFound|Update checked. None found."); 
  }
                 
  ntp::initNTP();                   // Sinchronize time and date

  // Connect and initialize CO2 sensor
  if (sunriseH::initCo2Sensor())
  {
    mqtt::publishEvent(INFO, "CO2_SENSOR|INIT_OK|CO2 sensor Initialized OK"); 
  }
  else 
  {
    mqtt::publishEvent(ERROR, "CO2_SENSOR|I2C_COMM_SUNRISE|CO2 sensor not responding"); 
    delay(180000);
    mqtt::publishEvent(INFO, "MCU|RESTART|Restarting MCU"); 
    delay(10000);
    ESP.restart();
  } 
  
  // Connect and initialize Temp/Presure/Humidity sensor
  if (bme::initBME())
  {
    mqtt::publishEvent(INFO, "BME280|INIT_OK|BME280 found and initialized.");
    leds::blinkLed(ledPinY,5);
    delay(1000);

  }
  else
  {
    mqtt::publishEvent(ERROR, "BME280|I2C_COMM_BME280|BME280 sensor not responding"); 
    delay(180000);
    mqtt::publishEvent(INFO, "MCU|RESTART|Restarting MCU"); 
    delay(10000);
    ESP.restart();
  }   
  

  

  
  // set relay pins to output
  pinMode(rlPin1, OUTPUT);
  pinMode(rlPin2, OUTPUT);
  digitalWrite(rlPin1,HIGH);  // turn off fan
  digitalWrite(rlPin2,HIGH);  // turn off UV


  // testRelay();
  // leds::blinkLed(ledPinG,3);
  // delay(1000);

  mqtt::publishEvent(INFO, "SETUP|OK|Setup finished successfully.");
}

void loop()
{
  unsigned long currentTime = millis();


  button.tick();
  measurementTick();
  updateTick(); // check for updates and install.
  mqtt::client.loop();
}

void startWifiPortal()
{
  Serial.println("Portal Triggered...");
  wm.startConfigPortal(PORTAL_NAME);
  ESP.restart();
}
