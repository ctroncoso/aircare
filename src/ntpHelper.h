#pragma once


#include "globals.h"
#include <WiFi.h>
#include "time.h"
#include "esp_sntp.h"
#include "ledHelper.h"

namespace ntp{

    const char* ntpServer1 = "ntp.shoa.cl";
    const char* ntpServer2 = "time.nist.gov";
    const long  gmtOffset_sec = 0;
    const int   daylightOffset_sec = 0;

    unsigned long getTime();
    struct tm getTM();
    void printLocalTime();
    void timeavailable(struct timeval *t);
    void setupNTP();


    void initNTP()
    {
        sntp_set_time_sync_notification_cb( timeavailable );
        sntp_servermode_dhcp(1);    // (optional)
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2);
        //mqtt::publishEvent(INFO, "SNTP|TIME_SET|SNTP server connected. DateTime updated.");
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
