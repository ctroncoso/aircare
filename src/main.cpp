#include "globals.h"
#include "ledHelper.h"
#include "wifiManagerHelper.h"
#include "mqttHelper.h"
#include "ntpHelper.h"
#include "time.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"
#include "otaHelper.h"
#include "OneButton.h"
#include "app/app.h"
#include "config/schedule.h"
#include "configHelper.h"   // dynamic MQTT broker (cfg::)
#include "actuators/relay.h"
#include "core/events.h"

#include "ESP32OTAPull.h"

OneButton button(GPIO_NUM_0);


void handleMultiClick();
void startWifiPortal();

// Event-bus handler: when the dynamic broker endpoint changes, force an MQTT
// disconnect so the next reconnect attempt binds to the new host/port. This
// replaces the old `mqttNeedsReconnect` global handshake polled in loop().
void brokerChangedHandler(Evt evt, void *ctx)
{
  if (evt == Evt::BrokerChanged)
  {
    Serial.printf("[CFG] Broker swap -> disconnecting from %s:%d\n",
                  cfg::brokerHost, cfg::brokerPort);
    mqtt::client.disconnect();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(100);
  
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
  leds::POSTBlinks();

    // Configure relay/load pins as outputs and default them OFF, BEFORE the
    // scheduler runs so the first applyRelay() at boot drives real hardware.
    relay::init();

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

  
  // Sinchronize time and date
  ntp::initNTP();  

  // Load/fetch the relay schedule (online JSON matched by MAC, persisted to NVS).
  sched::initSchedule();



  // Check for updates immediately (runs before MQTT — no MQTT calls inside)
  ota::checkUpdate();

  

  // Subscribe to the event bus (broker-swap handler) before resolving the
  // dynamic broker config so any BrokerChanged event is handled.
  events::subscribe(brokerChangedHandler);

  // Resolve the (dynamic) MQTT broker host/port from remote config + NVS
  // BEFORE connecting, so initMQTT() binds to the correct endpoint.
  cfg::initConfig();

  // Map the ESP reset reason to a short, human-readable token so the Events
  // panel can show "Power-on" / "Software reboot" instead of a numeric code.
  auto bootReasonString = []() -> const char *
  {
    switch (esp_reset_reason())
    {
      case ESP_RST_POWERON:   return "POWERON";
      case ESP_RST_SW:        return "SW";
      case ESP_RST_PANIC:     return "PANIC";
      case ESP_RST_TASK_WDT:  return "TASK_WDT";
      case ESP_RST_WDT:       return "WDT";
      case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
      case ESP_RST_BROWNOUT:  return "BROWNOUT";
      default:                return "UNKNOWN";
    }
  };

  // Initialize connection to MQTT Broker.
  if (mqtt::initMQTT())
  {
    mqtt::publishEvent(INFO, String("BOOT|") + bootReasonString() + "|Boot with reason");
    mqtt::publishEvent(INFO, "WIFI_RSSI|"+String(wifi_level)+"|WiFi connection strength.");
    mqtt::publishEvent(INFO, "MQTT|CONNECTED|MQTT conection established.");
    mqtt::publishEvent(INFO, "SNTP|TIME_SET|SNTP server connected. DateTime updated.");
    mqtt::publishEvent(INFO, "UPDT|NOTFound|Update checked. None found.");
    // Subscribe to the command channels and confirm on the events channel so it
    // is verifiable the device is listening for remote commands (RELAY,
    // EXCEPTION, REBOOT, UPDATE). Uses the same helper as the runtime
    // reconnect path so the two can't drift apart.
    mqtt::subscribeCommands();

    leds::blinkLed(ledPinY, 2);
    delay(1000);
  }
  else
  {
    Serial.println("MQTT unavailable. Continuing setup without broker.");
  }

  

                 

  // Connect and initialize CO2 sensor
  if (!sunriseH::initCo2Sensor())
  {
    Serial.println("Sensor Sunrise no responde.");
    mqtt::publishEvent(ERROR, "CO2_SENSOR|I2C_COMM_SUNRISE|CO2 sensor not responding");
    // Keep the MQTT keepalive alive during the long pre-restart wait so the
    // error event is delivered and the broker doesn't drop the socket.
    for (int i = 0; i < 36; i++) { mqtt::mqttPump(); delay(5000); }
    mqtt::publishEvent(INFO, "MCU|RESTART|Restarting MCU");
    delay(10000);
    ESP.restart();
  }


  // Connect and initialize Temp/Presure/Humidity sensor
  if (!bme::initBME())
  {
    mqtt::publishEvent(ERROR, "BME280|I2C_COMM_BME280|BME280 sensor not responding");
    for (int i = 0; i < 36; i++) { mqtt::mqttPump(); delay(5000); }
    mqtt::publishEvent(INFO, "MCU|RESTART|Restarting MCU");
    delay(10000);
    ESP.restart();
  }
  
  
  mqtt::publishEvent(INFO, "SETUP|OK|Setup finished successfully.");
}

void loop()
{
  unsigned long currentTime = millis();


  button.tick();
  measurementTick();
  sched::tick();   // event-driven relay scheduler: fire transitions at their edges
  updateTick(); // check for updates and install.

  //-------------MQTT
  // mqttLoop() pumps the keepalive (PINGREQ) and inbound messages every cycle,
  // and reconnects on a fixed 15s cadence if the link drops. No separate thread
  // is used — PubSubClient is not thread-safe; the main loop is the standard
  // ESP32 MQTT pattern.
  mqtt::mqttLoop();
  //-----------------
}


void startWifiPortal()
{
  Serial.println("Portal Triggered...");
  wm.startConfigPortal(PORTAL_NAME);
  ESP.restart();
}


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