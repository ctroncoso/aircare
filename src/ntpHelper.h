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

    unsigned long getTime();
    struct tm getTM();
    void printLocalTime();
    void timeavailable(struct timeval *t);
    bool timeSynced();
    void initNTP();
}