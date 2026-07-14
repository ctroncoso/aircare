#pragma once


#include "globals.h"
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include "ledHelper.h"
#include "core/events.h"

namespace ntp{

    const char* ntpServer1 = "ntp.shoa.cl";
    const char* ntpServer2 = "time.nist.gov";

    // Chile local time (America/Santiago) with automatic daylight saving.
    // newlib has no IANA tz database, so we use a POSIX TZ rule:
    //   standard  CLT = UTC-4  (<-04>4)
    //   daylight  CLST = UTC-3  (<-03>)
    //   DST starts 1st Sunday of September at 00:00 (M9.1.6/24)
    //   DST ends   1st Sunday of April    at 00:00 (M4.1.6/24)
    const char* tzChile = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";

    unsigned long getTime();
    struct tm getTM();
    void printLocalTime();
    void timeavailable(struct timeval *t);
    void setupNTP();

    /// @brief True once SNTP has completed at least one successful sync.
    /// Uses the authoritative esp_sntp sync status rather than guessing from
    /// the epoch value, so callers (e.g. the scheduler) only act on real time.
    bool timeSynced()
    {
        return sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED;
    }


    void initNTP()
    {
        sntp_set_time_sync_notification_cb( timeavailable );
        sntp_servermode_dhcp(1);    // (optional)
        // gmtOffset/daylightOffset are 0; the timezone (incl. DST) is applied
        // via the POSIX TZ rule below so getLocalTime() returns Chile local time.
        configTime(0, 0, ntpServer1, ntpServer2);
        setenv("TZ", tzChile, 1);
        tzset();
        leds::blinkLed(ledPinY,3);
        delay(1000);
    }

    unsigned long getTime() {
        time_t now;
        time(&now);
        return now;
    }

    struct tm getTM(){
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
        if(!getLocalTime(&timeinfo)){
            Serial.println("No time available (yet)");
            return;
        }
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }


}
