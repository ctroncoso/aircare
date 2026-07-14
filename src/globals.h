#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION "1.1.12"
// #define DEBUG
#define CO2_LOW (700)
#define CO2_HIGH (800)
#define PORTAL_NAME "AIRCARE"
#define PORTAL_TIMEOUT (180) // Timeout in seconds
#define ONE_MIN (60000)      ///   one minte in ms

const unsigned long measurementDelay = ONE_MIN;
const unsigned long updateDelay = ONE_MIN * 10; // 10 minute delay between update check

// Filter schedule — LOCAL Chile time (America/Santiago, DST-aware via POSIX TZ).
// These are now mutable globals so they can be overwritten at runtime from the
// online schedule JSON (matched by MAC) and persisted to NVS. The values below
// act as the compiled-in defaults / fallback when no NVS entry or server is available.
int FILTER_ON_HHMM   = 830;    // 08:30 local (no leading zero — leading 0 = octal!)
int FILTER_OFF_HHMM  = 1830;  // 18:30 local
int LUNCH_START_HHMM = 1230;  // 12:30 local (filter paused)
int LUNCH_END_HHMM   = 1430;  // 14:30 local

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

enum pub_event
{
    ERROR = -1,
    INFO = 0
};