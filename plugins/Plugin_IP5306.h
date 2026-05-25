#pragma once
// ============================================================
//  Plugin_IP5306.h  –  Core1 (Basic / Gray) battery monitor
//
//  The IP5306 is the boost converter / charge manager used on the
//  original M5Stack Core.  Its I2C interface (address 0x75) is
//  minimal — no actual battery voltage, just a 4-level LED-style
//  charge estimate plus a charging-state bit.  We expose what's
//  available so the Core1 has something resembling battery info
//  in the same JSON shape as the AXP192 / AXP2101 plugin.
//
//  Internal bus only.  Won't appear on Core2 / CoreS3 (those use
//  an AXP-series chip at 0x34, handled by Plugin_PMIC instead).
//  Mutually exclusive with Plugin_PMIC by virtue of living at a
//  different I2C address.
//
//  Also covers the M5GO Bottom 2 battery base: that module's
//  fuel gauge is the very same IP5306 at 0x75, so this plugin
//  reports it with no extra work.  (The Bottom 2's SK6812 RGB
//  LEDs are not on I2C and are out of this framework's scope.)
//
//  Register notes (from the M5Stack POWER class in the original
//  M5Stack Arduino library — datasheet bit definitions disagree
//  between revisions, so we match what M5Stack themselves used):
//    0x70  SYS_CTL0    bit 3 = charging in progress
//    0x78  Bat indicator (top nibble):
//            0x00 = 4 LEDs lit  ≈ 100 %
//            0x80 = 3 LEDs lit  ≈  75 %
//            0xC0 = 2 LEDs lit  ≈  50 %
//            0xE0 = 1 LED  lit  ≈  25 %
//          (the value is the INVERSE of which LEDs are on)
// ============================================================
#include "../src/IDevice.h"

class Plugin_IP5306 : public IDevice {
 public:
  const char* name() const override { return "Battery (IP5306)"; }
  const char* slug() const override { return "ip5306"; }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Builtin; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x75;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // No reliable WHO_AM_I-style register on the IP5306, so all we
    // can do is verify a simple register read completes — if the
    // chip is present and ACKs SMBus reads, that's enough.
    uint8_t v;
    return regRead(0x70, &v, 1);
  }

  void update() override {
    // Charging state from SYS_CTL0 (0x70) bit 3.  This matches the
    // decoding used in M5Stack's original POWER class.
    uint8_t ctl0 = 0;
    regRead(0x70, &ctl0, 1);
    _charging = (ctl0 & 0x08) != 0;

    // Battery level from the LED-state register (0x78), top nibble.
    // The IP5306 drives four physical battery-indicator LEDs and
    // mirrors their state into bits 4-7 of this register, with the
    // peculiarity that the bits represent which LEDs are OFF (so
    // all four LEDs lit = 0x00 = full).  Map back to a percentage.
    uint8_t led = 0;
    regRead(0x78, &led, 1);
    led &= 0xF0;
    switch (led) {
      case 0x00:
        _pct = 100;
        break;
      case 0x80:
        _pct = 75;
        break;
      case 0xC0:
        _pct = 50;
        break;
      case 0xE0:
        _pct = 25;
        break;
      default:
        _pct = 0;
        break;
    }
  }

  // The IP5306 isn't great at being polled rapidly — it can stall
  // briefly while servicing internal charge logic.  Override the
  // default bare-probe liveness check with a proper SMBus read of
  // the same register begin() touched, so a flaky moment doesn't
  // get the plugin yanked.
  bool isAlive() override {
    uint8_t v;
    return regRead(0x70, &v, 1);
  }

  void toJson(JsonObject& o) const override {
    o["battery"] = _pct;
    o["battery_unit"] = "%";
    o["charging"] = _charging;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"bat%", static_cast<float>(_pct), "%"};
    b[1] = {"charge", _charging ? 1.0f : 0.0f, ""};
    n = 2;
  }

 private:
  uint8_t _pct = 0;
  bool _charging = false;
};
