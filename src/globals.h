#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION    "1.2.2" 
#define CO2_LOW (700)
#define CO2_HIGH (800)
#define PORTAL_NAME "AIRCARE"
#define PORTAL_TIMEOUT (180) // Timeout in seconds

const unsigned long measurementDelay = 60000;
const unsigned long updateDelay = 60000*10; // 10 minute delay between update check


const int ledPinR = 25;
const int ledPinY = 26;
const int ledPinG = 27;

JsonDocument doc;
char serializedString[200];

enum class CO2_Condition { Green, Yellow, Red, Unknown };
CO2_Condition co2_State=CO2_Condition::Unknown;

WiFiClient espClient;
WiFiManager wm;


unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;