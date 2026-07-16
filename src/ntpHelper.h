// ntpHelper.h — SNTP time sync (declarations).
#pragma once

#include "globals.h"
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include "ledHelper.h"
#include "core/events.h"

namespace ntp
{
    extern const char *ntpServer1;
    extern const char *ntpServer2;
    extern const char *tzChile;

    // How often lwIP SNTP polls the server for a correction. Set explicitly in
    // initNTP() so the behaviour does not depend on the (silent) framework
    // default — the ESP32 RTC drifts only a few seconds/day, so one hour is
    // ample; this just makes the intent explicit and observable.
    constexpr uint32_t NTP_SYNC_INTERVAL_MS = 3600000UL; // 1 hour

    // If a sync has not succeeded for this long, the loop() watchdog forces a
    // re-sync (covers the rare case where poll mode silently stalls, e.g. after
    // a long network outage). Kept larger than the sync interval.
    constexpr uint32_t NTP_STALE_RESYNC_MS = 3 * NTP_SYNC_INTERVAL_MS; // 3 hours

    unsigned long getTime();
    struct tm getTM();
    void printLocalTime();
    void timeavailable(struct timeval *t);
    bool timeSynced();
    void initNTP();

    // Epoch (seconds) of the last successful NTP sync, and the measured offset
    // (server_time - local_before_sync) in seconds. 0 if never synced.
    time_t lastSyncEpoch();
    long  lastSyncOffsetSec();

    // Seconds since the last successful sync (UINT32_MAX if never synced).
    uint32_t secondsSinceSync();

    // Called from loop(): if SNTP poll mode has gone quiet for too long, kick a
    // fresh sync so the clock cannot drift unchecked across multi-day outages.
    void resyncIfStale();
}