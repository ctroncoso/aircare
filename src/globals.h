// globals.h — minimal shared state that is genuinely cross-cutting.
//
// After the restructure, almost everything moved into its own module
// (core/, sensors/, net/, config/, actuators/, app/). What remains here is
// the small amount of state that is shared across subsystems and has no
// natural single owner:
//   - co2_State      : set by app (measurement), read by leds (restore)
//   - espClient      : owned by net/mqtt (PubSubClient binds to it)
//   - wm             : owned by net/wifi (portal + main.cpp restart path)
//   - previousTimer_mqtt : MQTT reconnect gate, used only in main.cpp loop
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include "core/board.h"   // CO2_Condition, PROGRAM_VERSION, ONE_MIN, PORTAL_*
#include "core/events.h"

inline CO2_Condition co2_State = CO2_Condition::Unknown;

inline WiFiClient espClient;
inline WiFiManager wm;

inline unsigned long previousTimer_mqtt = 0; // gate for periodic MQTT reconnect attempts
