#pragma once

#include "globals.h"
#include <PubSubClient.h>

namespace mqtt
{
  const char *mqtt_server = "52.23.110.164";

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
    client.setServer(mqtt_server, 1883);
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
    //free(strpayload);
    // for (int i=0;i<length;i++) {
    //   Serial.print((char)payload[i]);
    // }
    // Serial.println();
  }
}