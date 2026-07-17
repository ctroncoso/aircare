// diag/main.cpp — standalone I2C / sensor diagnostic (offline, serial only).
//
// Reuses the firmware's sensor wrappers (sunrise_i2c / sunriseHelper /
// bmeHelper) to measure real-world behaviour on the protoboard rig:
//   - I2C bus scan (detection)
//   - per-device read timing (BME280 + Sunrise CO2)
//   - forced wedge probe: hold SCL low, then observe whether the 0xFFFF
//     hardware timeout aborts the next read, and how the clock-out recovery
//     behaves (no reset GPIO on this rig, so reboot is the only true cure).
//
// Serial commands (115200):
//   s  -> force a wedge: hold SCL low for 3s, then release
//   r  -> run the clock-out + Wire re-init recovery once
//   t  -> toggle the 0xFFFF hardware timeout on/off
//   h  -> print this help
//
// SDA=21 SCL=22 (ESP32 dev-module defaults).

#include <Arduino.h>
#include <Wire.h>
#include "driver/i2c.h"
#include "sunriseHelper.h"
#include "bmeHelper.h"

static const uint8_t SDA_PIN = 21;
static const uint8_t SCL_PIN = 22;
#define I2C_HW_TIMEOUT_TICKS 0xFFFF   // legal max (~0.82ms @ 80MHz APB)

static bool g_timeoutArmed = true;

// Mirror of the firmware's recovery: clock out SCL, re-init Wire, re-arm
// timeout. Reports whether a subsequent read succeeds.
static void clockOutRecovery()
{
    pinMode(SDA_PIN, OUTPUT_OPEN_DRAIN);
    pinMode(SCL_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(SDA_PIN, HIGH);
    for (int i = 0; i < 16; i++)
    {
        digitalWrite(SCL_PIN, LOW);
        delayMicroseconds(5);
        digitalWrite(SCL_PIN, HIGH);
        delayMicroseconds(5);
    }
    Wire.end();
    Wire.begin(SDA_PIN, SCL_PIN);
    if (g_timeoutArmed)
        i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
    Serial.println("[DIAG] clock-out recovery done (Wire re-init)");
}

// Force a wedge the way a latched Sunrise would: drive SCL low (open-drain)
// for holdMs so any in-flight/next transaction sees a stuck clock.
static void forceWedge(unsigned long holdMs)
{
    Serial.printf("[DIAG] forcing wedge: holding SCL low for %lu ms\n", holdMs);
    pinMode(SCL_PIN, OUTPUT_OPEN_DRAIN);
    digitalWrite(SCL_PIN, LOW);
    unsigned long t0 = millis();
    while (millis() - t0 < holdMs)
        delay(10);
    digitalWrite(SCL_PIN, HIGH);
    pinMode(SCL_PIN, INPUT);  // let Wire drive it again
    Serial.println("[DIAG] wedge released");
}

static void busScan()
{
    Serial.println("[DIAG] I2C bus scan (100kHz)...");
    byte n = 0;
    for (byte addr = 1; addr < 120; addr++)
    {
        Wire.beginTransmission(addr);
        byte err = Wire.endTransmission();
        if (err == 0)
        {
            Serial.printf("  [0x%02X] RESPONDS (err=0)\n", addr);
            n++;
        }
        else if (err == 4)
        {
            Serial.printf("  [0x%02X] UNKNOWN ERROR (bus stuck?)\n", addr);
        }
    }
    Serial.printf("[DIAG] scan done: %u device(s) responded\n", n);
}

static void measureCycle()
{
    Serial.println("---- [DIAG] cycle ----");

    // BME280
    unsigned long tb = millis();
    float t = bme::bme.readTemperature();
    float p = bme::bme.readPressure() / 100.0F;
    float h = bme::bme.readHumidity();
    unsigned long bmeMs = millis() - tb;
    bool bmeBad = isnan(t) || isnan(p) || isnan(h) || (t == 0 && p == 0 && h == 0);
    Serial.printf("  BME280  read=%lums temp=%.2f p=%.2f h=%.2f %s\n",
                  bmeMs, t, p, h, bmeBad ? "BAD" : "ok");

    // Sunrise CO2
    unsigned long tc = millis();
    uint16_t co2 = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
    unsigned long co2Ms = millis() - tc;
    int16_t err = sunriseH::co2sensor.readErrorStatus();
    bool co2Bad = (co2 == 0) || (err != 0);
    Serial.printf("  SUNRISE read=%lums co2=%u err=0x%04X %s\n",
                  co2Ms, co2, (unsigned int)err, co2Bad ? "BAD" : "ok");

    Serial.printf("  total=%lums\n", millis() - tb);
}

// Raw manual Sunrise read with full instrumentation, to isolate WHY the
// library read fails (Error -1). Replicates the library's wakeUp()+read, with
// and without a settle delay after the register-address write.
static void rawSunriseRead(uint8_t reg, bool withWake, bool withDelay)
{
    const uint8_t addr = 0x68;
    if (withWake)
    {
        Wire.beginTransmission(addr);
        uint8_t wu = Wire.endTransmission(true);
        Serial.printf("  [RAW] wakeUp endXfer=%u\n", wu);
    }
    unsigned long t0 = micros();
    Wire.beginTransmission(addr);
    Wire.write(reg);
    uint8_t wr = Wire.endTransmission(false); // repeated start
    if (withDelay)
        delayMicroseconds(1000);
    uint8_t got = Wire.requestFrom(addr, (byte)2);
    unsigned long us = micros() - t0;

    uint16_t val = 0;
    if (got == 2)
    {
        val = ((uint16_t)Wire.read() << 8) | Wire.read();
    }
    Serial.printf("  [RAW] reg=0x%02X wake=%d delay=%d endXfer=%u reqFrom=%u val=%u (%lums)\n",
                  reg, withWake, withDelay, wr, got, val, us);
}

static void rawSplitRead(uint8_t reg)
{
    const uint8_t addr = 0x68;
    // Split transaction: STOP after writing the register address, then a fresh
    // start for the read (no repeated-start). Tests whether the wedge is
    // specifically the combined/repeated-start transaction.
    Wire.beginTransmission(addr);
    Wire.write(reg);
    uint8_t wr = Wire.endTransmission(true); // STOP
    delayMicroseconds(1000);
    Wire.beginTransmission(addr);
    uint8_t got = Wire.requestFrom(addr, (byte)2);
    uint16_t val = 0;
    if (got == 2)
        val = ((uint16_t)Wire.read() << 8) | Wire.read();
    Serial.printf("  [SPLIT] reg=0x%02X endXfer=%u reqFrom=%u val=%u\n", reg, wr, got, val);
}

static void probeSunrise()
{
    Serial.println("[DIAG] probe CO2 (0x06): repeated-start variants");
    rawSunriseRead(0x06, true, false);
    rawSunriseRead(0x06, true, true);
    Serial.println("[DIAG] probe CO2 (0x06): SPLIT (stop+restart) transaction");
    rawSplitRead(0x06);
    Serial.printf("[DIAG] library readCO2(0x06) = %u\n", sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED));
    // Clock-speed test: drop to 50kHz and retry split + repeated-start.
    Wire.setClock(50000);
    Serial.println("[DIAG] switched to 50kHz");
    rawSplitRead(0x06);
    rawSunriseRead(0x06, true, true);
    Wire.setClock(100000);
    Serial.println("[DIAG] back to 100kHz");
}

