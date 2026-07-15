// configHelper.h — dynamic MQTT broker configuration (declarations).
//
// Mirrors the relay scheduler's "fetch from remote JSON, persist to NVS,
// refresh periodically" pattern so the broker endpoint (host + port) can be
// changed without reflashing every device.
//
// Resolution order per device:
//   1. matching MAC entry in "Devices"
//   2. the top-level "Default" entry
//   3. last value persisted in NVS
//   4. the compiled-in fallback (FALLBACK_BROKER / FALLBACK_PORT)
//
// The same entry also carries an optional human-friendly "Label" (description)
// resolved and persisted with the same order; its fallback is the device MAC so
// label() is never empty. The label is emitted in telemetry/report/event JSON so
// dashboards can show a name instead of a bare MAC.
//
// On a successful fetch where the resolved host/port differs from what is
// currently in use, the event bus emits Evt::BrokerChanged so the main loop
// disconnects and rebinds to the new endpoint.
#pragma once

#include "globals.h"
#include "core/nvsStore.h"
#include "core/httpFetch.h"
#include "core/events.h"
#include <WiFi.h>
#include <ArduinoJson.h>

namespace cfg
{
    extern const char *configURL;
    extern const char *FALLBACK_BROKER;
    extern const int   FALLBACK_PORT;

    // Max human-friendly label length (chars, excluding NUL).
    static const size_t LABEL_MAX = 64;

    extern const char *nvsNamespace;
    extern const char *nvsKeyBroker;
    extern const char *nvsKeyPort;
    extern const char *nvsKeyLabel;

    extern char   brokerHost[64]; // resolved broker hostname/IP
    extern int    brokerPort;
    extern char   deviceLabel[LABEL_MAX + 1]; // resolved human-friendly label

    void saveToNVS();
    void loadFromNVS();
    bool applyEntry(JsonObject entry);
    void fetchConfig();
    void initConfig();

    // Resolved device label (never empty; falls back to the device MAC).
    const char *label();
}