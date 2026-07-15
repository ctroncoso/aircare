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
    const char *nvsKeyLabel  = "cfg_lbl";

    char   brokerHost[64] = {0};
    int    brokerPort     = FALLBACK_PORT;
    char   deviceLabel[LABEL_MAX + 1] = {0};

    const char *label()
    {
        return deviceLabel;
    }

    // Copy `src` into deviceLabel, clamped to LABEL_MAX. Empty/null input falls
    // back to the device MAC so the label is never blank.
    static void setLabel(const char *src)
    {
        if (src == nullptr || src[0] == '\0')
        {
            String mac = WiFi.macAddress();
            strncpy(deviceLabel, mac.c_str(), LABEL_MAX);
        }
        else
        {
            strncpy(deviceLabel, src, LABEL_MAX);
        }
        deviceLabel[LABEL_MAX] = '\0';
    }

    void saveToNVS()
    {
        nvs::putString(nvsNamespace, nvsKeyBroker, String(brokerHost));
        nvs::putInt(nvsNamespace, nvsKeyPort, brokerPort);
        nvs::putString(nvsNamespace, nvsKeyLabel, String(deviceLabel));
        Serial.printf("[CFG] Saved NVS: broker=%s port=%d label=%s\n", brokerHost, brokerPort, deviceLabel);
    }

    void loadFromNVS()
    {
        // Label defaults to the MAC; overwritten by NVS/config when available.
        setLabel(nvs::getString(nvsNamespace, nvsKeyLabel, "").c_str());

        String b = nvs::getString(nvsNamespace, nvsKeyBroker, FALLBACK_BROKER);
        if (b.length() > 0)
        {
            strncpy(brokerHost, b.c_str(), sizeof(brokerHost) - 1);
            brokerHost[sizeof(brokerHost) - 1] = '\0';
            brokerPort = nvs::getInt(nvsNamespace, nvsKeyPort, FALLBACK_PORT);
            Serial.printf("[CFG] Loaded NVS: broker=%s port=%d label=%s\n", brokerHost, brokerPort, deviceLabel);
            return;
        }
        strncpy(brokerHost, FALLBACK_BROKER, sizeof(brokerHost) - 1);
        brokerHost[sizeof(brokerHost) - 1] = '\0';
        brokerPort = FALLBACK_PORT;
        Serial.println("[CFG] No NVS broker found, using compiled fallback.");
    }

    // Reject obviously invalid broker endpoints so a bad/malformed config.json
    // push can never point the device at an unreachable broker and permanently
    // sever the remote channel (no commands, no OTA, no reboot). On invalid
    // input we keep the last-good NVS/fallback values and report "no change".
    bool brokerValid(const char *b, int p)
    {
        if (b == nullptr || strlen(b) == 0) return false;
        if (strcmp(b, "0.0.0.0") == 0) return false;
        if (strcmp(b, "0") == 0) return false;
        if (p <= 0 || p > 65535) return false;
        return true;
    }

    bool applyEntry(JsonObject entry)
    {
        bool changed = false;

        // Label is independent of broker validity: apply it first so an entry
        // with only a Label (or a bad broker) still refreshes the description.
        // Absent/empty "Label" falls back to the MAC via setLabel().
        if (entry["Label"].is<const char *>())
        {
            const char *lbl = entry["Label"] | "";
            char newLabel[LABEL_MAX + 1];
            strncpy(newLabel, (lbl[0] ? lbl : WiFi.macAddress().c_str()), LABEL_MAX);
            newLabel[LABEL_MAX] = '\0';
            if (strcmp(newLabel, deviceLabel) != 0)
            {
                setLabel(lbl);
                changed = true;
            }
        }

        const char *b = entry["Broker"] | "";
        int         p = entry["Port"]   | FALLBACK_PORT;

        if (!brokerValid(b, p))
        {
            Serial.printf("[CFG] Ignoring invalid broker entry '%s':%d — keeping current.\n", b, p);
            return changed;
        }

        if (strcmp(b, brokerHost) != 0)
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

        bool changed = false;

        JsonDocument doc;
        http::FetchResult res = http::fetchJsonByMac(configURL, WiFi.macAddress().c_str(), doc);
        if (!res.ok)
        {
            Serial.println("[CFG] Fetch failed — keeping NVS/fallback values.");
        }
        else if (!res.entry.isNull())
        {
            changed = applyEntry(res.entry);
        }
        else
        {
            Serial.println("[CFG] No Default or matching entry — keeping NVS/fallback values.");
        }

        if (changed)
        {
            saveToNVS();
        }

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
        Serial.printf("[CFG] Active: broker=%s port=%d label=%s\n", brokerHost, brokerPort, deviceLabel);
    }
}