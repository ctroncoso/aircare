// wifiManagerHelper.h — WiFiManager portal / connect (declarations).
#pragma once

#include "globals.h"
#include "ledHelper.h"
#include <WiFiManager.h>

namespace wifiM
{
    void configModeCallback(WiFiManager *wm);
    bool initWifiM();
}