#pragma once
// ============================================================
//  Plugin_FACES2.h  –  M5Stack Faces II key panel
//
//  STACKABLE faceplate — the QWERTY keyboard, the calculator
//  keypad and the GameBoy-style panel are interchangeable
//  faces on one base.  All three use a MEGA328P at I2C 0x08
//  and the SAME protocol — so ONE plugin covers all of them.
//
//  INPUT only — there is nothing to control.  Reads a single
//  status byte from the panel:
//    • QWERTY / Calc : an ASCII-ish key code, 0 when idle
//    • GameBoy       : a button bitfield, non-zero while held
//
//  Reported readings:
//    key       – the byte read on the most recent poll (live;
//                0 between key presses, button bitfield for the
//                GameBoy face)
//    last_key  – the most recent NON-zero value seen (sticky;
//                handy for the keyboard/calc faces where the
//                code is momentary)
//
//  Polled via fastPoll() every loop iteration so momentary key
//  presses aren't missed between the 500 ms update() cycles.
//
//  ⚠ Address clash: the Earth Unit is also at 0x08.  Both
//  cannot share one bus — the framework binds whichever plugin
//  is registered first and begins successfully.
// ============================================================
#include "../src/IDevice.h"

class Plugin_FACES2 : public IDevice {
 public:
  const char* name() const override { return "Faces II Panel"; }
  const char* slug() const override { return "faces2"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x08;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool wantsFastPoll() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // No ID register on the FACES MEGA firmware — just confirm
    // something ACKs at 0x08.
    bus->beginTransmission(addr);
    return bus->endTransmission() == 0;
  }

  // The FACES panel returns its status as a bare byte — there is
  // no register to address, so we requestFrom() directly rather
  // than going through regRead().
  void fastPoll() override {
    if (bus->requestFrom(static_cast<int>(addr), 1) != 1)
      return;
    uint8_t k = bus->read();
    _key = k;
    if (k != 0)
      _lastKey = k;
  }

  void update() override {}  // all reading happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["key"] = _key;
    o["last_key"] = _lastKey;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"key", static_cast<float>(_key), ""};
    b[1] = {"last_key", static_cast<float>(_lastKey), ""};
    n = 2;
  }

 private:
  uint8_t _key = 0;      // most recent raw read
  uint8_t _lastKey = 0;  // most recent non-zero read
};
