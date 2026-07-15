// ntpHelper.cpp — SNTP time sync implementation.
#include "ntpHelper.h"

namespace ntp
{
    const char *ntpServer1 = "ntp.shoa.cl";
    const char *ntpServer2 = "time.nist.gov";

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
        Serial.println("Got time adjustment from NTP!");
        printLocalTime();
        // Notify subscribers (e.g. the scheduler) that local time is (re)synced,
        // so they can re-evaluate their timeline — catches DST changes and the
        // initial sync. Replaces the old `schedNeedsRearm` global handshake.
        events::emit(Evt::NtpSynced);
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