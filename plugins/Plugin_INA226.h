#pragma once
// ============================================================
//  Plugin_INA226.h  –  INA226 Current / Voltage / Power Monitor
//
//  A TI INA226 high-side power monitor — the chip behind M5's
//  INA226-1A and INA226-10A Power Monitor Units.  It measures bus
//  voltage and the voltage across an external shunt; current and
//  power are derived from the shunt voltage and the shunt's
//  resistance.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x40 by default (A0,A1 -> GND); the INA226 can be strapped
//  0x40..0x4F.  Bound by a POSITIVE die-ID check (reg 0xFF =
//  0x2260), so it never false-binds — even where the address would
//  otherwise clash (e.g. an INA3221, whose die-ID is 0x3220).
//
//  ── I2C register map (verified — TI INA226 datasheet) ────────
//   16-bit big-endian registers:
//    0x01  Shunt Voltage   signed, 2.5 uV per LSB
//    0x02  Bus Voltage     1.25 mV per LSB
//    0xFF  Die ID          fixed 0x2260
//   Bus and shunt voltage are read directly; the power-up default
//   config (continuous shunt+bus) needs no change.
//
//  ── ⚠ Shunt resistance ───────────────────────────────────────
//  Current = shunt-voltage / R_SHUNT.  M5's INA226-1A and
//  INA226-10A units use DIFFERENT shunt resistors, so set R_SHUNT
//  below to match YOUR unit (check its spec).  A wrong value scales
//  current and power by a constant — the bus voltage is unaffected.
//
//  Readings:  bus_v (V), current_a (A), power_w (W), shunt_mv (mV).
// ============================================================
#include "../src/IDevice.h"

class Plugin_INA226 : public IDevice {
 public:
  static constexpr uint8_t REG_SHUNT_V = 0x01;
  static constexpr uint8_t REG_BUS_V = 0x02;
  static constexpr uint8_t REG_DIE_ID = 0xFF;
  static constexpr uint16_t DIE_ID = 0x2260;

  // LSB sizes from the datasheet.
  static constexpr float SHUNT_LSB_V = 2.5e-6f;  // 2.5 uV
  static constexpr float BUS_LSB_V = 1.25e-3f;   // 1.25 mV

  // ⚠ Set this to your unit's shunt resistance in ohms.  0.002 ohm
  // suits a ~10 A-range unit; an INA226-1A uses a larger shunt.
  static constexpr float R_SHUNT = 0.002f;

  const char* name() const override { return "INA226 Power Monitor"; }
  const char* slug() const override { return "ina226"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x40;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Positive identification — only an INA226 returns 0x2260 here,
    // so this never mis-binds to an INA3221 or other 0x40 chip.
    return static_cast<uint16_t>(regRead16BE(REG_DIE_ID)) == DIE_ID;
  }

  void update() override {
    int16_t shunt = regRead16BE(REG_SHUNT_V);  // signed
    int16_t busr = regRead16BE(REG_BUS_V);     // 0..~28800, fits int16
    _shuntV = shunt * SHUNT_LSB_V;
    _busV = busr * BUS_LSB_V;
    _current = _shuntV / R_SHUNT;
    _power = _busV * _current;
  }

  void toJson(JsonObject& o) const override {
    o["bus_v"] = _busV;
    o["current_a"] = _current;
    o["power_w"] = _power;
    o["shunt_mv"] = _shuntV * 1000.0f;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"bus_v", _busV, "V"};
    b[1] = {"current_a", _current, "A"};
    b[2] = {"power_w", _power, "W"};
    b[3] = {"shunt_mv", _shuntV * 1000.0f, "mV"};
    n = 4;
  }

 private:
  float _busV = 0, _shuntV = 0, _current = 0, _power = 0;
};
