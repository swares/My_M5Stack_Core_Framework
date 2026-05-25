#pragma once
// ============================================================
//  PinDevice_Limit.h  –  M5Stack Unit Limit (travel switch) [non-I2C]
//
//  A contact-type mechanical limit / travel switch on a single
//  GPIO line.  The unit holds its output at 3.3 V HIGH through an
//  on-unit pull-up and pulls it to 0 V LOW while the lever is
//  pressed — so a plain INPUT pin mode is correct.
//
//  Port-B wiring (M5Stack Core):  signal = White = GPIO36.
//      fw.addPlugin(new PinDevice_Limit());       // default GPIO36
//      fw.addPlugin(new PinDevice_Limit(36));     // explicit pin
//
//  Readings:  pressed — 1 = lever pressed, 0 = released.
//             level   — raw pin level (0/1), for diagnostics.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Limit : public IPinDevice {
 public:
  explicit PinDevice_Limit(uint8_t signalPin = 36) : _pin(signalPin) {}

  const char* name() const override { return "Limit Switch Unit"; }
  const char* slug() const override { return "limit"; }

  bool beginPins() override {
    pinMode(_pin, INPUT);  // unit provides its own pull-up
    return true;
  }

  void update() override {
    _level = (digitalRead(_pin) == HIGH) ? 1 : 0;
    _pressed = _level ? 0 : 1;  // pressing pulls the line LOW
  }

  void toJson(JsonObject& o) const override {
    o["pressed"] = _pressed;
    o["level"] = _level;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"pressed", static_cast<float>(_pressed), ""};
    n = 1;
  }

 private:
  uint8_t _pin;
  uint8_t _level = 1;  // idle HIGH (released)
  uint8_t _pressed = 0;
};
