#pragma once
// ============================================================
//  PinDevice_DualButton.h  –  M5Stack Unit Dual Button   [non-I2C]
//
//  Two momentary push-buttons on one Grove port.  Each button
//  pulls its signal line LOW when pressed and the unit's on-board
//  pull-ups hold it HIGH when released — so a plain INPUT pin mode
//  is correct.  This unit uses BOTH Port-B signal lines.
//
//  Port-B wiring (M5Stack Core):
//      Red button  = Yellow = GPIO26
//      Blue button = White  = GPIO36
//      fw.addPlugin(new PinDevice_DualButton());        // 26, 36
//      fw.addPlugin(new PinDevice_DualButton(26, 36));  // explicit
//
//  Readings:  btn_red, btn_blue — 1 = pressed, 0 = released.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_DualButton : public IPinDevice {
 public:
  explicit PinDevice_DualButton(uint8_t redPin = 26, uint8_t bluePin = 36)
      : _redPin(redPin), _bluePin(bluePin) {}

  const char* name() const override { return "Dual Button Unit"; }
  const char* slug() const override { return "dualbtn"; }

  bool beginPins() override {
    pinMode(_redPin, INPUT);  // unit carries its own pull-ups
    pinMode(_bluePin, INPUT);
    return true;
  }

  void update() override {
    _red = (digitalRead(_redPin) == LOW) ? 1 : 0;  // active-low
    _blue = (digitalRead(_bluePin) == LOW) ? 1 : 0;
  }

  void toJson(JsonObject& o) const override {
    o["btn_red"] = _red;
    o["btn_blue"] = _blue;
    o["red_pin"] = _redPin;
    o["blue_pin"] = _bluePin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"btn_red", static_cast<float>(_red), ""};
    b[1] = {"btn_blue", static_cast<float>(_blue), ""};
    n = 2;
  }

 private:
  uint8_t _redPin, _bluePin;
  uint8_t _red = 0, _blue = 0;
};
