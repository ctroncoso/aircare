// otaHelper.h — OTA firmware update check (declarations).
#pragma once

#include <Arduino.h>
#include "ESP32OTAPull.h"
#include "ledHelper.h"

namespace ota
{
    // Set while an OTA download/install is in progress. Lets other periodic
    // work (schedule/config fetches) skip itself so the blocking OTA write
    // is never interleaved with another long HTTPS fetch or a broker swap.
    extern bool g_updating;

    // Set by the out-of-band OTA watchdog task (core 0) when it clean-aborts a
    // stalled download. The watchdog task MUST NOT call the (non-thread-safe)
    // PubSubClient itself, so it only raises this flag; loopTask publishes the
    // Warning on the next cycle (thread-safe) and clears it.
    extern bool g_otaTimedOut;

    // True if a stall/timeout abort happened and the Warning is pending publish.
    bool otaTimedOut();

    // Emit the coalesced OTA-timeout Warning from loopTask (thread-safe) and
    // clear the flag. Call this every loop() while !isUpdating().
    void publishOtaTimeoutWarning();

    // Launch the OTA check asynchronously (in its own task). Returns
    // immediately; loop() keeps running. See otaHelper.cpp for the stall/timeout
    // abort that recovers cleanly instead of wedging until the task WDT.
    bool checkUpdate();

    // Poll every loop() while an OTA is in flight; enforces the hard-timeout
    // ceiling and aborts a stuck download task so the device continues normally.
    // NOTE: this is now a SECONDARY guard — the independent OTA watchdog task
    // (initOtaWatch) is the primary one and works even if loop() is wedged.
    void pump();

    // Start the independent OTA watchdog task. Call once from setup(). It polls
    // the OTA timers every second and clean-aborts a stalled download even when
    // loop() itself is frozen by WiFi/TLS contention.
    void initOtaWatch();

    // True while an OTA check/download is in flight.
    bool isUpdating();
}