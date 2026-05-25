#pragma once
// ============================================================
//  Plugin_PPS.h  –  M5Stack Module13.2 PPS  (SKU M137)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  STM32G030F6-based programmable buck power supply: DC 9-36 V in,
//  programmable 0.5-30 V @ 0-5 A out (100 W, peak 150 W), with
//  isolated power + I2C.
//
//  ⚠ The module needs its external DC 9-36 V supply connected or it
//  does not appear on I2C at all (per the M5Stack docs) — if the
//  boot scan doesn't find 0x35, check the DC input first.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x35 — confirmed by both the product spec and the M5Module-PPS
//  library (MODULE_POWER_ADDR).  Does not clash with anything in
//  this framework (the AXP PMIC is 0x34, one address lower).
//
//  ── I2C register map (verified — M5Module-PPS library) ───────
//    0x00  ID            R    uint16 little-endian
//    0x04  ENABLE        R/W  1 byte: 1 = output on, 0 = off
//    0x05  RUNNING_MODE  R    1 byte (raw mode code)
//    0x08  VOUT_READBACK R    float — measured output voltage (V)
//    0x0C  IOUT_READBACK R    float — measured output current (A)
//    0x10  TEMP_READBACK R    float — module temperature (°C)
//    0x14  VIN_READBACK  R    float — measured input voltage (V)
//    0x18  VOUT_SET      R/W  float — output-voltage setpoint (V)
//    0x1C  IOUT_SET      R/W  float — output-current limit (A)
//    0x5F  ADDR-CFG      R/W  I2C address
//  All "float" registers are 4-byte IEEE-754 in native
//  little-endian order (the library packs them through a
//  float/uint8[4] union — equivalent to a plain memcpy on the
//  little-endian ESP32 and STM32).
//
//  ── I2C framing ──────────────────────────────────────────────
//  The library's readBytes() uses endTransmission(false) — a
//  repeated-start read — which is exactly what the IDevice
//  regRead() helper does, so reads use regRead() directly.  Writes
//  are plain STOP-terminated transactions.
//
//  CONTROLLABLE device.  Drive it via the Web API only:
//    GET /api/pps/set?enable=1        output ON
//    GET /api/pps/set?vset=12.0       set output voltage to 12 V
//    GET /api/pps/set?iset=1.5        set current limit to 1.5 A
//  Validation:
//    enable — 0/1 (or on/off)
//    vset   — 0.5 .. 30.0  (volts)
//    iset   — 0.0 .. 5.0   (amps)
//  Anything out of range is rejected and the hardware is untouched.
//
//  ⚠ SAFETY: begin() forces the output DISABLED so the framework
//  always starts with the supply off; the user re-enables it
//  explicitly from the dashboard.  The voltage/current setpoints
//  are left as the module had them.
//
//  ── Commanded vs. readback setpoints ─────────────────────────
//  vset / iset reported to the dashboard reflect the last value
//  COMMANDED through command() — recorded only once the I2C write
//  ACKs.  The raw 0x18 / 0x1C readback is published separately as
//  vset_hw / iset_hw.  This split exists because the module tends
//  to report those setpoint registers as 0 while the output is
//  disabled; binding the slider to the readback would snap it back
//  to minimum after every poll.  It also makes the serial "vset"
//  line a direct confirmation that a setpoint write was accepted.
// ============================================================
#include "../src/IDevice.h"

class Plugin_PPS : public IDevice {
 public:
  // ── Register map (verified — M5Module-PPS) ────────────────
  static constexpr uint8_t REG_ENABLE = 0x04;    // R/W 1 byte
  static constexpr uint8_t REG_MODE = 0x05;      // R   1 byte
  static constexpr uint8_t REG_VOUT_RB = 0x08;   // R   float
  static constexpr uint8_t REG_IOUT_RB = 0x0C;   // R   float
  static constexpr uint8_t REG_TEMP_RB = 0x10;   // R   float
  static constexpr uint8_t REG_VIN_RB = 0x14;    // R   float
  static constexpr uint8_t REG_VOUT_SET = 0x18;  // R/W float
  static constexpr uint8_t REG_IOUT_SET = 0x1C;  // R/W float

  // Output limits — from the product spec (0.5-30 V @ 0-5 A).
  static constexpr float VOUT_MIN = 0.5f;
  static constexpr float VOUT_MAX = 30.0f;
  static constexpr float IOUT_MIN = 0.0f;
  static constexpr float IOUT_MAX = 5.0f;

  const char* name() const override { return "PPS Module"; }
  const char* slug() const override { return "pps"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x35;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // SAFETY — force the output disabled before the framework runs.
    // regWrite is a plain STOP write (matches the library's
    // writeBytes); its ACK is also the module-present check.
    if (!regWrite(REG_ENABLE, 0x00))
      return false;
    _enabled = 0;
    return true;
  }

  void update() override {
    uint8_t b[4];
    if (regRead(REG_VOUT_RB, b, 4))
      _vout = _bytesToFloat(b);
    if (regRead(REG_IOUT_RB, b, 4))
      _iout = _bytesToFloat(b);
    if (regRead(REG_TEMP_RB, b, 4))
      _temp = _bytesToFloat(b);
    if (regRead(REG_VIN_RB, b, 4))
      _vin = _bytesToFloat(b);
    if (regRead(REG_VOUT_SET, b, 4))
      _vset = _bytesToFloat(b);
    if (regRead(REG_IOUT_SET, b, 4))
      _iset = _bytesToFloat(b);
    uint8_t v;
    if (regRead(REG_ENABLE, &v, 1))
      _enabled = v;
    if (regRead(REG_MODE, &v, 1))
      _mode = v;
    // Seed the commanded mirror from the module's own setpoints on the
    // first poll, so the sliders open at the hardware's current values.
    // After that, command() owns _vsetCmd / _isetCmd.
    if (!_seeded) {
      _vsetCmd = _vset;
      _isetCmd = _iset;
      _seeded = true;
    }
  }

