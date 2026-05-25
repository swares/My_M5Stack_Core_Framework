#pragma once
// ============================================================
//  Plugin_AIN420MA.h  –  M5Stack Module13.2 AIN4-20mA  (SKU M133)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  STM32G030F6-based; a FOUR-channel 4-20 mA current-loop analog
//  acquisition module with isolated front-end (HCNR200).  This is
//  an INPUT / sensor device — it has no outputs and is therefore
//  not controllable; it appears under "Read-only Sensors" on the
//  dashboard with one reading pair per channel.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x55 — confirmed by both the product spec and the
//  "M5Module-4-20mA" Arduino library (MODULE_4_20MA_ADDR).  It is
//  reconfigurable via register 0xFF; if your unit was re-addressed,
//  change the value in i2cAddresses().  0x55 does not clash with
//  any other plugin in this framework.
//
//  ── I2C register map (verified — M5Module-4-20mA library) ────
//    0x00 + 2·ch   ADC-12BIT   R   per channel, uint16 little-endian
//                              (raw 12-bit ADC count, 0-4095)
//    0x20 + 2·ch   CURRENT     R   per channel, uint16 little-endian
//                              (firmware's calibrated current value)
//    0x30 + 2·ch   CAL         W   per-channel calibration (unused here)
//    0xFE          VERSION     R   firmware version
//    0xFF          ADDR-CFG    R/W I2C address
//  ch is 0-3.  The library's getADC12BitsValue() / getCurrentValue()
//  read 2 bytes from these registers and assemble data[0]|(data[1]<<8).
//
//  ── Reads use a repeated start ───────────────────────────────
//  The library's readBytes() does endTransmission(false) + requestFrom
//  — a repeated-start read — and this module's STM32G030 firmware
//  handles it fine.  So unlike the 2-Relay module, this plugin uses
//  the standard IDevice regRead() helper (also repeated-start).
//
//  ── Current unit ─────────────────────────────────────────────
//  The 0x20 "current" register is the firmware's calibrated current
//  reading.  The library exposes it as a raw uint16 without
//  documenting the unit; for a 4-20 mA loop a microamp scale
//  (4000-20000) is the natural fit, so this plugin reports
//  milliamps as raw / CURRENT_PER_MA (1000).  ⚠ If a known current
//  source reads wrong, adjust CURRENT_PER_MA — e.g. 100 if the
//  firmware actually returns mA*100.  The raw 12-bit ADC count is
//  also reported per channel (chN_adc) for diagnostics.
// ============================================================
#include "../src/IDevice.h"

class Plugin_AIN420MA : public IDevice {
 public:
  static constexpr uint8_t CHANNELS = 4;

  // ── Register map (verified — M5Module-4-20mA) ─────────────
  static constexpr uint8_t REG_ADC = 0x00;      // 0x00 + 2·ch
  static constexpr uint8_t REG_CURRENT = 0x20;  // 0x20 + 2·ch

  // Divisor that converts the firmware's CURRENT register to mA.
  // See the "Current unit" note above — adjust if calibration shows
  // the milliamp readings are off by a constant factor.
  static constexpr float CURRENT_PER_MA = 1000.0f;

  const char* name() const override { return "AIN4-20mA Module"; }
  const char* slug() const override { return "ain420ma"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x55;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Presence check — read channel 0's ADC register.  This uses the
    // exact repeated-start regRead path update() uses, so a success
    // here confirms the polling path works too.
    uint8_t tmp[2];
    return regRead(REG_ADC, tmp, 2);
  }

  void update() override {
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      uint8_t d[2];
      if (regRead(REG_ADC + ch * 2, d, 2))
        _adc[ch] = static_cast<uint16_t>(d[0] | (d[1] << 8));
      if (regRead(REG_CURRENT + ch * 2, d, 2))
        _cur[ch] = static_cast<uint16_t>(d[0] | (d[1] << 8));
    }
  }

  void toJson(JsonObject& o) const override {
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      o[String("ch") + ch + "_mA"] = _cur[ch] / CURRENT_PER_MA;
      o[String("ch") + ch + "_adc"] = _adc[ch];
    }
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* mk[CHANNELS] = {"ch0_mA", "ch1_mA", "ch2_mA", "ch3_mA"};
    static const char* ak[CHANNELS] = {"ch0_adc", "ch1_adc", "ch2_adc",
                                       "ch3_adc"};
    uint8_t i = 0;
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      b[i++] = {mk[ch], _cur[ch] / CURRENT_PER_MA, "mA"};
      b[i++] = {ak[ch], static_cast<float>(_adc[ch]), ""};
    }
    n = i;  // 8 readings
  }

 private:
  uint16_t _adc[CHANNELS] = {0};  // raw 12-bit ADC count per channel
  uint16_t _cur[CHANNELS] = {0};  // firmware CURRENT register per channel
};
