#pragma once
// ============================================================
//  PinDevice_Button.h  –  M5Stack Button Unit            [non-I2C]
//
//  A momentary push-button on a single GPIO line — covers the
//  Mechanical Key Button Unit and the Mini Button Unit, and serves
//  equally as a generic digital input (e.g. the Laser receiver's
//  detect line).  The button shorts the signal to GND when pressed,
//  so the pin is driven with an internal pull-up and reads LOW
//  while the button is held.
//
//  Port-B wiring (M5Stack Core):  signal = White = GPIO36.
//      fw.addPlugin(new PinDevice_Button());      // default GPIO36
//      fw.addPlugin(new PinDevice_Button(26));    // explicit pin
//
//  Readings:  pressed (1 = held down now),
//             press_count (rising-edge presses since boot).
// ============================================================
#include "../src/IPinDevice.h"

class PinDevice_Button : public IPinDevice {
 public:
  explicit PinDevice_Button(uint8_t signalPin = 36) : _pin(signalPin) {}

  const char* name() const override { return "Button Unit"; }
  const char* slug() const override { return "button"; }

  // Poll fast so a brief tap between the slow update() cycles is
  // still caught by the press counter.
  bool wantsFastPoll() const override { return true; }

  bool beginPins() override {
    pinMode(_pin, INPUT_PULLUP);  // button shorts to GND when pressed
    _pressed = (digitalRead(_pin) == LOW);
    return true;
  }

  void fastPoll() override {
    bool now = (digitalRead(_pin) == LOW);
    if (now && !_pressed)
      _count++;  // count the press (falling) edge
    _pressed = now;
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["pressed"] = _pressed ? 1 : 0;
    o["press_count"] = _count;
    o["pin"] = _pin;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"pressed", static_cast<float>(_pressed ? 1 : 0), ""};
    b[1] = {"press_count", static_cast<float>(_count), ""};
    n = 2;
  }

 private:
  uint8_t _pin;
  bool _pressed = false;
  uint32_t _count = 0;  // press edges since boot
};
