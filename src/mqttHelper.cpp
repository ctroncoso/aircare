// mqttHelper.cpp — MQTT broker connect/publish/subscribe implementation.
#include "mqttHelper.h"
#include "secrets.h"  // MQTT_USER / MQTT_PASS (gitignored, private)

namespace mqtt
{
    PubSubClient client(espClient); // defined here (single TU)

    // Print the exact parameters used for the broker connection so configuration
    // mismatches (host/port/user) are visible in the serial log.
    void logConnectionParams(const char *clientId)
    {
        Serial.printf("[MQTT] host=%s port=%d user=%s clientId=%s tls=insecure\n",
                      cfg::brokerHost, cfg::brokerPort, MQTT_USER, clientId);
    }

    // Prepare the secure client for a fresh connection. After a disconnect the
    // WiFiClientSecure socket/TLS state can be left stale, which causes the next
    // connect() to time out (rc=-1) instead of starting a new handshake. Always
    // stop the client and re-apply setInsecure() before reconnecting.
    void prepareSecureClient()
    {
        espClient.stop();
        espClient.setInsecure();
    }

    // Exponential backoff state for the periodic reconnect in loop(). HiveMQ
    // Cloud rate-limits connection attempts; hammering it right after a reset
    // (or after an abrupt disconnect) makes the broker stop answering the TLS
    // handshake (rc=-1/-4). We therefore make a single attempt at boot and
    // back off with jitter in the loop so retries spread out over time.
    static uint8_t g_backoffStep = 0;
    static const unsigned long BACKOFF_BASE_MS = 30000;   // 30 s
    static const unsigned long BACKOFF_MAX_MS  = 600000;  // 10 min cap

    unsigned long mqttBackoffInterval()
    {
        // 30s, 60s, 120s, 240s, 480s, 600s(cap) ... with +/-25% jitter.
        unsigned long raw = BACKOFF_BASE_MS * (1UL << min(g_backoffStep, (uint8_t)4));
        if (raw > BACKOFF_MAX_MS) raw = BACKOFF_MAX_MS;
        long jitter = (long)(raw * 0.25 * ((float)random(0, 100) / 100.0 - 0.5) * 2);
        unsigned long interval = raw + jitter;
        return interval;
    }

    void mqttResetBackoff()
    {
        g_backoffStep = 0;
    }

    bool initMQTT()
    {
        // The broker (HiveMQ Cloud) requires TLS on port 8883. Configure the
        // secure client before binding PubSubClient. setInsecure() disables
        // certificate verification — consistent with the HTTPS config fetch in
        // httpFetch.cpp. For production, replace with setCACert() + the
        // broker's root CA.
        prepareSecureClient();

        client.setCallback(callback);
        // Must be large enough for the /cleanair/sensor telemetry packet
        // (topic + ~300-byte serializedString + MQTT header). 256 was too
        // small, causing publish() to silently drop measurements.
        client.setBufferSize(512);
        client.setKeepAlive(15);
        client.setServer(cfg::brokerHost, cfg::brokerPort);

        // Retry at boot with exponential backoff so the startup events aren't
        // lost. HiveMQ Cloud throttles rapid reconnects, so we space attempts
        // out instead of hammering. Bounded so we don't block forever — if it
        // still fails we continue without MQTT.
        const uint8_t BOOT_MAX_ATTEMPTS = 5;
        for (uint8_t attempt = 1; attempt <= BOOT_MAX_ATTEMPTS && !client.connected(); attempt++)
        {
            String clientId = "aircare-";
            clientId += WiFi.macAddress();
            Serial.printf("Attempting MQTT connection (boot %d/%d)...", attempt, BOOT_MAX_ATTEMPTS);
            logConnectionParams(clientId.c_str());
            prepareSecureClient();
            client.setServer(cfg::brokerHost, cfg::brokerPort);
            if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
            {
                Serial.println("connected");
                break;
            }
            Serial.printf("failed, rc=%d\n", client.state());
            if (attempt < BOOT_MAX_ATTEMPTS)
            {
                unsigned long wait = mqttBackoffInterval();
                mqttResetBackoff();  // keep each boot wait at the base interval
                Serial.printf("MQTT: boot retry in %lu ms\n", wait);
                delay(wait);
            }
        }
        mqttResetBackoff();
        return client.connected();
    }

