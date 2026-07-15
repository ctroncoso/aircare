// Arduino.h — minimal host stub for native (host) unit tests.
//
// The firmware sources include <Arduino.h> for type aliases and HAL helpers
// that only exist on the ESP32/Arduino framework. For the native test build we
// provide just enough for the dependency-free modules under test to compile and
// run: the common typedefs, the GPIO/timing HAL used by the relay actuator, and
// a no-op Serial stream.
//
// g_pinLevel[] is defined once in host_hal.cpp (extern here) so every
// translation unit shares the same pin-state table (a `static` here would give
// each .cpp its own copy and break cross-TU assertions).
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>

// Common Arduino typedefs used across the firmware headers.
typedef uint8_t byte;
typedef bool boolean;

#ifndef HIGH
#define HIGH 0x1
#endif
#ifndef LOW
#define LOW 0x0
#endif
#ifndef OUTPUT
#define OUTPUT 0x2
#endif
#ifndef INPUT
#define INPUT 0x0
#endif

// Shared pin-state table (defined in host_hal.cpp).
extern int g_pinLevel[64];

inline void pinMode(int, int) {} // ignore mode on host

inline void digitalWrite(int pin, int level)
{
    if (pin >= 0 && pin < 64)
        g_pinLevel[pin] = level;
}

inline int digitalRead(int pin)
{
    if (pin >= 0 && pin < 64)
        return g_pinLevel[pin];
    return 0;
}

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

// --- Serial stub (no-op stream so firmware print calls compile on host) ---
struct StubSerial
{
    void print(const char *) {}
    void println(const char *) {}
    void printf(const char *, ...) {}
};
static StubSerial Serial;