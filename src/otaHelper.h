#pragma once

#include <Arduino.h>
#include "ESP32OTAPull.h"
#include "ledHelper.h"


namespace ota
{
	void callback(int offset, int totallength);
	const char *errtext(int code);

  void checkUpdate(){
    ESP32OTAPull ota;
    ota.SetCallback(callback);
    Serial.println("Checking update");
    int ret;
    ret = ota
              .AllowDowngrades(true)
              .CheckForOTAUpdate("https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/update.json", PROGRAM_VERSION, ESP32OTAPull::DONT_DO_UPDATE);
    // check if update (or downgrade) exists. Download and reboot.
    if (ret == ESP32OTAPull::UPDATE_AVAILABLE)
    {
      Serial.println("Celaring leds");
      leds::clearLeds();
      leds::blinkLed(ledPinR, 4, false);
      leds::blinkLed(ledPinG, 4, false);
      leds::blinkLed(ledPinY, 4, false);
      delay(2000);
      Serial.println("Update found. Downloading & installing.");
      ret = ota
                .AllowDowngrades(true)
                .CheckForOTAUpdate("https://raw.githubusercontent.com/ctroncoso/aircare/main/bins/update.json", PROGRAM_VERSION);
  }
  Serial.printf("CheckOTAForUpdate returned %d (%s)\n\n", ret, errtext(ret));			
  }


	void callback(int offset, int totallength)
	{
		Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
		leds::flipLed(ledPinY);
		// static int status = LOW;
		// status = status == LOW && offset < totallength ? HIGH : LOW;
		// digitalWrite(LED_BUILTIN, status);
	}



	const char *errtext(int code)
	{
		switch(code)
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
} // namespace ota
