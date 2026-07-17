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
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include "core/board.h"   // CO2_Condition, PROGRAM_VERSION, ONE_MIN, PORTAL_*
#include "core/events.h"

inline CO2_Condition co2_State = CO2_Condition::Unknown;

inline WiFiClientSecure espClient;
inline WiFiManager wm;

inline unsigned long previousTimer_mqtt = 0; // gate for periodic MQTT reconnect attempts

inline unsigned long previousTimer_1 = 0;     // measurement cadence gate (app.cpp)
inline unsigned long previousTimer_2 = 0;     // update/fetch cadence gate (app.cpp)

// Enable loop-stall debug markers ([DBG] >/< ...). Defined here so a single
// switch controls all #ifdef DBG_WDT sites. Turned ON for the OTA-stall
// investigation so we can see which loop() step wedges.
#define DBG_WDT

// Enable I2C-only debug instrumentation (per-read timings, bus-recovery
// notice). Defined separately from DBG_WDT so you can probe just the suspect
// I2C path without the per-cycle loop markers. Uncomment to enable.
#define DBG_I2C

// Enable OTA-step debug markers + the async-OTA hard-timeout abort tracing.
// ON while diagnosing the update-check wedge.
#define DBG_OTA
