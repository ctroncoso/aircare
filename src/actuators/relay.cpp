// relay.cpp — physical relay/load actuator implementation.
#include "relay.h"

namespace relay
{
    // Per-channel logical state (true = ON). Index 0 unused; [1]=fan, [2]=uv.
    bool g_state[3] = {false, false, false};

    // Map a channel id/number to its GPIO pin.
    static int pinFor(int which)
    {
        return (which == 2) ? rlPin2 : rlPin1; // default to relay 1
    }

    bool state(Id which)
    {
        return g_state[(int)which];
    }

    bool state(int which)
    {
        if (which < 1 || which > 2) which = 1;
        return g_state[which];
    }

    void set(Id which, bool on)
    {
        int idx = (int)which;
        if (idx < 1 || idx > 2) return;
        if (on && !g_state[idx])
        {
            digitalWrite(pinFor(idx), LOW); // active-low relay module
            g_state[idx] = true;
            Serial.printf("[RELAY %d] -> ON\n", idx);
        }
        else if (!on && g_state[idx])
        {
            digitalWrite(pinFor(idx), HIGH);
            g_state[idx] = false;
            Serial.printf("[RELAY %d] -> OFF\n", idx);
        }
    }

    void set(int which, bool on)
    {
        set((Id)which, on);
    }

    void setBoth(bool on)
    {
        set(Id::Fan, on);
        set(Id::Uv, on);
    }

    bool bothOn()
    {
        return g_state[1] && g_state[2];
    }

    bool anyOn()
    {
        return g_state[1] || g_state[2];
    }

    void init()
    {
        pinMode(rlPin1, OUTPUT);
        pinMode(rlPin2, OUTPUT);
        // Drive the OFF level explicitly. g_state already defaults to false, so
        // the edge-triggered set(false) below would be a no-op and leave the
        // pins at their power-on level (LOW on ESP32). For an active-low relay
        // module that means the relay would stay energized ON at boot even
        // though the scheduler/logical state reports OFF. Force HIGH so the
        // relay is genuinely de-energized out of reset.
        digitalWrite(rlPin1, HIGH);
        digitalWrite(rlPin2, HIGH);
        g_state[1] = false;
        g_state[2] = false;
    }
}