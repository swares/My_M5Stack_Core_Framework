#pragma once
// ============================================================
//  PinDevice_OP180.h  –  M5Stack Unit OP180 (ITR9606)   [non-I2C]
//
//  A 180° non-contact photoelectric (IR break-beam) limit switch.
//  An infrared emitter and receiver face each other; when an
//  object passes between them and blocks the beam, the unit's
//  output changes state.  Single GPIO line.
//
//  Port-B wiring (M5Stack Core):  signal = White = GPIO36.
//      fw.addPlugin(new PinDevice_OP180());       // default GPIO36
//      fw.addPlugin(new PinDevice_OP180(36));     // explicit pin
//
//  ⚠ POLARITY ASSUMPTION: this plugin treats output HIGH as
//  "beam blocked".  The M5Stack docs don't state the polarity
//  explicitly, so verify on hardware — pass an object through the
//  slot and watch /api/op180 — and flip BLOCKED_LEVEL below to LOW
//  if the reading is inverted.
//
//  Readings:  blocked — 1 = beam blocked, 0 = clear.
//             level   — raw pin level (0/1), for diagnostics.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_OP180 : public IPinDevice {
 public:
  explicit PinDevice_OP180(uint8_t signalPin = 36) : _pin(signalPin) {}

  // Raw pin level that means "beam blocked".  Flip to LOW if the
  // hardware reads inverted (see the polarity note above).
  static constexpr int BLOCKED_LEVEL = HIGH;

  const char* name() const override { return "OP180 IR Unit"; }
  const char* slug() const override { return "op180"; }

  bool beginPins() override {
    pinMode(_pin, INPUT);
    return true;
  }

  void update() override {
    int r = digitalRead(_pin);
    _level = (r == HIGH) ? 1 : 0;
    _blocked = (r == BLOCKED_LEVEL) ? 1 : 0;
  }

  void toJson(JsonObject& o) const override {
    o["blocked"] = _blocked;
    o["level"] = _level;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"blocked", static_cast<float>(_blocked), ""};
    n = 1;
  }

 private:
  uint8_t _pin;
  uint8_t _level = 0;
  uint8_t _blocked = 0;
};
