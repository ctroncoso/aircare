// co2check.h — standalone CO2 sample validity check (no Arduino deps).
//
// Extracted from app.cpp so the boundary logic is unit-testable in a native
// (host) test environment without pulling in the full Arduino/WiFi stack.
#pragma once

#include <cstdint>

// A valid CO2 sample is in the sensor's measurable range (1..9999 ppm).
// A zero reading usually means the measurement hasn't completed or the sensor
// is not ready; 0xFFFF (or >=10000) is the error/over-range sentinel. Anything
// outside this range must not be trusted for state/LED/publish decisions.
inline bool co2Valid(uint16_t v)
{
    return v >= 1 && v < 10000;
}