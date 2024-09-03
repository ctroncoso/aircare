#pragma once

#include "globals.h"
#include <PubSubClient.h>

namespace mqtt{
    const char* mqtt_server = "52.23.110.164";

    PubSubClient client(espClient);  //mqtt 

    void mqttreconnect();
    void mqttPublish();


    void initMQTT(){
        client.setServer(mqtt_server, 1883);
        if (!client.connected()) {
            mqttreconnect();
        }        
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
        u_int8_t attempts = 120;
        while (!client.connected()) {
            Serial.print("Attempting MQTT connection...");
            // ID de cliente con string random
            String clientId = "ESP32Client-";
            clientId += String(random(0xffff), HEX);
            // probar conexión
            if (client.connect(clientId.c_str())) {
                Serial.println("connected");
            } else {
                Serial.printf("failed, rc=%d, attempts left =%d",client.state(), attempts);
                delay(5000);
                attempts--;
            }
            if (attempts == 0) {
                for (int i = 0; i < 3; i++) {
                    leds::blinkLed(ledPinR,2);
                    delay(500);
                }
                ESP.restart();
            }
        }
    }    
}