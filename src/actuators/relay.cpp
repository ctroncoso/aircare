// relay.cpp — physical relay/load actuator implementation.
#include "relay.h"

namespace relay
{
    bool g_state = false; // false = relay OFF (both channels high)

    bool state()
    {
        return g_state;
    }

    void set(bool on)
    {
        if (on && !g_state)
        {
            digitalWrite(rlPin1, LOW);
            digitalWrite(rlPin2, LOW);
            g_state = true;
            Serial.println("[RELAY] -> ON");
        }
        else if (!on && g_state)
        {
            digitalWrite(rlPin1, HIGH);
            digitalWrite(rlPin2, HIGH);
            g_state = false;
            Serial.println("[RELAY] -> OFF");
        }
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
        g_state = false;
    }
}