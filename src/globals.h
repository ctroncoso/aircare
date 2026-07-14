#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION "1.1.13"
// #define DEBUG
#define CO2_LOW (700)
#define CO2_HIGH (800)
#define PORTAL_NAME "AIRCARE"
#define PORTAL_TIMEOUT (180) // Timeout in seconds
#define ONE_MIN (60000)      ///   one minte in ms

const unsigned long measurementDelay = ONE_MIN;
const unsigned long updateDelay = ONE_MIN * 10; // 10 minute delay between update check

// Filter schedule (event-driven) — see src/scheduleHelper.h.
// The relay's desired state is owned by the sched:: engine; these globals only
// mirror the physical relay state for telemetry. The previous FILTER_ON_HHMM /
// FILTER_OFF_HHMM / LUNCH_START_HHMM / LUNCH_END_HHMM globals were replaced by a
// per-device Mode (auto/on/off) + weekly window list + MQTT override.

const int ledPinR = 25;
const int ledPinY = 26;
const int ledPinG = 27;

const int rlPin1 = 32;
const int rlPin2 = 33;

bool rl1State;
bool rl2State;

JsonDocument doc;
char serializedString[300];

enum class CO2_Condition
{
    Green,
    Yellow,
    Red,
    Unknown
};
CO2_Condition co2_State = CO2_Condition::Unknown;

WiFiClient espClient;
WiFiManager wm;

unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;
unsigned long previousTimer_mqtt = 0;   // gate for periodic MQTT reconnect attempts

// Event-driven scheduler handshake flags (set by other modules, consumed by
// sched::tick() so there is no cross-header include coupling):
//   schedNeedsRearm : NTP (re)synced — recompute relay state + timeline.
//   pendingRelayCmd : 0=none, 1=ON, 2=OFF, 3=AUTO (MQTT override request).
//   pendingException / pendingExceptionClear : MQTT date-range exception request.
//   pendingExcFrom/To : inclusive local days-since-epoch of the requested range.
//   pendingExcOn     : desired relay state during the range (true=ON).
volatile bool schedNeedsRearm = false;
volatile int  pendingRelayCmd = 0;
volatile bool pendingException = false;
volatile bool pendingExceptionClear = false;
volatile uint32_t pendingExcFrom = 0;
volatile uint32_t pendingExcTo = 0;
volatile bool pendingExcOn = false;

// Dynamic MQTT broker handshake flag: set by cfg::fetchConfig() when the
// resolved broker host/port changes from what is currently in use, so the main
// loop disconnects and rebinds the client to the new endpoint.
volatile bool mqttNeedsReconnect = false;

enum pub_event
{
    ERROR = -1,
    INFO = 0
};