#pragma once

#include "globals.h"
#include "wifiHelper.h"
#include <PubSubClient.h>

namespace mqtt{
    const char* mqtt_server = "52.23.110.164";

    PubSubClient client(wifi::espClient);  //mqtt 

    void mqttreconnect();
    void mqttPublish();


    void initMQTT(){
        client.setServer(mqtt_server, 1883);
    }

    void mqttPublish(){
        if (!client.connected()) {
            mqttreconnect();
        }
        client.publish("/cleanair/sensor", serializedString);
    }


    /// @brief Mqtt connect/reconnect
    void mqttreconnect() {
    // Loop hasta lograr conexión
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // ID de cliente con string random
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);
        // probar conexión
        if (client.connect(clientId.c_str())) {
        Serial.println("connected");
        } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
        delay(5000);
        }
    }
    }    
}