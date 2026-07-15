// mqttHelper.cpp — MQTT broker connect/publish/subscribe implementation.
#include "mqttHelper.h"
#include "secrets.h"  // MQTT_USER / MQTT_PASS (gitignored, private)

namespace mqtt
{
    PubSubClient client(espClient); // defined here (single TU)

    bool initMQTT()
    {
        client.setCallback(callback);
        client.setBufferSize(256);
        client.setKeepAlive(15);
        client.setServer(cfg::brokerHost, cfg::brokerPort);
        if (!client.connected())
        {
            mqttreconnect();
        }
        return client.connected();
    }

    bool mqttreconnect()
    {
        u_int8_t attempts = 4;
        while (!client.connected())
        {
            Serial.print("Attempting MQTT connection...");
            String clientId = "aircare-";
            clientId += WiFi.macAddress();
            client.setServer(cfg::brokerHost, cfg::brokerPort);
            if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
            {
                Serial.println("connected");
            }
            else
            {
                Serial.printf("failed, rc=%d, attempts left =%d\n", client.state(), attempts);
                delay(5000);
                attempts--;
            }
            if (attempts == 0)
            {
                Serial.println("MQTT: max reconnect attempts reached. Continuing without MQTT.");
                break;
            }
        }
        return client.connected();
    }

    bool mqttTryReconnect()
    {
        String clientId = "aircare-" + WiFi.macAddress();
        client.setServer(cfg::brokerHost, cfg::brokerPort);
        if (client.connect(clientId.c_str(), MQTT_USER, MQTT_PASS))
        {
            Serial.println("MQTT: reconnected.");
            client.subscribe("AirCare/inCommands/broadcast");
            client.subscribe(String("AirCare/inCommands/" + WiFi.macAddress()).c_str());
        }
        else
        {
            Serial.printf("MQTT: reconnect attempt failed, rc=%d\n", client.state());
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