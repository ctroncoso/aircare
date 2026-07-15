// app.h — application-level measurement / update cycles and telemetry.
//
// Owns the periodic timers and the shared JSON telemetry buffer that were
// previously scattered in mainHelper.h / globals.h. The relay state is read
// from actuators/relay; the scheduler owns the relay decision.
#pragma once

#include "globals.h"
#include "ntpHelper.h"
#include "bmeHelper.h"
#include "sunriseHelper.h"
#include "mqttHelper.h"
#include "otaHelper.h"
#include "config/schedule.h"
#include "actuators/relay.h"

// Shared telemetry buffer (defined in app.cpp).
extern JsonDocument doc;
extern char serializedString[512 + 1];

void readValues();
void printValues();
CO2_Condition getCO2_State(uint16_t co2_level);
String state2string();

void measurementTick();
void updateTick();
