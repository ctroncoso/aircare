// configHelper.cpp — dynamic MQTT broker configuration implementation.
#include "configHelper.h"

namespace cfg
{
    const char *configURL = "https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/config.json";
    const char *FALLBACK_BROKER = "4532efaca6c5432ea63dbae9a7690ef4.s1.eu.hivemq.cloud";
    const int   FALLBACK_PORT   = 8883;

    const char *nvsNamespace = "aircare";
    const char *nvsKeyBroker = "cfg_brk";
    const char *nvsKeyPort   = "cfg_prt";

    char   brokerHost[64] = {0};
    int    brokerPort     = FALLBACK_PORT;

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
        strncpy(brokerHost, FALLBACK_BROKER, sizeof(brokerHost) - 1);
        brokerHost[sizeof(brokerHost) - 1] = '\0';
        brokerPort = FALLBACK_PORT;
        Serial.println("[CFG] No NVS broker found, using compiled fallback.");
    }

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

        saveToNVS();

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
}