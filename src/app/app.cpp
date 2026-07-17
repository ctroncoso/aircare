// app.cpp — application-level measurement / update cycles and telemetry.
#include "app/app.h"
#include "core/co2check.h" // co2Valid() — extracted for unit testing
#include "core/board.h"    // PROGRAM_VERSION — emitted in telemetry/report/event JSON
#include "mqttHelper.h"    // mqtt::mqttPump() — keep keepalive alive during blocking HTTP fetches

// Shared telemetry buffer (was globals.h doc / serializedString).
// Size is derived from the actual JSON so serializeJson never truncates
// (the old fixed 300-byte buffer silently corrupted telemetry once all keys
// were present). 512 matches the MQTT PubSubClient buffer; +1 for the NUL.
JsonDocument doc;
char serializedString[512 + 1];

// Periodic timers (previousTimer_1 / previousTimer_2) live in globals.h so
// setup() can initialize them to millis() and defer the first fetch.

// ---- I2C hardening -------------------------------------------------------
// The ESP32 Wire library has NO software timeout, so a sensor that wedges the
// bus (e.g. the Sunrise CO2 module holding SCL low) blocks loop() indefinitely
// — the exact hang the 240s task WDT was catching. We set the I2C *hardware*
// timeout (APB 80MHz ticks) so a stuck transaction aborts in ~2s instead of
// minutes, then detect the failed read and recover the bus so the next cycle
// works. Recovery + a Warning are emitted per sensor (CO2 / BME).
#include <Wire.h>
#include "driver/i2c.h"

static bool g_i2cTimeoutSet = false;
static bool g_i2cHealthy = true;

// Timestamp (ms) of the last *successful* readValues() cycle. An independent
// monitor task watches this so it can detect a wedge even when loop() itself
// is blocked inside the I2C driver (where the in-band millis() check can't run).
static volatile uint32_t g_lastI2cOkMs = 0;

// Largest legal I2C hardware timeout: the timeout register is 16-bit, so the
// max expressible value is 0xFFFF APB ticks (~0.82 ms @ 80 MHz APB). A bus
// that is hard-wedged (SCL held low, no clock edges) aborts fast instead of
// blocking the driver forever. (A 2 s timeout is impossible with this API and
// was being rejected by the driver — i2c_set_timeout "timing value error".)
#define I2C_HW_TIMEOUT_TICKS 0xFFFF

// Out-of-band wedge thresholds (ms). MUST exceed the measurement interval
// (measurementDelay = 60s) so the monitor does NOT false-trigger during the
// normal idle gap between samples. It only fires if a read is this late — i.e.
// loop() is genuinely stuck inside the I2C driver. Recover+Warning at 75s,
// hard reboot at 90s; both well under the 120s task WDT backstop.
#define I2C_WEDGE_WARN_MS   75000   // recover + Warning
#define I2C_WEDGE_REBOOT_MS 90000   // still wedged -> ESP.restart()

// Recover a wedged I2C bus: clock out any stuck slave by toggling SCL, then
// re-init Wire. Default ESP32 dev-module pins are SDA=21 / SCL=22.
static void recoverI2cBus()
{
    // Clock out up to 16 cycles on SCL to free a slave holding the line.
    pinMode(21, OUTPUT_OPEN_DRAIN);
    pinMode(22, OUTPUT_OPEN_DRAIN);
    digitalWrite(21, HIGH);
    for (int i = 0; i < 16; i++)
    {
        digitalWrite(22, LOW);
        delayMicroseconds(5);
        digitalWrite(22, HIGH);
        delayMicroseconds(5);
    }
    Wire.end();
    Wire.begin(21, 22);
    // Wire.begin() resets the pin mode and drops any pull-ups, so re-apply
    // them — without pull-ups the Sunrise repeated-start reads wedge again.
    pinMode(21, INPUT_PULLUP);
    pinMode(22, INPUT_PULLUP);
    // Re-arm the (legal) hardware timeout after re-init.
    i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
    g_i2cTimeoutSet = true;
    g_i2cHealthy = true;
#ifdef DBG_I2C
    Serial.println("[DBG_I2C] bus recovered");
#endif
}

