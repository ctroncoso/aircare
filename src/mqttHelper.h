#pragma once

#include "globals.h"
#include <PubSubClient.h>

namespace mqtt
{
  const char *mqtt_server = "52.23.110.164";

  PubSubClient client(espClient); // mqtt

  void initMQTT();
  void mqttreconnect();
  void mqttPublish(const char* path, const char* content);
  void publishEvent(pub_event event, String param);
  void callback(char* topic, byte* payload, unsigned int length);

  void initMQTT()
  {
    client.setCallback(callback);
    client.setKeepAlive(15);
    client.setServer(mqtt_server, 1883);
    if (!client.connected())
    {
      mqttreconnect();
    }
    publishEvent(INFO, "MQTT|CONNECTED|MQTT conection established."); 
    leds::blinkLed(ledPinY,2);
    delay(1000);
  }

  void mqttPublish(const char* mq_path, const char* content)
  {
    if (!client.connected())
    {
      mqttreconnect();
    }
    client.publish(mq_path, content);
  }

  /// @brief Mqtt connect/reconnect
  void mqttreconnect()
  {
    // Loop hasta lograr conexión
    u_int8_t attempts = 120;
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
        client.subscribe("AirCare/inTopic");  
      }
      else
      {
        Serial.printf("failed, rc=%d, attempts left =%d\n", client.state(), attempts);
        delay(5000);
        attempts--;
      }
      if (attempts == 0)
      {
        for (int i = 0; i < 3; i++)
        {
          leds::blinkLed(ledPinR, 10);
          delay(500);
        }
        ESP.restart();
      }
    }
  }

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