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
        set(false); // default OFF (high)
    }
}