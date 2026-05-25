#pragma once
// ============================================================
//  Plugin_GP8413.h  –  M5Stack DAC 2 Unit (GP8413)
//
//  A dual-channel 15-bit I2C DAC.  The GP8413 converts a 15-bit
//  code (0x0000-0x7FFF) into an analog voltage on each of two
//  outputs — M5's DAC 2 Unit is the 0-10 V variant.  CONTROLLABLE
//  via the Web API.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x59 by default.  The GP8413 base address is 0x58 and the three
//  hardware-strap pins A2/A1/A0 select 0x58-0x5F (eight chips on one
//  bus).  ⚠ 0x58 itself is the SGP30 TVOC address and a CoreS3
//  reserved pin, which is why the unit is strapped up to 0x59; if
//  yours differs, edit i2cAddresses().  The GP8413 has no ID
//  register, so registration is opt-in (see the .ino).
//
//  ── I2C write format (verified — GP8413 datasheet §3.3) ──────
//   [addr] [reg] [DATA low] [DATA high]
//     reg 0x02 → channel 0 (VOUT0),  reg 0x04 → channel 1 (VOUT1)
//   The 15-bit code is left-aligned into a 16-bit word (code << 1),
//   then sent low byte first.   VOUT = code / 0x7FFF * VMAX.
//
//  ── Controls (Web API) ───────────────────────────────────────
//    GET /api/dac/set?v0=<volts>   channel-0 output, 0..VMAX
//    GET /api/dac/set?v1=<volts>   channel-1 output, 0..VMAX
//  ⚠ VMAX is 10 V for the standard DAC 2 Unit; set it to 5.0 if
//  your GP8413 is configured for the 0-5 V range.
//
//  Readings:  v0, v1 — last commanded output voltage per channel.
// ============================================================
#include "../src/IDevice.h"

class Plugin_GP8413 : public IDevice {
 public:
  static constexpr uint8_t REG_V0 = 0x02;      // channel 0 (VOUT0)
  static constexpr uint8_t REG_V1 = 0x04;      // channel 1 (VOUT1)
  static constexpr uint16_t CODE_FS = 0x7FFF;  // 15-bit full-scale code
  // Full-scale output voltage — 10 V for the standard DAC 2 Unit.
  static constexpr float VMAX = 10.0f;

  const char* name() const override { return "DAC 2 Unit (GP8413)"; }
  const char* slug() const override { return "dac"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x59;
    n = 1;
  }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Bring both channels to a known 0 V; the first write's ACK is
    // also the presence check (the GP8413 has no ID register).
    bool ok = _writeChannel(REG_V0, 0);
    _writeChannel(REG_V1, 0);
    _v0 = 0;
    _v1 = 0;
    return ok;
  }

  void update() override {}  // output device — nothing to poll

  void toJson(JsonObject& o) const override {
    o["v0"] = _v0;
    o["v1"] = _v1;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"v0", _v0, "V"};
    b[1] = {"v1", _v1, "V"};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "v0" || param == "v1") {
      float volts;
      if (!_parseVolts(value, volts))
        return false;  // not 0..VMAX
      uint16_t code = static_cast<uint16_t>(volts / VMAX * CODE_FS + 0.5f);
      if (code > CODE_FS)
        code = CODE_FS;
      uint8_t reg = (param == "v0") ? REG_V0 : REG_V1;
      if (!_writeChannel(reg, code))
        return false;
      if (param == "v0") {
        _v0 = volts;
      } else {
        _v1 = volts;
      }
      return true;
    }
    return false;
  }

  void controlSchema(JsonArray& out) const override {
    for (uint8_t ch = 0; ch < 2; ch++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = (ch == 0) ? "v0" : "v1";
      c["label"] = (ch == 0) ? "Channel 0" : "Channel 1";
      c["type"] = "slider";
      c["min"] = 0;
      c["max"] = VMAX;
      c["step"] = 0.1;
      c["unit"] = "V";
      c["value"] = (ch == 0) ? _v0 : _v1;
    }
  }

 private:
  float _v0 = 0, _v1 = 0;  // last commanded voltage per channel

  // Write a 15-bit code to one channel register (0x02 or 0x04).
  bool _writeChannel(uint8_t reg, uint16_t code15) {
    uint16_t w = static_cast<uint16_t>(code15 << 1);  // left-align 15-bit
    bus->beginTransmission(addr);
    bus->write(reg);
    bus->write(static_cast<uint8_t>(w & 0xFF));  // DATA low byte first
    bus->write(static_cast<uint8_t>(w >> 8));    // DATA high byte
    return bus->endTransmission() == 0;
  }

  // Parse a decimal voltage string in 0..VMAX (digits + one dot).
  static bool _parseVolts(const String& v, float& out) {
    if (v.length() == 0)
      return false;
    uint8_t dots = 0;
    for (uint16_t i = 0; i < v.length(); i++) {
      char c = v.charAt(i);
      if (c == '.') {
        if (++dots > 1)
          return false;
      } else if (!isDigit(c)) {
        return false;
      }
    }
    out = v.toFloat();
    return (out >= 0.0f && out <= VMAX);
  }
};
