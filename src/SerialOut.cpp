// ============================================================
//  SerialOut.cpp
// ============================================================
#include "Config.h"        // OUT_SERIAL — must precede the #if
#if OUT_SERIAL
#include "SerialOut.h"
#include "Framework.h"
#include "BoardInfo.h"
#include <cstdio>          // snprintf

// Render a printable bus label for a plugin.  Returns one of:
//   "internal"          — Core2/CoreS3 on-board chip
//   "external"          — Core2/CoreS3 Port-A direct
//   "shared"            — Core1 single bus (Port-A == internal)
//   "hub 0xNN chK"      — plugin lives behind a PCA9548A
// Writes into the caller's buffer because there's no per-plugin
// storage and we need to compose channel info on the fly.
static const char* _busLabel(IDevice* p, char* buf, size_t bufLen) {
  const auto& bi = BoardInfo::detect();
  if (p->muxAddr != 0) {
    snprintf(buf, bufLen, "hub 0x%02X ch%u", p->muxAddr, p->muxChannel);
    return buf;
  }
  if (bi.sharedBus())       return "shared";
  if (p->bus == bi.intBus)  return "internal";
  if (p->bus == bi.extBus)  return "external";
  return "?";
}

void SerialOut::begin() {
  Serial.begin(SERIAL_BAUD);
  delay(200);
  if (!enabled) return;
  Serial.println(F("\n========================================"));
  Serial.println(F("  M5Stack I2C Framework  –  Serial Out"));
  Serial.println(F("========================================"));
}

void SerialOut::update(Framework* fw) {
  if (!enabled) return;
  if (millis() - _last < POLL_MS) return;
  _last = millis();

  char hdr[52];
  uint32_t s = millis()/1000;
  snprintf(hdr, sizeof(hdr),
           "\n[%02lu:%02lu:%02lu] ─── Sensor Readings ───────────",
           s/3600, (s%3600)/60, s%60);
  Serial.println(hdr);

  bool any = false;
  for (auto* p : fw->plugins()) {
    if (!p->active) continue;
    any = true;
    char busBuf[24];
    Serial.printf("  %-20s  0x%02X  (%s bus)\n",
                  p->name(), p->addr,
                  _busLabel(p, busBuf, sizeof(busBuf)));

    SensorVal vals[16]; uint8_t cnt = 0;
    p->getReadings(vals, cnt);
    for (uint8_t i = 0; i < cnt; i++) {
      Serial.printf("    %-18s %10.4f  %s\n",
                    vals[i].key, vals[i].value, vals[i].unit);
    }
  }
  if (!any) Serial.println(F("  (no active sensors)"));
}
#endif  // OUT_SERIAL
