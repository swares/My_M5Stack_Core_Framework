#pragma once
// ============================================================
//  Plugin_WEIGHT.h  –  M5Stack Weight Unit (HX711 via STM32)
//
//  The Weight Unit has an STM32F030 between the host I2C bus
//  and the HX711 24-bit load-cell ADC.  Its firmware exposes a
//  small register map at address 0x26:
//
//    0x00  RW  raw HX711 ADC      (int32, little-endian)
//    0x10  R   calibrated weight  (float32, little-endian, grams)
//    0x20  RW  filter mode        (uint8)
//    0x30  W   calibration cmds   (write to tare / set scale)
//    0xFE  R   firmware version
//    0xFF  RW  I2C address
//
//  This plugin reads:
//    • the pre-calibrated float weight at 0x10  (primary value)
//    • the raw ADC at 0x00                      (for debugging)
//
//  No per-plugin tare/scale math is needed — the unit's firmware
//  retains its calibration in flash, so it returns grams directly.
//  If the unit was never calibrated, weight will be inaccurate;
//  use M5Stack's official UIFlow / Arduino calibration sketch
//  once and the values stick.
// ============================================================
#include "../src/IDevice.h"

class Plugin_WEIGHT : public IDevice {
 public:
  const char* name() const override { return "Weight Unit (HX711)"; }
  const char* slug() const override { return "weight"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x26;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Quick sanity: read the firmware version register.  Any
    // value 0..255 is plausible; we just want the I2C transaction
    // to ACK so we know the STM32 is alive.
    uint8_t fw = 0;
    if (!_readRegBytes(0xFE, &fw, 1))
      return false;
    _fwVersion = fw;
    return true;
  }

  void update() override {
    // ── Calibrated weight (float32 LE, grams) ────────────────
    uint8_t fb[4] = {0};
    if (_readRegBytes(0x10, fb, 4)) {
      // STM32 is little-endian; ESP32 is little-endian; just
      // memcpy the four bytes into a float.
      memcpy(&_grams, fb, 4);
      // Guard against NaN / wild values from an uncalibrated unit.
      if (isnan(_grams) || _grams > 1e6f || _grams < -1e6f)
        _grams = 0.0f;
    }

    // ── Raw ADC (int32 LE, HX711 counts) ─────────────────────
    uint8_t rb[4] = {0};
    if (_readRegBytes(0x00, rb, 4)) {
      _rawAdc = static_cast<int32_t>(
          static_cast<uint32_t>(rb[0]) | (static_cast<uint32_t>(rb[1]) << 8) |
          (static_cast<uint32_t>(rb[2]) << 16) |
          (static_cast<uint32_t>(rb[3]) << 24));
    }
  }

  void toJson(JsonObject& o) const override {
    o["weight"] = _grams;
    o["weight_unit"] = "g";
    o["raw_adc"] = _rawAdc;
    o["fw_version"] = _fwVersion;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"weight", _grams, "g"};
    b[1] = {"raw", static_cast<float>(_rawAdc), ""};
    n = 2;
  }

 private:
  float _grams = 0.0f;
  int32_t _rawAdc = 0;
  uint8_t _fwVersion = 0;

  // Helper: write a register address, then read N bytes back.
  // Wraps the framework's regRead with a guard for very small
  // / very large length values.
  bool _readRegBytes(uint8_t reg, uint8_t* dst, uint8_t len) {
    bus->beginTransmission(addr);
    bus->write(reg);
    if (bus->endTransmission(false) != 0)
      return false;
    if (bus->requestFrom(static_cast<int>(addr), static_cast<int>(len)) != len)
      return false;
    for (uint8_t i = 0; i < len; i++)
      dst[i] = bus->read();
    return true;
  }
};
