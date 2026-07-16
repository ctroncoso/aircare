// ntpHelper.cpp — SNTP time sync implementation.
#include "ntpHelper.h"
#include "mqttHelper.h"  // mqtt::publishEvent — surface sync/offset on the events channel

namespace ntp
{
    const char *ntpServer1 = "ntp.shoa.cl";
    const char *ntpServer2 = "time.nist.gov";

    // Sync bookkeeping so drift is observable and the loop() can force a
    // re-sync if poll mode ever stalls.
    static time_t  g_lastSyncEpoch   = 0;
    static long    g_lastSyncOffset  = 0;

    // Chile local time (America/Santiago) with automatic daylight saving.
    // newlib has no IANA tz database, so we use a POSIX TZ rule:
    //   standard  CLT = UTC-4  (<-04>4)
    //   daylight  CLST = UTC-3  (<-03>)
    //   DST starts 1st Sunday of September at 00:00 (M9.1.6/24)
    //   DST ends   1st Sunday of April    at 00:00 (M4.1.6/24)
    const char *tzChile = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";

    bool timeSynced()
    {
        // SNTP_SYNC_STATUS_COMPLETED is only reported momentarily right after a
        // successful sync, then the status reverts to RESET/IN_PROGRESS even
        // though the system clock stays valid. Gating time-dependent logic on
        // that transient flag makes it read false almost always (e.g. rearm()
        // would never apply the relay). Instead, treat time as synced once the
        // clock has advanced past a sane epoch (2023-01-01). Once SNTP has set
        // the clock at least once, this stays true for the rest of the uptime.
        time_t now = time(nullptr);
        return now > 1672531200; // 2023-01-01T00:00:00Z
    }

    void initNTP()
    {
        sntp_set_time_sync_notification_cb(timeavailable);
        sntp_servermode_dhcp(1);
        // Explicit periodic re-sync (see NTP_SYNC_INTERVAL_MS). Without this the
        // framework default is used silently; setting it documents intent.
        sntp_set_sync_interval(NTP_SYNC_INTERVAL_MS);
        configTime(0, 0, ntpServer1, ntpServer2);
        setenv("TZ", tzChile, 1);
        tzset();
        leds::blinkLed(ledPinY, 3);
        delay(1000);
    }

    unsigned long getTime()
    {
        time_t now;
        time(&now);
        return now;
    }

    struct tm getTM()
    {
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        return timeinfo;
    }

    void timeavailable(struct timeval *t)
    {
        // Offset we just applied: server time minus what the local clock read
        // immediately before the correction. Large values indicate the RTC had
        // drifted (or the device was just powered on with no prior sync).
        long offset = (long)t->tv_sec - (long)time(nullptr);
        g_lastSyncEpoch  = t->tv_sec;
        g_lastSyncOffset = offset;

        Serial.printf("Got time adjustment from NTP! offset=%+lds\n", offset);
        printLocalTime();
        // Observable in Grafana: age since previous sync + applied offset, so
        // drift is visible on the Events panel over time.
        mqtt::publishEvent(INFO, "SNTP|TIME_SET|synced offset=" + String(offset) + "s");
        // Notify subscribers (e.g. the scheduler) that local time is (re)synced,
        // so they can re-evaluate their timeline — catches DST changes and the
        // initial sync. Replaces the old `schedNeedsRearm` global handshake.
        events::emit(Evt::NtpSynced);
    }

    time_t lastSyncEpoch()
    {
        return g_lastSyncEpoch;
    }

    long lastSyncOffsetSec()
    {
        return g_lastSyncOffset;
    }

    uint32_t secondsSinceSync()
    {
        if (g_lastSyncEpoch == 0) return UINT32_MAX;
        time_t now = time(nullptr);
        long diff = (long)now - (long)g_lastSyncEpoch;
        return diff < 0 ? 0 : (uint32_t)diff;
    }

    void resyncIfStale()
    {
        // Throttle: only attempt a forced restart once per sync interval, so we
        // don't hammer sntp_restart() every loop while an outage persists.
        static uint32_t lastRestartAttempt = 0;
        uint32_t since = secondsSinceSync();
        if (since >= NTP_STALE_RESYNC_MS / 1000UL)
        {
            uint32_t nowMs = millis();
            if (nowMs - lastRestartAttempt >= NTP_SYNC_INTERVAL_MS)
            {
                // Poll mode has not corrected the clock for a while (likely a
                // long network outage). Force a fresh sync attempt rather than
                // letting the RTC drift unchecked until the next spontaneous
                // poll.
                sntp_restart();
                lastRestartAttempt = nowMs;
                Serial.println("NTP resync forced (sync stale)");
            }
        }
        else
        {
            lastRestartAttempt = 0; // reset throttle once a sync lands
        }
    }

    void printLocalTime()
    {
        struct tm timeinfo;
        if (!getLocalTime(&timeinfo))
        {
            Serial.println("No time available (yet)");
            return;
        }
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }
}