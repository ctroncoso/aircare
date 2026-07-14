// httpFetch.h — shared HTTPS JSON fetch-by-MAC helper.
// Encapsulates the duplicated logic in configHelper.h and scheduleHelper.h:
//   WiFiClientSecure (insecure) -> HTTPClient (5s timeouts) -> HTTP_CODE_OK
//   -> parse JSON -> find the matching "Devices" entry by MAC, else "Default".
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace http
{
    struct FetchResult
    {
        bool ok = false;          // true if a doc was fetched and parsed
        bool matched = false;     // true if a device-specific entry was found
        JsonObject entry;         // the matched device entry, or Default, or null
    };

    // Fetch `url`, parse into `doc`, and resolve the entry for `mac`:
    //   1. first object in the `arrayKey` array whose "Device" == mac
    //   2. otherwise the top-level `defaultKey` object (if non-null)
    // `arrayKey` is the JSON array that holds per-device entries (e.g.
    // "Devices" or "Schedules"). `defaultKey` may be nullptr to skip the
    // default lookup. If WiFi is down or the request fails, ok=false and
    // entry is null. All branches print [HTTP] debug info to Serial.
    FetchResult fetchJsonByMac(const char *url, const char *mac, JsonDocument &doc,
                               const char *arrayKey = "Devices",
                               const char *defaultKey = "Default");
}