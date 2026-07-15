// otaHelper.cpp — OTA firmware update check implementation.
#include "otaHelper.h"
#include "core/board.h"  // updateURL — centralised manifest URL constant
#include "mqttHelper.h"  // publishEvent / client.connected() — guarded, see below

namespace ota
{
    bool g_updating = false;

    void callback(int offset, int totallength)
    {
        Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
        leds::flipLed(ledPinY);
    }

    const char *errtext(int code)
    {
        switch (code)
        {
        case ESP32OTAPull::UPDATE_AVAILABLE:
            return "An update is available but wasn't installed";
        case ESP32OTAPull::NO_UPDATE_PROFILE_FOUND:
            return "No profile matches";
        case ESP32OTAPull::NO_UPDATE_AVAILABLE:
            return "Profile matched, but update not applicable";
        case ESP32OTAPull::UPDATE_OK:
            return "An update was done, but no reboot";
        case ESP32OTAPull::HTTP_FAILED:
            return "HTTP GET failure";
        case ESP32OTAPull::WRITE_ERROR:
            return "Write error";
        case ESP32OTAPull::JSON_PROBLEM:
            return "Invalid JSON";
        case ESP32OTAPull::OTA_UPDATE_FAIL:
            return "Update fail (no OTA partition?)";
        default:
            if (code > 0)
                return "Unexpected HTTP response code";
            break;
        }
        return "Unknown error";
    }

    // Reboot the device, flushing serial and (if connected) an event first.
    // checkUpdate() may be called from setup() before MQTT is up, so the
    // publish is guarded by client.connected().
    void doReboot(const char *reason)
    {
        if (mqtt::client.connected())
        {
            mqtt::publishEvent(INFO, String("MCU|RESTART|") + reason);
            // Pump once so the PUBLISH actually goes out before we reset.
            mqtt::mqttPump();
        }
        leds::blinkLed(ledPinG, 3, false);
        delay(2000);
        ESP.restart();
    }

    bool checkUpdate()
    {
        // Mark the OTA window so updateTick() skips the (also blocking) schedule
        // and config fetches this cycle — the OTA write must run exclusively.
        g_updating = true;

        ESP32OTAPull ota;
        ota.SetCallback(callback);
        Serial.println("Checking update");

        // Single fetch + install. The library reports availability vs. installed
        // in one pass, so we no longer do a separate DONT_DO_UPDATE probe (that
        // introduced a TOCTOU where update.json could change between calls).
        int ret = ota.AllowDowngrades(true)
                      .CheckForOTAUpdate(updateURL,
                                         PROGRAM_VERSION);

        bool installed = (ret == ESP32OTAPull::UPDATE_OK ||
                          ret == ESP32OTAPull::UPDATE_AVAILABLE);

        Serial.printf("CheckOTAForUpdate returned %d (%s)\n\n", ret, errtext(ret));

        if (installed)
        {
            // The new image is now on the inactive OTA partition and marked
            // bootable. Without an explicit restart the device would keep
            // running the OLD firmware forever — the remote update would
            // silently never take effect. Reboot to apply it.
            doReboot("Update installed, applying new firmware");
            // does not return
        }

        g_updating = false;
        return false;
    }
}