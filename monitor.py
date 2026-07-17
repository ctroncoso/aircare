#!/usr/bin/env python3
# monitor.py — ESP32 serial monitor with watchdog-trigger tee.
#
# Buffers the last N lines of serial output. When it detects a task-WDT trip
# (or any reboot signature), it dumps the buffered tail (the last 40 lines) so
# you see the context that led to the stall, without being flooded by normal
# console chatter. Then it keeps running and watches for the next event.
#
# Usage:  python3 monitor.py [port] [baud]
#         port defaults to /dev/ttyUSB0, baud to 115200.
#
# Detected "reboot" signatures (printed as a banner when seen):
#   - "Task watchdog got triggered"
#   - "abort() was called"
#   - "Rebooting..."
#   - "ets Jun" (bootloader banner)
#   - "rst:" (reset reason)
# After a reboot signature it prints the buffered tail once, then resets the
# buffer so the next run starts clean.

import sys
import time
import serial
from datetime import datetime

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
TAIL = 40  # lines to tee on a watchdog/reboot event

REBOOT_SIGNS = [
    "Task watchdog got triggered",
    "abort() was called",
    "Rebooting...",
    "ets Jun",
    "rst:",
    "Guru Meditation",
]

def ts():
    # Local wall-clock with milliseconds, for correlating stalls against time.
    return datetime.now().strftime("%H:%M:%S.") + "%03d" % (datetime.now().microsecond // 1000)

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    ser.setDTR(False)
    ser.setRTS(False)
    time.sleep(1)
    ser.flushInput()
    buf = []
    print("[monitor] opened %s @ %s, tail=%d" % (PORT, BAUD, TAIL))
    try:
        while True:
            line = ser.readline()
            if not line:
                continue
            try:
                text = line.decode(errors="replace").rstrip()
            except Exception:
                continue
            if text == "":
                continue
            # live echo with a local timestamp prefix
            print("%s %s" % (ts(), text))
            buf.append(text)
            if len(buf) > 200:
                buf = buf[-200:]
            # detect reboot / WDT trip
            if any(s in text for s in REBOOT_SIGNS):
                print("\n" + "=" * 60)
                print("[monitor] REBOOT/WDT EVENT at %s — last %d lines before it:" % (ts(), TAIL))
                print("=" * 60)
                for l in buf[-TAIL:]:
                    print("   " + l)
                print("=" * 60 + "\n")
                buf = []  # reset so the post-reboot boot log starts fresh
    except KeyboardInterrupt:
        print("\n[monitor] stopped")
    finally:
        ser.close()

if __name__ == "__main__":
    main()
