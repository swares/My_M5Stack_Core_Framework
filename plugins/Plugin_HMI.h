#pragma once
// ============================================================
//  Plugin_HMI.h  –  M5Stack Module HMI  (SKU M129)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  STM32F030F4P6-based human-machine-interface module: a rotary
//  encoder, two push buttons, two indicator LEDs and a 500 mAh
//  battery.
//
//  READ-ONLY plugin.  It surfaces the encoder and buttons as
//  readings; it appears under "Read-only Sensors" on the dashboard
//  and has no controls.  (The module's two LEDs and its
//  reset-counter command are writes — deliberately not exposed
//  here, this plugin only reads.)
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
//    0x30  LED         W   indicator LEDs (not used — read-only)
//    0x40  RESET-CNT   W   reset the counter (not used — read-only)
//    0xFE  VERSION     R   firmware version
//    0xFF  ADDR-CFG    R/W I2C address
//
//  ── I2C framing ──────────────────────────────────────────────
//  The library's readBytes() uses endTransmission(false) — a
//  repeated-start read — which is exactly what IDevice's regRead()
//  helper does, so reads use regRead() directly.
// ============================================================
#include "../src/IDevice.h"

class Plugin_HMI : public IDevice {
 public:
  // ── Register map (verified — M5Module-HMI) ────────────────
  static constexpr uint8_t REG_ENCODER = 0x00;    // int32 LE
  static constexpr uint8_t REG_INCREMENT = 0x10;  // int32 LE
  static constexpr uint8_t REG_BUTTON = 0x20;     // S, +1, +2

  const char* name() const override { return "HMI Module"; }
  const char* slug() const override { return "hmi"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x41;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }

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
  }

  void toJson(JsonObject& o) const override {
    o["encoder"] = _encoder;
    o["increment"] = _increment;
    o["buttonS"] = _btnS ? 1 : 0;
    o["button1"] = _btn1 ? 1 : 0;
    o["button2"] = _btn2 ? 1 : 0;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"encoder", static_cast<float>(_encoder), ""};
    b[1] = {"increment", static_cast<float>(_increment), ""};
    b[2] = {"buttonS", static_cast<float>(_btnS ? 1 : 0), ""};
    b[3] = {"button1", static_cast<float>(_btn1 ? 1 : 0), ""};
    b[4] = {"button2", static_cast<float>(_btn2 ? 1 : 0), ""};
    n = 5;
  }

 private:
  int32_t _encoder = 0;    // absolute encoder count
  int32_t _increment = 0;  // increment-register delta count
  uint8_t _btnS = 0, _btn1 = 0, _btn2 = 0;

  // Assemble a 4-byte little-endian signed int32 (same order the
  // M5Module-HMI library's getEncoderValue() uses).  The bytes are
  // promoted through uint32_t first so the high bit lands cleanly.
  static int32_t _i32(const uint8_t* d) {
    return static_cast<int32_t>(
        static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8) |
        (static_cast<uint32_t>(d[2]) << 16) |
        (static_cast<uint32_t>(d[3]) << 24));
  }
};