void readValues()
{
    // One-time: arm the (legal) I2C hardware timeout so a stuck transaction
    // can't hang loop() forever. The 240s task WDT remains the backstop.
    if (!g_i2cTimeoutSet)
    {
        i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
        g_i2cTimeoutSet = true;
    }

    unsigned long t0 = millis();

    // --- BME280 (temp / pressure / humidity) ---
    bool bmeOk = true;
#ifdef DBG_I2C
    unsigned long tb = millis();
#endif
    bme::temp = bme::bme.readTemperature();
    bme::pressure = bme::bme.readPressure() / 100.0F;
    bme::altitude = bme::bme.readAltitude(SEALEVELPRESSURE_HPA);
    bme::humidity = bme::bme.readHumidity();
    // NaN/zero across the board indicates a wedged BME read.
    if (isnan(bme::temp) || isnan(bme::pressure) || isnan(bme::humidity) ||
        (bme::temp == 0 && bme::pressure == 0 && bme::humidity == 0))
    {
        bmeOk = false;
    }
#ifdef DBG_I2C
    Serial.printf("[DBG_I2C] BME read %lums ok=%d\n", millis() - tb, bmeOk);
#endif

    delay(100);

    // --- Sunrise CO2 (4 variants) ---
#ifdef DBG_I2C
    unsigned long tc = millis();
#endif
    sunriseH::co2_fc = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
    sunriseH::co2_uc = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_COMPENSATED);
    sunriseH::co2_fu = sunriseH::co2sensor.readCO2(CO2_FILTERED_UNCOMPENSATED);
    sunriseH::co2_uu = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_UNCOMPENSATED);
#ifdef DBG_I2C
    Serial.printf("[DBG_I2C] CO2 read %lums fc=%u\n", millis() - tc, sunriseH::co2_fc);
#endif

    // If the whole read took suspiciously long, the bus was likely wedged
    // (hardware timeout aborted it). Recover and warn.
    bool wedged = (millis() - t0 > 2500);
    if (wedged || !bmeOk || !co2Valid(sunriseH::co2_fc))
    {
        if (g_i2cHealthy) // rate-limit: warn once per degradation
        {
            if (wedged)
                mqtt::publishWarning("I2C|BUS_TIMEOUT|Sensor I2C read exceeded 2s — bus recovered");
            else if (!bmeOk)
                mqtt::publishWarning("BME280|I2C_READ_FAIL|BME280 returned invalid data — bus recovered");
            else
                mqtt::publishWarning("CO2_SENSOR|I2C_READ_FAIL|Sunrise CO2 read invalid — bus recovered");
            g_i2cHealthy = false;
        }
        recoverI2cBus();
    }
    else
    {
        g_i2cHealthy = true; // cleared fault
        g_lastI2cOkMs = millis(); // mark this cycle as healthy for the monitor task
    }
#ifdef DBG_I2C
    Serial.printf("[DBG_I2C] total read %lums wedged=%d bmeOk=%d co2Valid=%d\n",
                  millis() - t0, wedged, bmeOk, co2Valid(sunriseH::co2_fc));
#endif
}

// ----------------------------------------------------------------------------
// Out-of-band I2C wedge monitor (Fix B).
//
// If loop() is blocked *inside* the I2C driver (a hard bus wedge that the
// hardware timeout couldn't abort), the in-band millis() check in readValues()
// never returns, so nothing recovers. This independent task runs on core 0 and
// watches g_lastI2cOkMs, which is only updated on a successful read cycle. If
// no successful read has happened for I2C_WEDGE_WARN_MS it force-recovers the
// bus and emits a Warning; if still wedged past I2C_WEDGE_REBOOT_MS it reboots
// (safely under the 240s task WDT).
// ----------------------------------------------------------------------------
static void i2cMonitorTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;)
    {
        // Seed the baseline once loop() has run at least one cycle.
        if (g_lastI2cOkMs != 0)
        {
            uint32_t stalledFor = millis() - g_lastI2cOkMs;
            if (stalledFor >= I2C_WEDGE_WARN_MS)
            {
                // Rate-limit the Warning + recovery to once per episode.
                static uint32_t lastActionMs = 0;
                if (g_lastI2cOkMs != lastActionMs)
                {
                    mqtt::publishWarning("I2C|BUS_TIMEOUT|No successful I2C read in "
                                         + String(I2C_WEDGE_WARN_MS / 1000)
                                         + "s — bus recovered");
                    lastActionMs = g_lastI2cOkMs;
                    g_i2cHealthy = false;
#ifdef DBG_I2C
                    Serial.printf("[DBG_I2C] monitor: wedged %lums, recovering\n", stalledFor);
#endif
                }
                recoverI2cBus();
                if (stalledFor >= I2C_WEDGE_REBOOT_MS)
                {
#ifdef DBG_I2C
                    Serial.printf("[DBG_I2C] monitor: wedged %lums, rebooting\n", stalledFor);
#endif
                    mqtt::publishEvent(ERROR, "MCU|I2C_WEDGE|Bus stuck >"
                                       + String(I2C_WEDGE_REBOOT_MS / 1000)
                                       + "s — rebooting");
                    delay(2000);
                    ESP.restart();
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000)); // poll once per second
    }
}