  void toJson(JsonObject& o) const override {
    o["vout"] = _vout;     // measured output voltage
    o["iout"] = _iout;     // measured output current
    o["vin"] = _vin;       // measured input voltage
    o["temp"] = _temp;     // module temperature
    o["vset"] = _vsetCmd;  // commanded output-voltage setpoint
    o["iset"] = _isetCmd;  // commanded output-current limit
    o["vset_hw"] = _vset;  // raw 0x18 readback (0 V while output off)
    o["iset_hw"] = _iset;  // raw 0x1C readback
    o["enabled"] = _enabled ? 1 : 0;
    o["mode"] = _mode;  // raw running-mode code
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"vout", _vout, "V"};
    b[1] = {"iout", _iout, "A"};
    b[2] = {"vin", _vin, "V"};
    b[3] = {"temp", _temp, "°C"};
    b[4] = {"vset", _vsetCmd, "V"};
    b[5] = {"iset", _isetCmd, "A"};
    b[6] = {"enabled", static_cast<float>(_enabled ? 1 : 0), ""};
    n = 7;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "enable") {
      int on = _parseBool(value);
      if (on < 0)
        return false;
      if (!regWrite(REG_ENABLE, on ? 0x01 : 0x00))
        return false;
      _enabled = static_cast<uint8_t>(on);
      return true;
    }
    if (param == "vset") {
      float v;
      if (!_parseFloat(value, VOUT_MIN, VOUT_MAX, v))
        return false;
      if (!_writeFloat(REG_VOUT_SET, v))
        return false;
      _vsetCmd = v;
      _seeded = true;  // I2C write ACKed — record command
      return true;
    }
    if (param == "iset") {
      float v;
      if (!_parseFloat(value, IOUT_MIN, IOUT_MAX, v))
        return false;
      if (!_writeFloat(REG_IOUT_SET, v))
        return false;
      _isetCmd = v;
      _seeded = true;  // I2C write ACKed — record command
      return true;
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    JsonObject en = out.add<JsonObject>();
    en["id"] = "enable";
    en["label"] = "Output enabled";
    en["type"] = "toggle";
    en["group"] = "Output";
    en["value"] = _enabled ? 1 : 0;

    JsonObject v = out.add<JsonObject>();
    v["id"] = "vset";
    v["label"] = "Set voltage";
    v["type"] = "slider";
    v["min"] = VOUT_MIN;
    v["max"] = VOUT_MAX;
    v["step"] = 0.1;
    v["unit"] = "V";
    v["group"] = "Output";
    v["value"] = _vsetCmd;

    JsonObject c = out.add<JsonObject>();
    c["id"] = "iset";
    c["label"] = "Current limit";
    c["type"] = "slider";
    c["min"] = IOUT_MIN;
    c["max"] = IOUT_MAX;
    c["step"] = 0.05;
    c["unit"] = "A";
    c["group"] = "Output";
    c["value"] = _isetCmd;
  }

 private:
  float _vout = 0, _iout = 0, _vin = 0, _temp = 0;
  float _vset = 0, _iset = 0;        // raw 0x18 / 0x1C readback
  float _vsetCmd = 0, _isetCmd = 0;  // last commanded setpoints
  bool _seeded = false;              // _vsetCmd/_isetCmd seeded yet?
  uint8_t _enabled = 0, _mode = 0;

  // Write a 4-byte little-endian float to a register.  Plain
  // STOP-terminated transaction — matches the M5Module-PPS
  // library's writeBytes().
  bool _writeFloat(uint8_t reg, float f) {
    union {
      float v;
      uint8_t b[4];
    } u;
    u.v = f;
    bus->beginTransmission(addr);
    bus->write(reg);
    bus->write(u.b, 4);
    return bus->endTransmission() == 0;
  }
  // Decode a 4-byte little-endian IEEE-754 float (same union form
  // the library's bytes_to_float() uses).
  static float _bytesToFloat(const uint8_t* s) {
    union {
      float v;
      uint8_t b[4];
    } u;
    u.b[0] = s[0];
    u.b[1] = s[1];
    u.b[2] = s[2];
    u.b[3] = s[3];
    return u.v;
  }

  // "1/on/true" → 1, "0/off/false" → 0, anything else → -1.
  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
  // Parse a decimal (optional sign, one optional '.') and verify it
  // falls within [lo,hi].  Rejects any other character.
  static bool _parseFloat(const String& s, float lo, float hi, float& out) {
    uint16_t len = s.length();
    if (len == 0)
      return false;
    uint16_t i = 0;
    if (s.charAt(0) == '-' || s.charAt(0) == '+')
      i = 1;
    bool dot = false, digit = false;
    for (; i < len; i++) {
      char ch = s.charAt(i);
      if (ch == '.') {
        if (dot)
          return false;
        dot = true;
      } else if (isDigit(ch)) {
        digit = true;
      } else {
        return false;
      }
    }
    if (!digit)
      return false;
    out = s.toFloat();
    return (out >= lo && out <= hi);
  }
};
