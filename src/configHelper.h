#pragma once

#include "globals.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/**
 * @brief Dynamic MQTT broker configuration.
 *
 * Mirrors the relay scheduler's "fetch from remote JSON, persist to NVS,
 * refresh periodically" pattern so the broker endpoint (host + port) can be
 * changed without reflashing every device.
 *
 * Resolution order per device:
 *   1. matching MAC entry in "Devices"
 *   2. the top-level "Default" entry
 *   3. last value persisted in NVS
 *   4. the compiled-in fallback (FALLBACK_BROKER / FALLBACK_PORT)
 *
 * On a successful fetch where the resolved host/port differs from what is
 * currently in use, the global handshake flag `mqttNeedsReconnect` is raised
 * so the main loop disconnects and rebinds to the new broker.
 *
 * All branches print [CFG] debug information to the Serial monitor.
 */

namespace cfg
{
    // ---------- constants ----------
    const char *configURL = "https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/config.json";

    const char *FALLBACK_BROKER = "52.23.110.164";
    const int   FALLBACK_PORT   = 1883;

    // ---------- NVS ----------
    const char *nvsNamespace = "aircare";
    const char *nvsKeyBroker = "cfg_brk"; // null-terminated hostname string
    const char *nvsKeyPort   = "cfg_prt"; // int port

    // ---------- runtime state ----------
    char   brokerHost[64] = {0}; // resolved broker hostname/IP
    int    brokerPort     = FALLBACK_PORT;

    Preferences prefs;

    // ---------- persistence ----------
    void saveToNVS()
    {
        if (prefs.begin(nvsNamespace, false))
        {
            prefs.putString(nvsKeyBroker, String(brokerHost));
            prefs.putInt(nvsKeyPort, brokerPort);
            prefs.end();
            Serial.printf("[CFG] Saved NVS: broker=%s port=%d\n", brokerHost, brokerPort);
        }
        else
        {
            Serial.println("[CFG] Failed to open NVS (write).");
        }
    }

    void loadFromNVS()
    {
        if (prefs.begin(nvsNamespace, true))
        {
            if (prefs.isKey(nvsKeyBroker))
            {
                String b = prefs.getString(nvsKeyBroker, FALLBACK_BROKER);
                strncpy(brokerHost, b.c_str(), sizeof(brokerHost) - 1);
                brokerHost[sizeof(brokerHost) - 1] = '\0';
                brokerPort = prefs.getInt(nvsKeyPort, FALLBACK_PORT);
                prefs.end();
                Serial.printf("[CFG] Loaded NVS: broker=%s port=%d\n", brokerHost, brokerPort);
                return;
            }
            prefs.end();
        }
        else
        {
            Serial.println("[CFG] Failed to open NVS (read).");
        }
        // No persisted value: seed from compiled fallback.
        strncpy(brokerHost, FALLBACK_BROKER, sizeof(brokerHost) - 1);
        brokerHost[sizeof(brokerHost) - 1] = '\0';
        brokerPort = FALLBACK_PORT;
        Serial.println("[CFG] No NVS broker found, using compiled fallback.");
    }

    // ---------- remote config parsing ----------
    /// @brief Apply a broker entry (host + port) from a JSON object.
    /// Returns true if the resolved endpoint changed vs. the current state.
    bool applyEntry(JsonObject entry)
    {
        const char *b = entry["Broker"] | "";
        int         p = entry["Port"]   | FALLBACK_PORT;

        bool changed = false;
        if (strlen(b) > 0 && strcmp(b, brokerHost) != 0)
        {
            strncpy(brokerHost, b, sizeof(brokerHost) - 1);
            brokerHost[sizeof(brokerHost) - 1] = '\0';
            changed = true;
        }
        if (p != brokerPort)
        {
            brokerPort = p;
            changed = true;
        }
        return changed;
    }

    void fetchConfig()
    {
        // Start from NVS/compiled values; only override when the remote
        // actually resolves something for us.
        loadFromNVS();
        char beforeHost[64];
        strncpy(beforeHost, brokerHost, sizeof(beforeHost) - 1);
        beforeHost[sizeof(beforeHost) - 1] = '\0';
        int beforePort = brokerPort;

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[CFG] WiFi not connected — keeping NVS/fallback values.");
            return;
        }

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient https;
        https.setConnectTimeout(5000);
        https.setTimeout(5000);
        if (!https.begin(client, configURL))
        {
            Serial.println("[CFG] HTTPS begin failed — keeping NVS/fallback values.");
            return;
        }

        int httpCode = https.GET();
        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[CFG] HTTP GET failed (code %d) — keeping NVS/fallback values.\n", httpCode);
            https.end();
            return;
        }

        String payload = https.getString();
        https.end();

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            Serial.printf("[CFG] JSON parse error: %s — keeping NVS/fallback values.\n", err.c_str());
            return;
        }

        bool resolved = false;
        const char *myMac = WiFi.macAddress().c_str();
        JsonArray devices = doc["Devices"];
        for (JsonObject entry : devices)
        {
            const char *device = entry["Device"] | "";
            if (strcmp(device, myMac) == 0)
            {
                applyEntry(entry);
                resolved = true;
                Serial.println("[CFG] Matched device-specific broker entry.");
                break;
            }
        }

        if (!resolved)
        {
            JsonObject def = doc["Default"];
            if (!def.isNull())
            {
                applyEntry(def);
                resolved = true;
                Serial.println("[CFG] Using Default broker entry.");
            }
        }

        if (!resolved)
        {
            Serial.println("[CFG] No Default or matching entry — keeping NVS/fallback values.");
        }

        // Persist whatever we ended up with.
        saveToNVS();

        // If the resolved broker differs from what we had at the start of this
        // fetch, signal the main loop to reconnect to the new endpoint.
        if (strcmp(brokerHost, beforeHost) != 0 || brokerPort != beforePort)
        {
            mqttNeedsReconnect = true;
            Serial.printf("[CFG] Broker changed -> %s:%d (reconnect pending)\n", brokerHost, brokerPort);
        }
    }

    void initConfig()
    {
        Serial.println("--- Initializing broker config (dynamic)");
        fetchConfig();
        Serial.printf("[CFG] Active: broker=%s port=%d\n", brokerHost, brokerPort);
    }
} // namespace cfg