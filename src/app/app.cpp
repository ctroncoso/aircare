// app.cpp — application-level measurement / update cycles and telemetry.
#include "app/app.h"
#include "mqttHelper.h"  // mqtt::mqttPump() — keep keepalive alive during blocking HTTP fetches

// Shared telemetry buffer (was globals.h doc / serializedString).
JsonDocument doc;
char serializedString[300];

// Periodic timers (were globals.h previousTimer_1 / previousTimer_2).
static unsigned long previousTimer_1 = 0;
static unsigned long previousTimer_2 = 0;

void readValues()
{
    bme::temp = bme::bme.readTemperature();
    bme::pressure = bme::bme.readPressure() / 100.0F;
    bme::altitude = bme::bme.readAltitude(SEALEVELPRESSURE_HPA);
    bme::humidity = bme::bme.readHumidity();
    delay(100);
    sunriseH::co2_fc = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
    sunriseH::co2_uc = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_COMPENSATED);
    sunriseH::co2_fu = sunriseH::co2sensor.readCO2(CO2_FILTERED_UNCOMPENSATED);
    sunriseH::co2_uu = sunriseH::co2sensor.readCO2(CO2_UNFILTERED_UNCOMPENSATED);
}

CO2_Condition getCO2_State(uint16_t co2_level)
{
    if (co2_level < CO2_LOW)
        return CO2_Condition::Green;
    else if (co2_level < CO2_HIGH)
        return CO2_Condition::Yellow;
    else
        return CO2_Condition::Red;
}

void printValues()
{
    doc["t"] = bme::temp;
    doc["p"] = bme::pressure;
    doc["a"] = bme::altitude;
    doc["h"] = bme::humidity;
    doc["co2_fc"] = sunriseH::co2_fc;
    doc["co2_uc"] = sunriseH::co2_uc;
    doc["co2_fu"] = sunriseH::co2_fu;
    doc["co2_uu"] = sunriseH::co2_uu;
    doc["time"] = ntp::getTime();
    doc["uptime"] = millis();
    doc["mac"] = WiFi.macAddress();
    doc["rl1"] = int(relay::state());
    doc["rl2"] = int(relay::state());
    doc["sched_mode"] = sched::modeToString(sched::mode);
    doc["sched_ovr"] = sched::overrideToString(sched::override);
    doc["sched_exc"] = sched::excCount;
    doc["fw"] = PROGRAM_VERSION;
    serializeJson(doc, serializedString);
    Serial.println(serializedString);
    Serial.printf("Current State: %s \r\n", state2string().c_str());
    sched::printNextTransition();
}

String state2string()
{
    switch (getCO2_State(sunriseH::co2_fc))
    {
    case CO2_Condition::Green:
        return "Green";
    case CO2_Condition::Yellow:
        return "Yellow";
    case CO2_Condition::Red:
        return "Red";
    default:
        return "";
    }
}

void measurementTick()
{
    unsigned long currentTime = millis();
    if (currentTime - previousTimer_1 >= measurementDelay)
    {
        previousTimer_1 = currentTime;

        // Relay state is owned by the event-driven sched:: engine and applied
        // via actuators/relay; we only read relay::state() for telemetry.
        readValues();
        co2_State = getCO2_State(sunriseH::co2_fc);
        printValues();
        mqtt::mqttPublish("cleanair/sensor", serializedString);
        leds::setLedOnCO2Condition(co2_State);
    }
}

void updateTick()
{
    unsigned long currentTime = millis();
    if (currentTime - previousTimer_2 >= updateDelay)
    {
        previousTimer_2 = currentTime;
        mqtt::mqttPump();       // keep keepalive alive across the blocking HTTP calls below
        ota::checkUpdate();
        mqtt::mqttPump();
        sched::fetchSchedule(); // re-fetch schedule (falls back to NVS on failure)
        mqtt::mqttPump();
        cfg::fetchConfig();     // re-fetch broker config (emits Evt::BrokerChanged on change)
        mqtt::mqttPump();
    }
}