void initI2cWatch()
{
    // Ensure the hardware timeout is armed before loop() first reads.
    if (!g_i2cTimeoutSet)
    {
        i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
        g_i2cTimeoutSet = true;
    }
    // Do NOT seed g_lastI2cOkMs here: the first real readValues() only runs
    // after measurementDelay (~60s), so seeding would make the monitor think
    // the bus is wedged and spam recovery long before the first sample. The
    // monitor stays dormant until the first successful read sets the baseline
    // (guarded by `if (g_lastI2cOkMs != 0)`), so it only catches a true wedge
    // that occurs AFTER measurements have started.
    xTaskCreatePinnedToCore(i2cMonitorTask, "i2c_mon", 2048, NULL, 1, NULL, 0);
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
    // Publish the next (up to 2) upcoming exceptions so Grafana can show their
    // date ranges per device without the firmware exposing the internal array.
    sched::ExceptionView excView[sched::MAX_EXC_PUBLISH];
    int excViewCount = 0;
    sched::getExceptionList(excView, &excViewCount);
    if (excViewCount >= 1)
    {
        doc["exc1_from"]  = excView[0].fromDay;
        doc["exc1_to"]    = excView[0].toDay;
        doc["exc1_state"] = excView[0].on ? "ON" : "OFF";
    }
    if (excViewCount >= 2)
    {
        doc["exc2_from"]  = excView[1].fromDay;
        doc["exc2_to"]    = excView[1].toDay;
        doc["exc2_state"] = excView[1].on ? "ON" : "OFF";
    }
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

        // ---- periodic status/health heartbeat (every 5 measurement cycles) ----
        // Replaces the old standalone HEALTH message: the status snapshot
        // (config/state + health) is the liveness signal and is also what the
        // on-demand REPORT command publishes.
        static unsigned long status_last_publish = 0;
        const unsigned long status_interval = 5 * measurementDelay; // ~5 min
        if (millis() - status_last_publish >= status_interval)
        {
            status_last_publish = millis();
            mqtt::publishStatus("STATUS|Device status report");
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
#ifdef DBG_WDT
        unsigned long t = millis();
        Serial.println("[DBG] > ota::checkUpdate");
        ota::checkUpdate();
        Serial.printf("[DBG] < ota::checkUpdate took %lu ms\n", millis() - t);
#else
        ota::checkUpdate();
#endif
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

#ifdef DBG_WDT
        t = millis();
        Serial.println("[DBG] > sched::fetchSchedule");
        sched::fetchSchedule(); // re-fetch schedule (falls back to NVS on failure)
        Serial.printf("[DBG] < sched::fetchSchedule took %lu ms\n", millis() - t);
#else
        sched::fetchSchedule();
#endif
        mqtt::mqttPump();
#ifdef DBG_WDT
        t = millis();
        Serial.println("[DBG] > cfg::fetchConfig");
        cfg::fetchConfig(); // re-fetch broker config (emits Evt::BrokerChanged on change)
        Serial.printf("[DBG] < cfg::fetchConfig took %lu ms\n", millis() - t);
#else
        cfg::fetchConfig();
#endif
        mqtt::mqttPump();
    }
}
