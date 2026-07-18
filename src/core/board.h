// board.h — hardware pins, build constants, and shared enums.
// Replaces the old monolithic globals.h "god header".
#pragma once

#include <Arduino.h>

// ---- build / version ----
#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION "1.1.21"
// #define DEBUG
#define CO2_LOW (700)
#define CO2_HIGH (800)
#define PORTAL_NAME "AIRCARE"
#define PORTAL_TIMEOUT (180) // Timeout in seconds
#define ONE_MIN (60000)      ///   one minute in ms

inline const unsigned long measurementDelay = ONE_MIN;
inline const unsigned long updateDelay = ONE_MIN * 10; // 10 minute delay between update check

// ---- GPIO pins ----
inline const int ledPinR = 25;
inline const int ledPinY = 26;
inline const int ledPinG = 27;

inline const int rlPin1 = 32;
inline const int rlPin2 = 33;

// ---- Remote manifest URLs (served from the GitHub repo) ----
// Centralised so a branch/staging switch doesn't require code edits scattered
// across schedule.cpp and otaHelper.cpp.
#define REMOTE_BASE_URL "https://raw.githubusercontent.com/ctroncoso/aircare/main/bins"
inline const char *scheduleURL = REMOTE_BASE_URL "/schedule.json";
inline const char *updateURL = REMOTE_BASE_URL "/update.json";

// ---- CO2 condition state machine ----
enum class CO2_Condition
{
    Green,
    Yellow,
    Red,
    Unknown
};

// ---- MQTT event severity ----
enum pub_event
{
    ERROR = -1,
    INFO = 0,
    WARNING = 1
};