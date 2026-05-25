#pragma once
// ============================================================
//  SerialOut.h  –  Prints sensor readings to Serial monitor
// ============================================================
#include "Config.h"  // for the OUT_SERIAL build switch

#if OUT_SERIAL
#include <Arduino.h>
#include "IDevice.h"

class Framework;

class SerialOut {
 public:
  void begin();
  void update(Framework* fw);

 private:
  bool enabled = OUT_SERIAL;
  uint32_t _last = 0;
};

#else  // !OUT_SERIAL
#include <Arduino.h>
// ── Stub — per-plugin serial output compiled out (OUT_SERIAL = false).
//  begin() still brings the UART up so the framework's boot
//  diagnostics (printed via Serial directly) keep working; only
//  the periodic per-plugin readings dump is removed.
class Framework;
class SerialOut {
 public:
  bool enabled = false;
  void begin() {
    Serial.begin(SERIAL_BAUD);
    delay(200);
  }
  void update(Framework*) {}
};
#endif  // OUT_SERIAL
