#pragma once

#include "globals.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/**
 * @brief Remote schedule + NVS persistence.
 *
 * The four relay schedule times (filter on/off, lunch start/end) are fetched
 * from an online JSON file keyed by the device MAC address (same MAC-matching
 * pattern used by bins/update.json). The fetched values are persisted to NVS
 * (non-volatile memory) so that on reboot, or whenever the server is
 * unreachable, the device falls back to the last known good values (or the
 * compiled-in defaults on a first boot).
 *
 * All branches print [SCHED] debug information to the Serial monitor.
 */

namespace sched
{
    // NVS namespace and keys
    const char *nvsNamespace = "aircare";
    const char *nvsKeyOn     = "filter_on";
    const char *nvsKeyOff    = "filter_off";
    const char *nvsKeyLunchS = "lunch_start";
    const char *nvsKeyLunchE = "lunch_end";

    // Online schedule manifest (mirrors the OTA update.json style).
    const char *scheduleURL = "https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/schedule.json";

    Preferences prefs;

    /// @brief Load schedule values from NVS. Returns true if at least one key existed.
    bool loadFromNVS()
    {
        bool found = false;
        if (prefs.begin(nvsNamespace, true)) // read-only
        {
            if (prefs.isKey(nvsKeyOn))
            {
                FILTER_ON_HHMM   = prefs.getInt(nvsKeyOn);
                FILTER_OFF_HHMM  = prefs.getInt(nvsKeyOff);
                LUNCH_START_HHMM = prefs.getInt(nvsKeyLunchS);
                LUNCH_END_HHMM   = prefs.getInt(nvsKeyLunchE);
                found = true;
                Serial.printf("[SCHED] Loaded from NVS: on=%d off=%d lunch=%d-%d\n",
                              FILTER_ON_HHMM, FILTER_OFF_HHMM, LUNCH_START_HHMM, LUNCH_END_HHMM);
            }
            else
            {
                Serial.println("[SCHED] No NVS schedule found, keeping compiled defaults.");
            }
            prefs.end();
        }
        else
        {
            Serial.println("[SCHED] Failed to open NVS (read).");
        }
        return found;
    }

    /// @brief Persist the current schedule globals to NVS.
    void saveToNVS()
    {
        if (prefs.begin(nvsNamespace, false)) // read-write
        {
            prefs.putInt(nvsKeyOn,     FILTER_ON_HHMM);
            prefs.putInt(nvsKeyOff,    FILTER_OFF_HHMM);
            prefs.putInt(nvsKeyLunchS, LUNCH_START_HHMM);
            prefs.putInt(nvsKeyLunchE, LUNCH_END_HHMM);
            prefs.end();
            Serial.printf("[SCHED] Saved to NVS: on=%d off=%d lunch=%d-%d\n",
                          FILTER_ON_HHMM, FILTER_OFF_HHMM, LUNCH_START_HHMM, LUNCH_END_HHMM);
        }
        else
        {
            Serial.println("[SCHED] Failed to open NVS (write).");
        }
    }

    /// @brief Fetch schedule from the online JSON and apply the entry matching this MAC.
    ///
    /// Order of operations / robustness:
    ///   1. Always load NVS first so we have sane values even without network.
    ///   2. If no WiFi -> keep NVS/defaults.
    ///   3. HTTPS GET the JSON; on any failure -> keep NVS/defaults.
    ///   4. Find the entry whose "Device" == this MAC; on match, update globals + save NVS.
    ///   5. If MAC not present -> keep NVS/defaults.
    void fetchSchedule()
    {
        // 1. Start from persisted / default values.
        loadFromNVS();

        // 2. Network sanity check.
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[SCHED] WiFi not connected — using NVS/default values.");
            return;
        }

        // 3. Fetch the schedule manifest over HTTPS.
        WiFiClientSecure *client = new WiFiClientSecure;
        client->setInsecure(); // no CA bundle verification (matches OTA helper behaviour)

        HTTPClient https;
        https.setConnectTimeout(5000);
        https.setTimeout(5000);

        Serial.printf("[SCHED] Fetching %s\n", scheduleURL);
        if (!https.begin(*client, scheduleURL))
        {
            Serial.println("[SCHED] HTTPS begin failed — using NVS/default values.");
            delete client;
            return;
        }

        int httpCode = https.GET();
        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[SCHED] HTTP GET failed (code %d) — using NVS/default values.\n", httpCode);
            https.end();
            delete client;
            return;
        }

        String payload = https.getString();
        https.end();
        delete client;

        // 4. Parse JSON and look for our MAC.
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            Serial.printf("[SCHED] JSON parse error: %s — using NVS/default values.\n", err.c_str());
            return;
        }

        const char *myMac = WiFi.macAddress().c_str();
        Serial.printf("[SCHED] Looking for Device=%s\n", myMac);

        bool matched = false;
        JsonArray schedules = doc["Schedules"];
        for (JsonObject entry : schedules)
        {
            const char *device = entry["Device"] | "";
            if (strcmp(device, myMac) == 0)
            {
                int on  = entry["FilterOn"]   | FILTER_ON_HHMM;
                int off = entry["FilterOff"]  | FILTER_OFF_HHMM;
                int ls  = entry["LunchStart"] | LUNCH_START_HHMM;
                int le  = entry["LunchEnd"]   | LUNCH_END_HHMM;

                FILTER_ON_HHMM   = on;
                FILTER_OFF_HHMM  = off;
                LUNCH_START_HHMM = ls;
                LUNCH_END_HHMM   = le;

                Serial.printf("[SCHED] Match found. Applied: on=%d off=%d lunch=%d-%d\n",
                              on, off, ls, le);
                saveToNVS();
                matched = true;
                break;
            }
        }

        if (!matched)
        {
            Serial.println("[SCHED] No entry for this MAC — using NVS/default values.");
        }
    }

    /// @brief Convenience init called from setup() (after WiFi/NTP are up).
    void initSchedule()
    {
        Serial.println("--- Initializing schedule");
        fetchSchedule();
        Serial.printf("[SCHED] Active schedule: on=%d off=%d lunch=%d-%d\n",
                      FILTER_ON_HHMM, FILTER_OFF_HHMM, LUNCH_START_HHMM, LUNCH_END_HHMM);
    }
} // namespace sched