#pragma once
// ============================================================
//  PinDevice_PIR.h  –  M5Stack PIR Unit (AS312)   [non-I2C]
//
//  A passive-infrared motion sensor on a single GPIO line: the
//  output goes HIGH while motion is detected and stays high for a
//  couple of seconds after.  Plug into a GPIO-capable Grove port
//  and pass the signal pin to the constructor:
//      fw.addPlugin(new PinDevice_PIR(36));
//
//  Reading:  motion — 1 = motion now, 0 = quiet.
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_PIR : public IPinDevice {
 public:
  explicit PinDevice_PIR(uint8_t signalPin) : _pin(signalPin) {}

  const char* name() const override { return "PIR Motion Unit"; }
  const char* slug() const override { return "pir"; }

  bool beginPins() override {
    pinMode(_pin, INPUT);
    return true;
  }

  void update() override { _motion = (digitalRead(_pin) == HIGH) ? 1 : 0; }

  void toJson(JsonObject& o) const override {
    o["motion"] = _motion;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"motion", static_cast<float>(_motion), ""};
    n = 1;
  }

 private:
  uint8_t _pin;
  uint8_t _motion = 0;
};
