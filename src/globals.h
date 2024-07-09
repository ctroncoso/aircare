#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#define SEALEVELPRESSURE_HPA (1013.25)
#define PROGRAM_VERSION    "1.0.2" 

const unsigned long measurementDelay = 60000;
const unsigned long updateDelay = 60000*10; // 10 minute delay between update check


const int ledPinR = 25;
const int ledPinY = 26;
const int ledPinG = 27;

JsonDocument doc;
char serializedString[200];