static void handleCmd()
{
    if (!Serial.available())
        return;
    char c = Serial.read();
    while (Serial.available())
        Serial.read(); // drain
    switch (c)
    {
        case 's': forceWedge(3000); break;
        case 'r': clockOutRecovery(); break;
        case 'p': probeSunrise(); break;
        case 't':
            g_timeoutArmed = !g_timeoutArmed;
            if (g_timeoutArmed)
            {
                i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
                Serial.println("[DIAG] hardware timeout ON (0xFFFF)");
            }
            else
            {
                i2c_set_timeout(I2C_NUM_0, 0);
                Serial.println("[DIAG] hardware timeout OFF (0)");
            }
            break;
        case 'h':
        default:
            Serial.println("[DIAG] commands: s=wedge r=recover t=toggle-timeout p=probe w=pullups b=batch h=help");
            break;
        case 'w':
            // ESP32 internal pull-ups (≈45kΩ) — weak, but may help a protoboard
            // with no external resistors. Real fix is external 1.5–4.7kΩ.
            pinMode(SDA_PIN, INPUT_PULLUP);
            pinMode(SCL_PIN, INPUT_PULLUP);
            Serial.println("[DIAG] ESP32 internal pull-ups ENABLED (weak)");
            break;
        case 'b': {
            // Reliability batch: 20 reads, count success vs wedge. The 0xFFFF
            // hardware timeout bounds each wedge to ~0.8ms so it won't hang.
            int ok = 0, bad = 0;
            unsigned long t0 = millis();
            for (int i = 0; i < 20; i++)
            {
                uint16_t v = sunriseH::co2sensor.readCO2(CO2_FILTERED_COMPENSATED);
                if (v != 0xFFFF && v != 0) ok++;
                else bad++;
            }
            Serial.printf("[DIAG] batch: 20 reads, ok=%d bad=%d in %lums\n", ok, bad, millis() - t0);
            break;
        }
    }
}

void setup()
{
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[DIAG] I2C/Sensor diagnostic starting");

    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);
    pinMode(SDA_PIN, INPUT_PULLUP);   // weak ESP32 pull-ups (protoboard may lack external)
    pinMode(SCL_PIN, INPUT_PULLUP);
    i2c_set_timeout(I2C_NUM_0, I2C_HW_TIMEOUT_TICKS);
    g_timeoutArmed = true;
    Serial.println("[DIAG] Wire @100kHz, internal pull-ups on, hardware timeout armed (0xFFFF)");

    busScan();

    if (!bme::initBME())
        Serial.println("[DIAG] BME init FAILED");
    else
        Serial.println("[DIAG] BME init OK");

    if (!sunriseH::initCo2Sensor())
        Serial.println("[DIAG] SUNRISE init FAILED");
    else
        Serial.println("[DIAG] SUNRISE init OK");

    Serial.println("[DIAG] commands: s=wedge r=recover t=toggle-timeout p=probe w=pullups b=batch h=help");
}

void loop()
{
    handleCmd();
    static unsigned long last = 0;
    if (millis() - last >= 5000)
    {
        last = millis();
        measureCycle();
    }
}
