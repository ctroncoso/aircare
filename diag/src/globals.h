// diag/globals.h — minimal shim for the standalone diagnostic build.
// The real src/globals.h pulls in WiFiManager/MQTT/OTA; the diag build only
// needs the hardware constants/enums from core/board.h. This file shadows the
// real globals.h because -Idiag is listed before -Isrc (no -Isrc used here).
#pragma once
#include "board.h"
