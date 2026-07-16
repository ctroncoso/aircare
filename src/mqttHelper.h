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
    // Subscribe to the inbound command topics (broadcast + this device's MAC)
    // and publish a confirmation to the events channel so it is verifiable that
    // the device is actually listening for remote commands. Call this on every
    // (re)connect — both the boot path and the runtime reconnect path.
    void subscribeCommands();
    void mqttLoop();
    void mqttPump();
    // Returns the current backoff interval (ms) for the periodic reconnect,
    // growing exponentially with each failed attempt and capped. Call
    // mqttResetBackoff() after a successful connect.
    unsigned long mqttBackoffInterval();
    void mqttResetBackoff();
    void mqttPublish(const char *path, const char *content);
    void publishEvent(pub_event event, String param);
    // Publish a status snapshot (config/state + health) to cleanair/status.
    // Periodic health heartbeat and the on-demand REPORT command both use it.
    void publishStatus(const char *param);
    void callback(char *topic, byte *payload, unsigned int length);
}