// otaHelper.cpp — OTA firmware update check implementation.
//
// The ESP32OTAPull library performs a single blocking HTTPS GET+write with NO
// internal timeout: if the server stalls mid-download the call never returns,
// wedging loop() until the task WDT resets the chip (seen: "Task watchdog got
// triggered" ~120s into "Checking update"). To recover *cleanly* (no reboot),
// the blocking CheckForOTAUpdate() runs in its own task while loop() keeps
// running; a pump() called every cycle enforces a hard ceiling and aborts the
// stuck task if it overruns, letting the device continue normally.
#include "otaHelper.h"
#include "core/board.h"  // updateURL — centralised manifest URL constant
#include "mqttHelper.h"  // publishEvent / client.connected() — guarded, see below
#include "esp_task_wdt.h" // feed the watchdog during the (slow, blocking) OTA download

namespace ota
{
    bool g_updating = false;

    // --- async OTA state -------------------------------------------------
    static TaskHandle_t g_otaTask = nullptr;
    static unsigned long g_otaStartMs = 0;     // when the OTA task was launched
    static unsigned long g_lastProgressMs = 0; // last download-progress callback
    static bool g_otaLaunched = false;         // a check is in flight
    static int  g_otaResult = 0;               // result once the task returns

    // Hard ceilings (ms). STALL = no progress bytes for this long; HARD = total
    // OTA wall-clock cap. Both well under the 120s task WDT so we abort and
    // continue instead of forcing a watchdog reset.
    #define OTA_STALL_MS   90000
    #define OTA_HARD_MS    100000

    // --- independent OTA watchdog (Fix A) ---------------------------------
    // The old backstop (pump(), called from loop()) assumed loop() keeps
    // cycling while the OTA task downloads. But a stalled OTA download contends
    // the WiFi/TLS stack with the MQTT socket and can FREEZE loop() itself
    // (seen: loopTask tripped the 120s task WDT while the OTA task hung at the
    // manifest fetch). When loop() is wedged, pump() never runs, so the OTA
    // task hung forever. This dedicated task runs on core 0 and watches the OTA
    // timers independently of loop(), aborting a stuck download cleanly even if
    // loop() is dead. Mirrors the out-of-band I2C monitor pattern.
    static void otaWatchdogTask(void *pv)
    {
        (void)pv;
        for (;;)
        {
            if (g_updating && g_otaLaunched && g_otaTask != nullptr)
            {
                unsigned long now = millis();
                bool stalled = (now - g_lastProgressMs >= OTA_STALL_MS);
                bool hard = (now - g_otaStartMs >= OTA_HARD_MS);
                if (stalled || hard)
                {
                    Serial.printf("[OTA] ABORT: %s (start=%lums lastProgress=%lums) — killing task\n",
                                  stalled ? "stalled" : "hard-timeout",
                                  now - g_otaStartMs, now - g_lastProgressMs);
                    mqtt::publishWarning("OTA|TIMEOUT|Firmware download stalled — aborted");
                    vTaskDelete(g_otaTask); // free the wedged download task
                    g_otaTask = nullptr;
                    g_otaLaunched = false;
                    g_updating = false;      // loop() (if alive) resumes schedule/config
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000)); // poll once per second
        }
    }

    void callback(int offset, int totallength)
    {
        g_lastProgressMs = millis();
        Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
        leds::flipLed(ledPinY);
        // The OTA download is a long, blocking, synchronous HTTPS transfer that
        // can take >1 min. Feed the task WDT here so a genuine (progressing)
        // download keeps the device alive, while a *stalled* socket (no progress
        // callbacks) is caught by pump()'s timeout instead of the WDT.
        esp_task_wdt_reset();
    }

    const char *errtext(int code)
    {
        switch (code)
        {
        case ESP32OTAPull::UPDATE_AVAILABLE:
            return "An update is available but wasn't installed";
        case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
            return "No profile matches";
        case ESP32OTAPull::NO_UPDATE_AVAILABLE:
            return "Profile matched, but update not applicable";
        case ESP32OTAPull::UPDATE_OK:
            return "An update was done, but no reboot";
        case ESP32OTAPull::HTTP_FAILED:
            return "HTTP GET failure";
        case ESP32OTAPull::WRITE_ERROR:
            return "Write error";
        case ESP32OTAPull::JSON_PROBLEM:
            return "Invalid JSON";
        case ESP32OTAPull::OTA_UPDATE_FAIL:
            return "Update fail (no OTA partition?)";
        default:
            if (code > 0)
                return "Unexpected HTTP response code";
            break;
        }
        return "Unknown error";
    }

