#pragma once
// ============================================================
//  Plugin_ADS1110.h  –  M5Stack ADC Unit v1.1  (TI ADS1110)
//
//  A single-channel 16-bit delta-sigma ADC with an on-chip 2.048 V
//  reference — a Grove / Port-A unit that turns a small analog
//  voltage into a clean digital reading.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x48 by default.  The ADS1110 ships in eight order-code variants
//  (ADS1110A0..A7) fixed at 0x48..0x4F; if your unit differs, edit
//  i2cAddresses().  ⚠ The ADS1110 has NO ID register — at a shared
//  address it cannot be told apart from an ADS1115 — so register
//  this plugin only if you actually have the ADC Unit.
//
//  ── Protocol (verified — TI ADS1110 datasheet) ───────────────
//  A one-byte config register; a read clocks back 2 data bytes plus
//  the config byte.  Config 0x0C = continuous, 15 SPS, 16-bit,
//  gain 1.  Voltage = code * 2.048 V / 32768 (gain 1, 16-bit FS).
//
//  Readings:  voltage (V), raw (signed 16-bit code).
// ============================================================
#include "../src/IDevice.h"

class Plugin_ADS1110 : public IDevice {
 public:
  // Config: continuous conversion, 15 SPS / 16-bit, PGA gain 1.
  static constexpr uint8_t CONFIG = 0x0C;
  // On-chip reference is 2.048 V; 16-bit code 32768 = full scale.
  static constexpr float VREF = 2.048f;

  const char* name() const override { return "ADC Unit (ADS1110)"; }
  const char* slug() const override { return "adc"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x48;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // The ADS1110 takes a single config byte directly (no register
    // address).  Its ACK doubles as the presence check.
    bus->beginTransmission(addr);
    bus->write(CONFIG);
    return bus->endTransmission() == 0;
  }

  void update() override {
    // A read clocks back: data MSB, data LSB, config byte.
    if (bus->requestFrom(static_cast<int>(addr), 3) != 3)
      return;
    uint8_t hi = bus->read();
    uint8_t lo = bus->read();
    bus->read();  // config byte — discarded
    _raw = static_cast<int16_t>((hi << 8) | lo);
    _voltage = _raw * VREF / 32768.0f;
  }

  void toJson(JsonObject& o) const override {
    o["voltage"] = _voltage;
    o["raw"] = _raw;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"voltage", _voltage, "V"};
    b[1] = {"raw", static_cast<float>(_raw), ""};
    n = 2;
  }

 private:
  int16_t _raw = 0;
  float _voltage = 0;
};
