#pragma once

#include "globals.h"
#include "scheduleHelper.h"  // for sched::parseDate() used in the EXCEPTION command
#include "configHelper.h"    // for cfg::brokerHost / cfg::brokerPort (dynamic broker)
#include "core/events.h"
#include <PubSubClient.h>

namespace mqtt
{
  PubSubClient client(espClient); // mqtt

  bool initMQTT();
  bool mqttreconnect();
  bool mqttTryReconnect();
  void mqttPublish(const char* path, const char* content);
  void publishEvent(pub_event event, String param);
  void callback(char* topic, byte* payload, unsigned int length);

  bool initMQTT()
  {
    client.setCallback(callback);
    client.setBufferSize(256);  // TODO - mover reinicio de mcu a mqttreconnect 
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
    // Loop hasta lograr conexión. Luego de 12 intentos cada 5 segundos, reiniciar MCU
    u_int8_t attempts = 4;
    while (!client.connected())
    {
      Serial.print("Attempting MQTT connection...");
      // ID de cliente con string random
      String clientId = "aircare-";
      clientId += WiFi.macAddress();
      // (Re)bind to the dynamically resolved broker before each connect attempt.
      client.setServer(cfg::brokerHost, cfg::brokerPort);
      // probar conexión
      if (client.connect(clientId.c_str()))
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


  /// @brief Single non-blocking reconnect attempt for use in the main loop.
  /// Does not loop or delay — returns immediately whether it succeeded or not.
  bool mqttTryReconnect()
  {
    String clientId = "aircare-" + WiFi.macAddress();
    // (Re)bind to the dynamically resolved broker before each connect attempt.
    client.setServer(cfg::brokerHost, cfg::brokerPort);
    if (client.connect(clientId.c_str()))
    {
      Serial.println("MQTT: reconnected.");
      // Re-subscribe after reconnect
      client.subscribe("AirCare/inCommands/broadcast");
      client.subscribe(String("AirCare/inCommands/" + WiFi.macAddress()).c_str());
    }
    else
    {
      Serial.printf("MQTT: reconnect attempt failed, rc=%d\n", client.state());
    }
    return client.connected();
  }


  void mqttPublish(const char* mq_path, const char* content)
  {
    if (!client.connected())
    {
      Serial.println("MQTT: skipping publish, not connected.");
      return;
    }
    client.publish(mq_path, content);
  }

  /// @brief Mqtt connect/reconnect

  void publishEvent(pub_event event, String param){
    JsonDocument doc;
    doc["event"] = event;
    doc["param"] = param;
    doc["mac"] = WiFi.macAddress();
    doc["fw"] = PROGRAM_VERSION ;
    serializeJson(doc, serializedString);

    mqtt::mqttPublish("cleanair/events", serializedString);
  }


  void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");

    char strpayload[length+1];
    memcpy(strpayload, payload, length);
    strpayload[length]=0;
    Serial.println(strpayload);

    // Parse remote commands, e.g. {"cmd":"RELAY","value":"ON|OFF|AUTO"}.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, strpayload);
    if (err)
    {
      Serial.printf("MQTT: callback JSON parse error: %s\n", err.c_str());
      return;
    }

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "RELAY") == 0)
    {
      const char* value = doc["value"] | "";
      if (strcmp(value, "ON") == 0)
      {
        bool on = true;
        events::emit(Evt::RelayOverride, &on); // sched::Override::ON
        publishEvent(INFO, "RELAY|OVERRIDE_ON|Manual override ON via MQTT");
      }
      else if (strcmp(value, "OFF") == 0)
      {
        bool on = false;
        events::emit(Evt::RelayOverride, &on); // sched::Override::OFF
        publishEvent(INFO, "RELAY|OVERRIDE_OFF|Manual override OFF via MQTT");
      }
      else if (strcmp(value, "AUTO") == 0)
      {
        events::emit(Evt::RelayAuto); // sched::Override::NONE
        publishEvent(INFO, "RELAY|AUTO|Override cleared, back to schedule");
      }
      else
      {
        Serial.printf("MQTT: unknown RELAY value '%s'\n", value);
      }
    }
    else if (strcmp(cmd, "EXCEPTION") == 0)
    {
      // {"cmd":"EXCEPTION","from":"2026-07-20","to":"2026-08-10","state":"off"}
      const char* from = doc["from"] | "";
      const char* to   = doc["to"]   | "";
      const char* st   = doc["state"] | "off";
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