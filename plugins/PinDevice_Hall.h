#pragma once
// ============================================================
//  PinDevice_Hall.h  –  M5Stack Unit Hall (A3144E)   [non-I2C]
//
//  A Hall-effect magnetic switch on a single GPIO line.  Three
//  A3144E sensors feed a 74HC08 gate; the unit drives its output
//  LOW when a magnetic pole is present (low-level active) and
//  HIGH otherwise.  It is a push-pull output, so no pull-up is
//  needed and a plain INPUT pin mode is correct.
//
//  Port-B wiring (M5Stack Core):  signal = White = GPIO36.
//      fw.addPlugin(new PinDevice_Hall());        // default GPIO36
//      fw.addPlugin(new PinDevice_Hall(36));      // explicit pin
//
//  Readings:  magnet — 1 = magnet detected, 0 = none.
//             level  — raw pin level (0/1), for diagnostics.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Hall : public IPinDevice {
 public:
  explicit PinDevice_Hall(uint8_t signalPin = 36) : _pin(signalPin) {}

  const char* name() const override { return "Hall Unit"; }
  const char* slug() const override { return "hall"; }

  bool beginPins() override {
    pinMode(_pin, INPUT);  // unit has a push-pull driver
    return true;
  }

  void update() override {
    _level = (digitalRead(_pin) == HIGH) ? 1 : 0;
    _magnet = _level ? 0 : 1;  // low-level active
  }

  void toJson(JsonObject& o) const override {
    o["magnet"] = _magnet;
    o["level"] = _level;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"magnet", static_cast<float>(_magnet), ""};
    n = 1;
  }

 private:
  uint8_t _pin;
  uint8_t _level = 1;  // idle HIGH (no magnet)
  uint8_t _magnet = 0;
};
