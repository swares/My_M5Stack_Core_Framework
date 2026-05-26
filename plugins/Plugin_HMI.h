#pragma once
// ============================================================
//  Plugin_HMI.h  –  M5Stack Module HMI  (SKU M129)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  STM32F030F4P6-based human-machine-interface module: a rotary
//  encoder, two push buttons, two indicator LEDs and a 500 mAh
//  battery.
//
//  CONTROLLABLE.  The encoder and buttons are reported as readings;
//  the two indicator LEDs and a counter-reset action are driven via
//  the Web API only:
//    GET /api/hmi/set?led1=1      indicator LED 1 on
//    GET /api/hmi/set?led2=off    indicator LED 2 off
//    GET /api/hmi/set?reset=1     zero the encoder / increment count
//  led1 / led2 take a boolean token (0/1/on/off/true/false); anything
//  else is rejected.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x41 — confirmed by both the product spec and the M5Module-HMI
//  library (HMI_ADDR).  Does not clash with anything else in this
//  framework.
//
//  ── I2C register map (verified — M5Module-HMI library) ───────
//    0x00  ENCODER     R   int32 little-endian — absolute count
//    0x10  INCREMENT   R   int32 little-endian — delta count
//    0x20  BUTTON S    R   1 byte — encoder push-button
//    0x21  BUTTON 1    R   1 byte — input button 1
//    0x22  BUTTON 2    R   1 byte — input button 2
//    0x30  LED 1       R/W 1 byte — 0 = off, non-zero = on
//    0x31  LED 2       R/W 1 byte — 0 = off, non-zero = on
//    0x40  RESET-CNT   W   1 byte — write 1 to zero the counter
//    0xFE  VERSION     R   firmware version
//    0xFF  ADDR-CFG    R/W I2C address
//  The library drives LED n with setLEDStatus(n, status) — a plain
//  one-byte write to register 0x30 + n — and zeroes the count with
//  resetCounter(), a one-byte write of 1 to register 0x40.
//
//  ── I2C framing ──────────────────────────────────────────────
//  The library's readBytes() uses endTransmission(false) — a
//  repeated-start read — exactly what IDevice's regRead() helper
//  does; writeBytes() is a plain STOP-terminated register write,
//  the form regWrite() uses.  So reads go through regRead() and
//  writes through regWrite().
// ============================================================
#include "../src/IDevice.h"

class Plugin_HMI : public IDevice {
 public:
  // ── Register map (verified — M5Module-HMI) ────────────────
  static constexpr uint8_t REG_ENCODER = 0x00;    // int32 LE
  static constexpr uint8_t REG_INCREMENT = 0x10;  // int32 LE
  static constexpr uint8_t REG_BUTTON = 0x20;     // S, +1, +2
  static constexpr uint8_t REG_LED = 0x30;        // LED 1/2 at +0/+1
  static constexpr uint8_t REG_RESET = 0x40;      // write 1 = zero count

  const char* name() const override { return "HMI Module"; }
  const char* slug() const override { return "hmi"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x41;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Presence check — read the 4-byte encoder register.  Uses the
    // same repeated-start regRead path update() uses, so success
    // here confirms the polling path works too.
    uint8_t tmp[4];
    return regRead(REG_ENCODER, tmp, 4);
  }

  void update() override {
    uint8_t d[4];
    if (regRead(REG_ENCODER, d, 4))
      _encoder = _i32(d);
    if (regRead(REG_INCREMENT, d, 4))
      _increment = _i32(d);
    uint8_t b;
    if (regRead(REG_BUTTON + 0, &b, 1))
      _btnS = b;
    if (regRead(REG_BUTTON + 1, &b, 1))
      _btn1 = b;
    if (regRead(REG_BUTTON + 2, &b, 1))
      _btn2 = b;
    // The LED registers read back the live indicator state — the
    // module keeps it on its own battery across a host reboot.
    if (regRead(REG_LED + 0, &b, 1))
      _led1 = b ? 1 : 0;
    if (regRead(REG_LED + 1, &b, 1))
      _led2 = b ? 1 : 0;
  }

  void toJson(JsonObject& o) const override {
    o["encoder"] = _encoder;
    o["increment"] = _increment;
    o["buttonS"] = _btnS ? 1 : 0;
    o["button1"] = _btn1 ? 1 : 0;
    o["button2"] = _btn2 ? 1 : 0;
    o["led1"] = _led1;
    o["led2"] = _led2;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"encoder", static_cast<float>(_encoder), ""};
    b[1] = {"increment", static_cast<float>(_increment), ""};
    b[2] = {"buttonS", static_cast<float>(_btnS ? 1 : 0), ""};
    b[3] = {"button1", static_cast<float>(_btn1 ? 1 : 0), ""};
    b[4] = {"button2", static_cast<float>(_btn2 ? 1 : 0), ""};
    b[5] = {"led1", static_cast<float>(_led1), ""};
    b[6] = {"led2", static_cast<float>(_led2), ""};
    n = 7;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // ledN — N is 1-2, value a boolean token.  LED 1 maps to
    // register 0x30, LED 2 to 0x31 (M5Module-HMI setLEDStatus).
    if (param == "led1" || param == "led2") {
      int on = _parseBool(value);
      if (on < 0)
        return false;  // bad value
      uint8_t idx = (param == "led2") ? 1 : 0;
      if (!regWrite(REG_LED + idx, on ? 1 : 0))
        return false;
      if (idx == 0)
        _led1 = static_cast<uint8_t>(on);
      else
        _led2 = static_cast<uint8_t>(on);
      return true;
    }
    // reset — zero the encoder / increment counter (write 1 to 0x40,
    // the M5Module-HMI resetCounter() command).
    if (param == "reset")
      return regWrite(REG_RESET, 1);
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  //  Two LED toggles (value mirrors the live register state) plus a
  //  one-shot "reset count" action button.
  void controlSchema(JsonArray& out) const override {
    JsonObject l1 = out.add<JsonObject>();
    l1["id"] = "led1";
    l1["label"] = "LED 1";
    l1["type"] = "toggle";
    l1["group"] = "Indicator LEDs";
    l1["value"] = _led1;

    JsonObject l2 = out.add<JsonObject>();
    l2["id"] = "led2";
    l2["label"] = "LED 2";
    l2["type"] = "toggle";
    l2["group"] = "Indicator LEDs";
    l2["value"] = _led2;

    JsonObject rst = out.add<JsonObject>();
    rst["label"] = "Reset count";
    rst["type"] = "button";
    rst["query"] = "reset=1";
    rst["group"] = "Encoder";
  }

 private:
  int32_t _encoder = 0;          // absolute encoder count
  int32_t _increment = 0;        // increment-register delta count
  uint8_t _btnS = 0, _btn1 = 0, _btn2 = 0;
  uint8_t _led1 = 0, _led2 = 0;  // indicator-LED state mirror

  // Assemble a 4-byte little-endian signed int32 (same order the
  // M5Module-HMI library's getEncoderValue() uses).  The bytes are
  // promoted through uint32_t first so the high bit lands cleanly.
  static int32_t _i32(const uint8_t* d) {
    return static_cast<int32_t>(
        static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8) |
        (static_cast<uint32_t>(d[2]) << 16) |
        (static_cast<uint32_t>(d[3]) << 24));
  }

  // "1/on/true" -> 1, "0/off/false" -> 0, anything else -> -1.
  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
};
