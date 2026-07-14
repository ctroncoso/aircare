#pragma once

#include <ArduinoJson.h>
#include <WiFiManager.h>

// board.h owns pins, build constants, CO2_Condition, and pub_event.
#include "core/board.h"
// events.h is the event bus that replaces the volatile handshake globals below.
#include "core/events.h"

// Filter schedule (event-driven) — see src/scheduleHelper.h.
// The relay's desired state is owned by the sched:: engine; these globals only
// mirror the physical relay state for telemetry. The previous FILTER_ON_HHMM /
// FILTER_OFF_HHMM / LUNCH_START_HHMM / LUNCH_END_HHMM globals were replaced by a
// per-device Mode (auto/on/off) + weekly window list + MQTT override.

bool rl1State;
bool rl2State;

JsonDocument doc;
char serializedString[300];

CO2_Condition co2_State = CO2_Condition::Unknown;

WiFiClient espClient;
WiFiManager wm;

unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;
unsigned long previousTimer_mqtt = 0;   // gate for periodic MQTT reconnect attempts

// Cross-module triggers (NTP re-sync, MQTT override/exception, broker swap)
// now flow through the event bus in core/events.h instead of the old
// volatile handshake flags, which have been removed.
