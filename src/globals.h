#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION    "1.1.7-D" 
//#define DEBUG
#define CO2_LOW (700)
#define CO2_HIGH (800)
#define PORTAL_NAME "AIRCARE"
#define PORTAL_TIMEOUT (180) // Timeout in seconds
#define ONE_MIN (60000) ///   one minte in ms

const unsigned long measurementDelay = ONE_MIN;
const unsigned long updateDelay = ONE_MIN * 10; // 10 minute delay between update check


const int ledPinR = 25;
const int ledPinY = 26;
const int ledPinG = 27;

const int rlPin1 = 32;
const int rlPin2 = 33;

bool rl1State;
bool rl2State;


JsonDocument doc;
char serializedString[300];

enum class CO2_Condition { Green, Yellow, Red, Unknown };
CO2_Condition co2_State=CO2_Condition::Unknown;

WiFiClient espClient;
WiFiManager wm;


unsigned long previousTimer_1 = 0;
unsigned long previousTimer_2 = 0;

enum pub_event{ERROR = -1, INFO=0 };