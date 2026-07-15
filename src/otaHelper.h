// otaHelper.h — OTA firmware update check (declarations).
#pragma once

#include <Arduino.h>
#include "ESP32OTAPull.h"
#include "ledHelper.h"

namespace ota
{
    // Set while an OTA download/install is in progress. Lets other periodic
    // work (schedule/config fetches) skip themselves so the blocking OTA write
    // is never interleaved with another long HTTPS fetch or a broker swap.
    extern bool g_updating;

    bool checkUpdate();
}