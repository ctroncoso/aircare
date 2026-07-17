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
    // Tear down the MQTT socket unconditionally (used after an OTA window, when
    // the TLS session has been idle/stale for the whole download, so the next
    // mqttLoop() reconnects cleanly instead of risking a blocking read).
    void forceDisconnect();
    // Returns the current backoff interval (ms) for the periodic reconnect,
    // growing exponentially with each failed attempt and capped. Call
    // mqttResetBackoff() after a successful connect.
    unsigned long mqttBackoffInterval();
    void mqttResetBackoff();
    void mqttPublish(const char *path, const char *content);
    // "Communication dead" watchdog: true if no publish has succeeded for
    // COMMS_DEAD_MS while WiFi is still associated. Used by the main loop to
    // force an autonomous reboot that rebuilds a wedged TLS/socket state.
    bool commsIsDead();
    void publishEvent(pub_event event, String param);
    // Publish a "Warning" severity message to the dedicated aircare/event topic
    // (separate from cleanair/events). Used for recoverable hardware faults
    // such as an I2C sensor read timeout/bus recovery.
    void publishWarning(String param);
    // Publish a status snapshot (config/state + health) to cleanair/status.
    // Periodic health heartbeat and the on-demand REPORT command both use it.
    void publishStatus(const char *param);
    void callback(char *topic, byte *payload, unsigned int length);
}