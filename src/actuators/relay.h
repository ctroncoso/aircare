// relay.h — physical relay/load actuator.
// Owns the relay GPIO state (rl1State/rl2State) which used to live in
// globals.h / scheduleHelper.h. The scheduler calls relay::set() to drive
// the physical pins; telemetry reads relay::state().
#pragma once

#include "core/board.h" // rlPin1, rlPin2

namespace relay
{
    // Current logical relay state (true = ON).
    bool state();

    // Drive both relay channels to the requested state, only writing the
    // GPIO lines when the state actually changes (edge-triggered).
    void set(bool on);

    // Configure the relay pins as outputs and default them to OFF at boot.
    void init();
}