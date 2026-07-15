// app.cpp — application-level measurement / update cycles and telemetry.
#include "app/app.h"
#include "core/co2check.h" // co2Valid() — extracted for unit testing
#include "mqttHelper.h"    // mqtt::mqttPump() — keep keepalive alive during blocking HTTP fetches

// Shared telemetry buffer (was globals.h doc / serializedString).
// Size is derived from the actual JSON so serializeJson never truncates
// (the old fixed 300-byte buffer silently corrupted telemetry once all keys
// were present). 512 matches the MQTT PubSubClient buffer; +1 for the NUL.
JsonDocument doc;
char serializedString[512 + 1];

// Periodic timers (were globals.h previousTimer_1 / previousTimer_2).
static unsigned long previousTimer_1 = 0;
static unsigned long previousTimer_2 = 0;

void readValues()
{
    bme::temp = bme::bme.readTemperature();
    bme::pressure = bme::bme.readPressure() / 100.0F;
    bme::altitude = bme::bme.readAltitude(SEALEVELPRESSURE_HPA);
    bme::humidity = bme::bme.readHumidity();
    delay(100);
    sunriseH::co2_fc = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
    sunriseH::co2_uc = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_COMPENSATED);
    sunriseH::co2_fu = sunriseH::co2sensor.readCO2(CO2_FILTERED_UNCOMPENSATED);
    sunriseH::co2_uu = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_UNCOMPENSATED);
}

CO2_Condition getCO2_State(uint16_t co2_level)
{
    if (co2_level < CO2_LOW)
        return CO2_Condition::Green;
    else if (co2_level < CO2_HIGH)
        return CO2_Condition::Yellow;
    else
        return CO2_Condition::Red;
}

void printValues()
{
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
    doc["label"] = cfg::label();
    doc["rl1"] = int(relay::state(1));
    doc["rl2"] = int(relay::state(2));
    doc["sched_mode"] = sched::modeToString(sched::mode);
    doc["sched_ovr"] = sched::overrideToString(sched::override);
    doc["sched_exc"] = sched::excCount;
    doc["fw"] = PROGRAM_VERSION;
    // Bound the write to the buffer size so a future key addition can never
    // silently truncate the published telemetry (serializedString is sized to
    // the MQTT client buffer +1; if doc ever exceeds it we log instead of corrupt).
    size_t written = serializeJson(doc, serializedString, sizeof(serializedString));
    if (written >= sizeof(serializedString))
    {
        Serial.printf("[APP] WARN telemetry truncated (%u bytes, buf %u)\n",
                      (unsigned)written, (unsigned)sizeof(serializedString));
    }
    Serial.println(serializedString);
    Serial.printf("Current State: %s \r\n", state2string().c_str());
    sched::printNextTransition();
}

String state2string()
{
    switch (getCO2_State(sunriseH::co2_fc))
    {
    case CO2_Condition::Green:
        return "Green";
    case CO2_Condition::Yellow:
        return "Yellow";
    case CO2_Condition::Red:
        return "Red";
    default:
        return "";
    }
}

void measurementTick()
{
    unsigned long currentTime = millis();
    if (currentTime - previousTimer_1 >= measurementDelay)
    {
        previousTimer_1 = currentTime;

        // Relay state is owned by the event-driven sched:: engine and applied
        // via actuators/relay; we only read relay::state() for telemetry.
        readValues();

        // A2: validate the CO2 reading before trusting it. If the primary
        // filtered/compensated sample is invalid (or the sensor reports an
        // error), skip the state change + LED update + publish so we never
        // report a misleading "Green" off a stale/zero reading.
        int16_t err = sunriseH::co2sensor.readErrorStatus();
        if (!co2Valid(sunriseH::co2_fc) || (err != 0))
        {
            Serial.printf("[APP] CO2 sample invalid (co2_fc=%u, err=0x%04X) — skipping publish/LED\n",
                          sunriseH::co2_fc, (unsigned int)err);
            return;
        }

        co2_State = getCO2_State(sunriseH::co2_fc);
        printValues();
        mqtt::mqttPublish("cleanair/sensor", serializedString);
        leds::setLedOnCO2Condition(co2_State);

        // ---- D11: periodic health telemetry (every 5 measurement cycles) ----
        static unsigned long health_last_publish = 0;
        const unsigned long health_interval = 5 * measurementDelay; // 5 min
        if (millis() - health_last_publish >= health_interval)
        {
            health_last_publish = millis();
            // Build a tiny health snapshot.
            static char healthBuf[300];
            JsonDocument doc;
            doc["event"] = INFO;
            doc["param"] = "HEALTH|heap=" + String(ESP.getFreeHeap()) + "|rssi=" + String(WiFi.RSSI()) + "|uptime=" + String(millis());
            serializeJson(doc, healthBuf);
            mqtt::mqttPublish("cleanair/events", healthBuf);
        }
    }
}

void updateTick()
{
    unsigned long currentTime = millis();
    if (currentTime - previousTimer_2 >= updateDelay)
    {
        previousTimer_2 = currentTime;
        mqtt::mqttPump(); // keep keepalive alive across the blocking HTTP calls below
        ota::checkUpdate();
        mqtt::mqttPump();

        // The OTA install above sets ota::g_updating and runs exclusively. If an
        // update was just applied it has already rebooted; if it is still in
        // progress (or just finished this cycle), skip the other blocking
        // fetches so the OTA write is never interleaved with another long HTTPS
        // GET or a broker swap — that keeps the remote-update path safe.
        if (ota::g_updating)
        {
            Serial.println("[APP] OTA in progress — skipping schedule/config fetch this cycle.");
            return;
        }

        sched::fetchSchedule(); // re-fetch schedule (falls back to NVS on failure)
        mqtt::mqttPump();
        cfg::fetchConfig(); // re-fetch broker config (emits Evt::BrokerChanged on change)
        mqtt::mqttPump();
    }
}
