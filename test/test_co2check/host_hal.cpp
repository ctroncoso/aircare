// host_hal.cpp — single definition of the shared pin-state table for the
// native test binary (see Arduino.h). One copy per test suite so the
// relay actuator's digitalWrite() and the test assertions read the same array.
#include "Arduino.h"
int g_pinLevel[64] = {0};