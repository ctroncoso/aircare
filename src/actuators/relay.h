// relay.h — physical relay/load actuator.
// Owns the relay GPIO state which used to live in globals.h / scheduleHelper.h.
// There are two independent channels (fan = rlPin1, UV = rlPin2). The scheduler
// drives them via relay::set(); telemetry reads relay::state(1/2). Because the
// two channels "usually" work in tandem, convenience helpers that act on BOTH
// channels are also provided (setBoth / bothState / bothOn / bothOff).
#pragma once

#include "core/board.h" // rlPin1, rlPin2

namespace relay
{
    // Which relay channel (1 = fan/rlPin1, 2 = UV/rlPin2).
    enum class Id : uint8_t
    {
        Fan = 1,
        Uv = 2
    };

    // Current logical state of a single channel (true = ON).
    bool state(Id which);

    // Convenience overloads using the integer channel number (1 or 2).
    bool state(int which);

    // Drive a single channel to the requested state, writing the GPIO line only
    // on a real edge change (edge-triggered, no relay chatter).
    void set(Id which, bool on);

    // Convenience overload using the integer channel number (1 or 2).
    void set(int which, bool on);

    // ---- combined helpers (the two channels usually run in tandem) ----
    // Drive BOTH channels to the same state.
    void setBoth(bool on);

    // Returns true only if BOTH channels are ON.
    bool bothOn();

    // Returns true if EITHER channel is ON.
    bool anyOn();

    // Configure the relay pins as outputs and default them to OFF at boot.
    void init();
}