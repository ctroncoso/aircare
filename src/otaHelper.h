#pragma once

#include <Arduino.h>


namespace ota
{
    void checkUpdate(){

    }


void callback(int offset, int totallength)
{
	Serial.printf("Updating %d of %d (%02d%%)...\n", offset, totallength, 100 * offset / totallength);
	// static int status = LOW;
	// status = status == LOW && offset < totallength ? HIGH : LOW;
	// digitalWrite(LED_BUILTIN, status);
}
} // namespace ota
