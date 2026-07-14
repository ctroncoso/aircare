#pragma once

#include "globals.h"
#include "core/nvsStore.h"
#include "core/httpFetch.h"
#include "core/events.h"
#include <WiFi.h>
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

    // ---------- NVS keys ----------
    const char *nvsNamespace = "aircare";
    const char *nvsKeyBroker = "cfg_brk"; // null-terminated hostname string
    const char *nvsKeyPort   = "cfg_prt"; // int port

    // ---------- runtime state ----------
    char   brokerHost[64] = {0}; // resolved broker hostname/IP
    int    brokerPort     = FALLBACK_PORT;

    // ---------- persistence ----------
    void saveToNVS()
    {
        nvs::putString(nvsNamespace, nvsKeyBroker, String(brokerHost));
        nvs::putInt(nvsNamespace, nvsKeyPort, brokerPort);
        Serial.printf("[CFG] Saved NVS: broker=%s port=%d\n", brokerHost, brokerPort);
    }

    void loadFromNVS()
    {
        String b = nvs::getString(nvsNamespace, nvsKeyBroker, FALLBACK_BROKER);
        if (b.length() > 0)
        {
            strncpy(brokerHost, b.c_str(), sizeof(brokerHost) - 1);
            brokerHost[sizeof(brokerHost) - 1] = '\0';
            brokerPort = nvs::getInt(nvsNamespace, nvsKeyPort, FALLBACK_PORT);
            Serial.printf("[CFG] Loaded NVS: broker=%s port=%d\n", brokerHost, brokerPort);
            return;
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

        JsonDocument doc;
        http::FetchResult res = http::fetchJsonByMac(configURL, WiFi.macAddress().c_str(), doc);
        if (!res.ok)
        {
            Serial.println("[CFG] Fetch failed — keeping NVS/fallback values.");
        }
        else if (!res.entry.isNull())
        {
            applyEntry(res.entry);
        }
        else
        {
            Serial.println("[CFG] No Default or matching entry — keeping NVS/fallback values.");
        }

        // Persist whatever we ended up with.
        saveToNVS();

        // If the resolved broker differs from what we had at the start of this
        // fetch, notify the main loop (via the event bus) to reconnect to the
        // new endpoint. Replaces the old `mqttNeedsReconnect` global handshake.
        if (strcmp(brokerHost, beforeHost) != 0 || brokerPort != beforePort)
        {
            events::emit(Evt::BrokerChanged);
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