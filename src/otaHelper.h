// otaHelper.h — OTA firmware update check (declarations).
#pragma once

#include <Arduino.h>
#include "ESP32OTAPull.h"
#include "ledHelper.h"

namespace ota
{
    bool checkUpdate();
}