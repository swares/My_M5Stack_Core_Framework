#pragma once
// ============================================================
//  Plugin_4IN8OUT.h  –  M5Stack 4In8Out Module (Module13.2)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  I2C address 0x45.  STM32F030-based digital I/O expander:
//  four passive-contact inputs and eight high-side MOSFET
//  (sourcing) outputs, 1 A per channel.
//
//  INPUT — the four inputs are reported as readings in1..in4.
//
//  CONTROLLABLE — the eight outputs are driven via the Web API:
//    GET /api/4in8out/set?out1=1          output 1 on
//    GET /api/4in8out/set?out5=0          output 5 off
//    GET /api/4in8out/set?outs=170        all 8 from a 0-255 bitmap
//  Validation:
//    outN — N is 1-8, value is a boolean token (0/1/on/off)
//    outs — value is a 0-255 byte bitmap (bit0 = out1 ... bit7 = out8)
//  Anything out of range is rejected; outputs are left untouched.
//
//  Register map — VERIFIED against M5Stack's M5Module-4IN8OUT
//  Arduino library (header + source):
//    0x10..0x13  inputs  — one byte per channel, nonzero = active
//    0x20..0x27  outputs — one byte per channel, 0x01 = on
//    0xFE        firmware version (one byte)
//    0xFF        address-config (write to re-address the module)
//  The library's getInput(i) reads byte 0x10+i and setOutput(i,
//  state) writes byte 0x20+i — the registers are a byte-per-
//  channel array, NOT a packed bitmap.  (The library's own
//  setAllOutput()/reverseOutput() write only a 2-byte block at
//  0x20 and so reach just channels 1-2 — a library bug; this
//  plugin's "all outputs" path writes all eight registers.)
// ============================================================
#include "../src/IDevice.h"

class Plugin_4IN8OUT : public IDevice {
 public:
  // ── Register map (verified — M5Module-4IN8OUT) ────────────
  static constexpr uint8_t REG_INPUT = 0x10;    // 0x10..0x13, byte per input
  static constexpr uint8_t REG_OUTPUT = 0x20;   // 0x20..0x27, byte per output
  static constexpr uint8_t REG_VERSION = 0xFE;  // firmware version

  const char* name() const override { return "4In8Out Module"; }
  const char* slug() const override { return "4in8out"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x45;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }  // the outputs

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    _ver = regRead8(REG_VERSION);  // informational (0 if unreadable)
    // Drive all eight outputs to a known-off state.  The first
    // write's ACK doubles as the module-present check.
    for (uint8_t i = 0; i < 8; i++)
      if (!regWrite(REG_OUTPUT + i, 0x00))
        return false;
    _out = 0x00;
    return true;
  }

  void update() override {
    // Four input channels — one byte each, registers 0x10..0x13.
    uint8_t b[4] = {0};
    if (regRead(REG_INPUT, b, 4)) {
      uint8_t in = 0;
      for (uint8_t i = 0; i < 4; i++)
        if (b[i])
          in |= (1 << i);
      _in = in;
    }
    // Output registers are driven write-only; the _out mirror is
    // authoritative (kept in step by begin() and every command()).
  }

  void toJson(JsonObject& o) const override {
    for (uint8_t i = 0; i < 4; i++)
      o[String("in") + (i + 1)] = (_in >> i) & 1;
    for (uint8_t i = 0; i < 8; i++)
      o[String("out") + (i + 1)] = (_out >> i) & 1;
    o["fw_version"] = _ver;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    uint8_t i = 0;
    static const char* ink[4] = {"in1", "in2", "in3", "in4"};
    for (; i < 4; i++)
      b[i] = {ink[i], static_cast<float>((_in >> i) & 1), ""};
    static const char* outk[8] = {"out1", "out2", "out3", "out4",
                                  "out5", "out6", "out7", "out8"};
    for (uint8_t j = 0; j < 8; j++, i++)
      b[i] = {outk[j], static_cast<float>((_out >> j) & 1), ""};
    n = 12;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // outs — whole 0-255 output bitmap.  Checked before outN so
    // the trailing 's' isn't read as an output index.
    if (param == "outs") {
      int32_t v;
      if (!_parseInt(value, 0, 255, v))
        return false;
      return _writeAll(static_cast<uint8_t>(v));
    }
    // outN — N is 1-8, value is a boolean token.
    if (param.length() == 4 && param.startsWith("out")) {
      char c = param.charAt(3);
      if (c < '1' || c > '8')
        return false;  // bad index
      int on = _parseBool(value);
      if (on < 0)
        return false;        // bad value
      uint8_t ch = c - '1';  // channel 0..7
      if (!regWrite(REG_OUTPUT + ch, on ? 0x01 : 0x00))
        return false;
      if (on)
        _out |= (1 << ch);
      else
        _out &= ~(1 << ch);
      return true;
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  //  One toggle per high-side output; value mirrors the output
  //  register state.  Two quick actions drive all eight at once.
  void controlSchema(JsonArray& out) const override {
    for (uint8_t i = 0; i < 8; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("out") + (i + 1);
      c["label"] = String("Output ") + (i + 1);
      c["type"] = "toggle";
      c["group"] = "Outputs";
      c["value"] = (_out >> i) & 1;
    }
    JsonObject on = out.add<JsonObject>();
    on["label"] = "All on";
    on["type"] = "button";
    on["query"] = "outs=255";
    on["group"] = "Quick actions";
    JsonObject off = out.add<JsonObject>();
    off["label"] = "All off";
    off["type"] = "button";
    off["query"] = "outs=0";
    off["group"] = "Quick actions";
  }

 private:
  uint8_t _in = 0;   // input bits  (0-3), mirror
  uint8_t _out = 0;  // output bits (0-7), mirror of the 8 registers
  uint8_t _ver = 0;  // firmware version (read once at begin)

  // Apply an 8-bit output bitmap by writing one byte per channel —
  // bit i → output register 0x20+i.
  bool _writeAll(uint8_t s) {
    for (uint8_t i = 0; i < 8; i++)
      if (!regWrite(REG_OUTPUT + i, (s >> i) & 1))
        return false;
    _out = s;
    return true;
  }

  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
  static bool _parseInt(const String& v, int32_t lo, int32_t hi, int32_t& out) {
    if (v.length() == 0)
      return false;
    for (uint16_t i = 0; i < v.length(); i++)
      if (!isDigit(v.charAt(i)))
        return false;
    out = v.toInt();
    return (out >= lo && out <= hi);
  }
};
