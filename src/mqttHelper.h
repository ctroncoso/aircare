// mqttHelper.h — MQTT broker connect/publish/subscribe (declarations).
#pragma once

#include "globals.h"
#include "config/schedule.h"  // for sched::parseDate() used in the EXCEPTION command
#include "configHelper.h"     // for cfg::brokerHost / cfg::brokerPort (dynamic broker)
#include "core/events.h"
#include <PubSubClient.h>

namespace mqtt
{
    extern PubSubClient client; // defined in mqttHelper.cpp

    bool initMQTT();
    bool mqttTryReconnect();
    // Returns the current backoff interval (ms) for the periodic reconnect,
    // growing exponentially with each failed attempt and capped. Call
    // mqttResetBackoff() after a successful connect.
    unsigned long mqttBackoffInterval();
    void mqttResetBackoff();
    void mqttPublish(const char *path, const char *content);
    void publishEvent(pub_event event, String param);
    void callback(char *topic, byte *payload, unsigned int length);
}