// httpFetch.cpp — HTTPS fetch-by-MAC implementation.
#include "httpFetch.h"

namespace http
{
    FetchResult fetchJsonByMac(const char *url, const char *mac, JsonDocument &doc,
                               const char *arrayKey, const char *defaultKey)
    {
        FetchResult res;

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("[HTTP] WiFi not connected — skipping fetch.");
            return res;
        }

        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient https;
        https.setConnectTimeout(5000);
        https.setTimeout(5000);

        if (!https.begin(client, url))
        {
            Serial.println("[HTTP] HTTPS begin failed — keeping current values.");
            return res;
        }

        int httpCode = https.GET();
        if (httpCode != HTTP_CODE_OK)
        {
            Serial.printf("[HTTP] GET failed (code %d) — keeping current values.\n", httpCode);
            https.end();
            return res;
        }

        String payload = https.getString();
        https.end();

        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            Serial.printf("[HTTP] JSON parse error: %s — keeping current values.\n", err.c_str());
            return res;
        }

        res.ok = true;

        JsonArray devices = doc[arrayKey];
        if (devices.size() > 0)
        {
            for (JsonObject entry : devices)
            {
                const char *device = entry["Device"] | "";
                if (strcmp(device, mac) == 0)
                {
                    res.entry = entry;
                    res.matched = true;
                    Serial.println("[HTTP] Matched device-specific entry.");
                    return res;
                }
            }
        }

        if (defaultKey != nullptr)
        {
            JsonObject def = doc[defaultKey];
            if (!def.isNull())
            {
                res.entry = def;
                Serial.println("[HTTP] Using default entry.");
                return res;
            }
        }

        Serial.println("[HTTP] No default or matching entry found.");
        return res;
    }
}