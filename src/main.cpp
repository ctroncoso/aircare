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
#include "esp_task_wdt.h"

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
      mqtt::forceDisconnect(); // thread-safe: signals the MQTT task to drop the socket
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

  // Move all MQTT socket I/O into its own pinned task + watchdog so loopTask
  // can never be wedged by a blocking TLS call (the cause of the repeated
  // task-WDT resets). From here loopTask publishes only via mqttPublish().
  mqtt::startMqttTask();

  

                 

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

  // Defer the first updateTick() so it does not fire on the very first loop()
  // call (previousTimer_2 starts at 0). This avoids a redundant second
  // ota::checkUpdate() immediately after the one run during setup(), and
  // spreads the first periodic fetch to updateDelay after boot.
  previousTimer_2 = millis();

  // Task Watchdog (2 min) — last-resort recovery from a wedged loop(). The OTA
  // library does blocking, unbounded HTTPS (no transfer timeout) and a stalled
  // TLS/manifest GET can hang loop() forever (seen: MCU froze at "Checking
  // update"). The 2 min window is fed by the per-loop esp_task_wdt_reset() and
  // by OTA's progress callback during the download (see otaHelper.cpp), while
  // still catching a true stall. Armed LAST so the intentional pre-restart
  // waits above never trip it. The out-of-band I2C monitor task is the primary
  // fast recovery (10s recover / 30s reboot); this WDT is the backstop.
  if (esp_task_wdt_init(120, true) == ESP_OK)
  {
    esp_task_wdt_add(NULL); // NULL == current (loop) task
    Serial.println("[WDT] Task watchdog armed (120s).");
  }
  else
  {
    Serial.println("[WDT] WARNING: task watchdog init failed.");
  }

  // Arm the I2C hardware timeout and start the out-of-band wedge monitor task
  // (catches a bus wedge that blocks loop() inside the I2C driver).
  initI2cWatch();

  // Start the independent OTA watchdog task (Fix A): aborts a stalled download
  // cleanly even if loop() is itself frozen by OTA/MQTT TLS contention.
  ota::initOtaWatch();
}

void loop()
{
  unsigned long currentTime = millis();

  // Feed the task WDT every loop(): it now only fires on a *genuine* stall
  // (loop() blocked inside the I2C driver or an OTA download we don't feed),
  // not on a merely slow-but-progressing cycle. OTA's progress callback also
  // feeds it during the download.
  esp_task_wdt_reset();
  button.tick();
  ntp::resyncIfStale();   // force a fresh NTP sync if poll mode has gone quiet (async, non-blocking)
  // Lightweight liveness heartbeat (every 10s) — proves loop() is still
  // cycling without flooding the console. DBG_WDT step markers live INSIDE
  // the timed blocks (see app.cpp), so they only print when the work runs.
  {
    static unsigned long lastHb = 0;
    if (currentTime - lastHb >= 10000)
    {
      lastHb = currentTime;
      Serial.printf("[HB] t=%lu free=%u\n", currentTime, esp_get_free_heap_size());
    }
  }
  measurementTick();
  sched::tick();   // event-driven relay scheduler: fire transitions at their edges
  updateTick();

  //-------------MQTT
  // mqttLoop() pumps the keepalive (PINGREQ) and inbound messages every cycle,
  // and reconnects on a fixed 15s cadence if the link drops. No separate thread
  // is used — PubSubClient is not thread-safe; the main loop is the standard
  // ESP32 MQTT pattern.
  // While an OTA is in flight we do NOT touch the MQTT socket at all (Fix B+):
  // the MQTT task (mqttTask) owns the broker socket on core 0, fully separate
  // from loopTask, so loopTask can never be wedged by a blocking TLS call. The
  // OTA window is <=100s (watchdog abort) vs a 30s keepalive, so a brief
  // keepalive gap is harmless; the MQTT task reconnects the moment g_updating
  // clears. The independent OTA watchdog still aborts a stuck download task
  // itself — and raises a flag that the MQTT task publishes next cycle.
  ota::publishOtaTimeoutWarning(); // no-op unless the OTA watchdog flagged a stall
  //-----------------

  // "Communication dead" liveness check. This used to force an autonomous
  // reboot after 60 min with no successful publish — but that produced the
  // hourly Watchdog resets (the mqttTask's own watchdog already recovers a
  // wedged/stuck TLS session without a reboot). We now only surface the
  // condition and nudge the MQTT task to tear+rebuild its socket, so the
  // device stays up. A real, unrecoverable comms loss is better handled by
  // the mqttWatchdogTask recreating the task than by a full reset.
  if (mqtt::commsIsDead())
  {
    static unsigned long lastCommsDeadWarn = 0;
    if (millis() - lastCommsDeadWarn >= 600000UL) // warn at most every 10 min
    {
      lastCommsDeadWarn = millis();
      mqtt::publishEvent(WARNING, "MCU|COMMS_DEAD|No successful publish in 60 min — nudging MQTT socket");
      mqtt::forceDisconnect(); // signal mqttTask to rebuild the TLS session
    }
  }
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