    // Reboot the device, flushing serial and (if connected) an event first.
    // checkUpdate() may be called from setup() before MQTT is up, so the
    // publish is guarded by client.connected().
    void doReboot(const char *reason)
    {
        if (mqtt::client.connected())
        {
            mqtt::publishEvent(INFO, String("MCU|RESTART|") + reason);
            // Pump once so the PUBLISH actually goes out before we reset.
            mqtt::mqttPump();
        }
        leds::blinkLed(ledPinG, 3, false);
        delay(2000);
        ESP.restart();
    }

    // Runs in its own task: performs the (blocking) OTA check/download. On a
    // real install it reboots from inside here; otherwise it records the result
    // and signals completion so pump() can clear g_updating.
    static void otaTask(void *pv)
    {
#ifdef DBG_OTA
        Serial.println("[OTA] task start");
#endif
        g_otaStartMs = millis();
        g_lastProgressMs = millis();

        ESP32OTAPull pull;
        pull.SetCallback(callback);
#ifdef DBG_OTA
        Serial.println("[OTA] CheckForOTAUpdate...");
#endif
        int ret = pull.AllowDowngrades(true)
                       .CheckForOTAUpdate(updateURL, PROGRAM_VERSION);
#ifdef DBG_OTA
        Serial.printf("[OTA] CheckForOTAUpdate returned %d (%s)\n", ret, errtext(ret));
#endif

        g_otaResult = ret;
        bool installed = (ret == ESP32OTAPull::UPDATE_OK ||
                          ret == ESP32OTAPull::UPDATE_AVAILABLE);

        if (installed)
        {
            // New image on the inactive OTA partition + marked bootable; reboot
            // to apply. doReboot() does not return.
            doReboot("Update installed, applying new firmware");
        }

        // No install: signal completion. pump() will clear g_updating.
        g_otaLaunched = false;
        g_otaTask = nullptr;
        vTaskDelete(NULL);
    }

    // Non-blocking: launch the OTA check in a task and return immediately so
    // loop() keeps running. If an OTA is already in flight, do nothing.
    bool checkUpdate()
    {
        if (g_otaLaunched)
        {
#ifdef DBG_OTA
            Serial.println("[OTA] check skipped — already in flight");
#endif
            return false;
        }
        g_otaLaunched = true;
        g_updating = true;
        g_otaStartMs = millis();
        g_lastProgressMs = millis();
        g_otaResult = -99; // in-flight sentinel
#ifdef DBG_OTA
        Serial.println("[OTA] launching task");
#endif
        // Pin the OTA task to core 0 (Fix B) so it never shares core 1 with
        // loopTask; competing TLS sessions (OTA download + MQTT socket) then
        // don't directly preempt the main loop, reducing the chance loop()
        // freezes during a download.
        BaseType_t ok = xTaskCreatePinnedToCore(otaTask, "ota", 8192, NULL, 2, &g_otaTask, 0);
        if (ok != pdPASS)
        {
            // Could not spawn the task: bail out cleanly, no wedge.
            g_otaLaunched = false;
            g_updating = false;
            Serial.println("[OTA] task create FAILED — skipping update");
            return false;
        }
        return false;
    }

    // Called every loop() cycle while an OTA is in flight. Enforces the stall /
    // hard-timeout ceilings and aborts the stuck task cleanly (no WDT reset),
    // then clears g_updating so normal schedule/config fetches resume.
    void pump()
    {
        if (!g_updating)
            return;

        if (g_otaLaunched && g_otaTask != nullptr)
        {
            unsigned long now = millis();
            bool stalled = (now - g_lastProgressMs >= OTA_STALL_MS);
            bool hard = (now - g_otaStartMs >= OTA_HARD_MS);
            if (stalled || hard)
            {
                Serial.printf("[OTA] ABORT: %s (start=%lums lastProgress=%lums) — killing task\n",
                              stalled ? "stalled" : "hard-timeout",
                              now - g_otaStartMs, now - g_lastProgressMs);
                mqtt::publishWarning("OTA|TIMEOUT|Firmware download stalled — aborted");
                vTaskDelete(g_otaTask); // free the wedged download task
                g_otaTask = nullptr;
                g_otaLaunched = false;
                g_updating = false; // loop() continues; schedule/config resume
            }
        }
        else if (!g_otaLaunched && g_updating)
        {
            // Task finished without installing (g_otaLaunched cleared in task).
            g_updating = false;
        }
    }

    bool isUpdating() { return g_updating; }

    // Start the independent OTA watchdog task (Fix A). Call once from setup(),
    // after the first checkUpdate() is safe to launch. The task stays dormant
    // (polls every 1s) until an OTA is actually in flight.
    void initOtaWatch()
    {
        static bool started = false;
        if (started) return;
        xTaskCreatePinnedToCore(otaWatchdogTask, "ota_wdog", 2048, NULL, 1, NULL, 0);
        started = true;
    }
}