    bool mqttTryReconnect()
    {
        String clientId = "aircare-" + WiFi.macAddress();
        logConnectionParams(clientId.c_str());
        prepareSecureClient();
        client.setServer(cfg::brokerHost, cfg::brokerPort);
        if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
        {
            Serial.println("MQTT: reconnected.");
            mqttResetBackoff();
            client.subscribe("AirCare/inCommands/broadcast");
            client.subscribe(String("AirCare/inCommands/" + WiFi.macAddress()).c_str());
        }
        else
        {
            g_backoffStep++;
            Serial.printf("MQTT: reconnect attempt failed, rc=%d (next in %lu ms)\n",
                          client.state(), mqttBackoffInterval());
        }
        return client.connected();
    }

    void mqttPublish(const char *mq_path, const char *content)
    {
        if (!client.connected())
        {
            Serial.println("MQTT: skipping publish, not connected.");
            return;
        }
        client.publish(mq_path, content);
    }

    void publishEvent(pub_event event, String param)
    {
        // Local buffer: events no longer share the telemetry buffer
        // (app.cpp's serializedString), removing cross-module coupling.
        static char eventBuf[300];
        JsonDocument doc;
        doc["event"] = event;
        doc["param"] = param;
        doc["mac"] = WiFi.macAddress();
        doc["fw"] = PROGRAM_VERSION;
        serializeJson(doc, eventBuf);
        mqtt::mqttPublish("cleanair/events", eventBuf);
    }

    void callback(char *topic, byte *payload, unsigned int length)
    {
        Serial.print("Message arrived [");
        Serial.print(topic);
        Serial.print("] ");

        char strpayload[length + 1];
        memcpy(strpayload, payload, length);
        strpayload[length] = 0;
        Serial.println(strpayload);

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, strpayload);
        if (err)
        {
            Serial.printf("MQTT: callback JSON parse error: %s\n", err.c_str());
            return;
        }

        const char *cmd = doc["cmd"] | "";
        if (strcmp(cmd, "RELAY") == 0)
        {
            const char *value = doc["value"] | "";
            if (strcmp(value, "ON") == 0)
            {
                bool on = true;
                events::emit(Evt::RelayOverride, &on);
                publishEvent(INFO, "RELAY|OVERRIDE_ON|Manual override ON via MQTT");
            }
            else if (strcmp(value, "OFF") == 0)
            {
                bool on = false;
                events::emit(Evt::RelayOverride, &on);
                publishEvent(INFO, "RELAY|OVERRIDE_OFF|Manual override OFF via MQTT");
            }
            else if (strcmp(value, "AUTO") == 0)
            {
                events::emit(Evt::RelayAuto);
                publishEvent(INFO, "RELAY|AUTO|Override cleared, back to schedule");
            }
            else
            {
                Serial.printf("MQTT: unknown RELAY value '%s'\n", value);
            }
        }
        else if (strcmp(cmd, "EXCEPTION") == 0)
        {
            const char *from = doc["from"] | "";
            const char *to = doc["to"] | "";
            const char *st = doc["state"] | "off";
            uint32_t fd = 0, td = 0;
            if (sched::parseDate(from, fd) && sched::parseDate(to, td))
            {
                ExceptionReq req{fd, td, (strcmp(st, "on") == 0)};
                events::emit(Evt::ExceptionSet, &req);
                publishEvent(INFO, String("EXCEPTION|SET|") + st + " " + from + ".." + to);
            }
            else
            {
                Serial.printf("MQTT: bad EXCEPTION dates '%s'..'%s'\n", from, to);
            }
        }
        else if (strcmp(cmd, "EXCEPTION_CLEAR") == 0)
        {
            events::emit(Evt::ExceptionClear);
            publishEvent(INFO, "EXCEPTION|CLEAR|All exceptions cleared via MQTT");
        }
        else
        {
            Serial.printf("MQTT: unknown cmd '%s'\n", cmd);
        }
    }
}