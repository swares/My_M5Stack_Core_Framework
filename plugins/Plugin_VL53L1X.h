#pragma once
// ============================================================
//  Plugin_VL53L1X.h  –  M5Stack ToF4M Unit (VL53L1X)
//
//  ST VL53L1X laser time-of-flight ranging sensor — the longer-
//  range successor to the VL53L0X behind Plugin_TOF.  Measures
//  roughly 40–4000 mm.
//
//  ── Thin wrapper — needs the Pololu "VL53L1X" library ────────
//  The VL53L1X has no documented register map: ST ships its
//  ranging core as a large opaque firmware blob loaded at init,
//  so a raw transcription like Plugin_TOF (VL53L0X) is NOT
//  possible.  This plugin therefore drives the sensor THROUGH
//  Pololu's well-tested "VL53L1X" library — install it from the
//  Arduino Library Manager, then uncomment BOTH the #include of
//  this header and the matching registration line in the .ino.
//  This is the same opt-in pattern the UART devices (TinyGPSPlus,
//  M5Module-LLM) and Plugin_FAN (M5Module-Fan) use: the library
//  owns the verified init sequence, this plugin just adapts it
//  to the IDevice interface.
//
//      fw.addPlugin(new Plugin_VL53L1X());
//
//  ── I2C address ──────────────────────────────────────────────
//  0x29 (7-bit) — the fixed VL53L1X default.  ⚠ This is the SAME
//  address as Plugin_TOF (VL53L0X) and Plugin_COLOR; only one
//  device can sit on 0x29 at a time.  Register this plugin BEFORE
//  Plugin_TOF / Plugin_COLOR so its strict ID check runs first.
//  Pololu init() verifies the VL53L1X model ID (reg 0x010F reads
//  0xEACC) and returns false for any other chip, so a VL53L0X or
//  colour sensor at 0x29 is cleanly rejected — control then falls
//  through to whichever plugin actually matches.
//
//  ── Configuration ────────────────────────────────────────────
//  Long distance mode (full ~4 m range), 50 ms timing budget,
//  continuous ranging with a new sample every 50 ms.  Edit begin()
//  to trade range for robustness — Short mode reaches only ~1.3 m
//  but is far less sensitive to ambient light.
//
//  Readings:  dist (mm), valid (1 = range_status reports a good
//  measurement; 0 = clipped / low-signal / out-of-bounds).
// ============================================================
#include <VL53L1X.h>
#include "../src/IDevice.h"

class Plugin_VL53L1X : public IDevice {
 public:
  const char* name() const override { return "ToF4M Unit (VL53L1X)"; }
  const char* slug() const override { return "tof4m"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x29;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    _tof.setBus(wire);
    _tof.setTimeout(500);
    // init() checks the model ID (reg 0x010F == 0xEACC) and loads
    // ST's tuning blob; it returns false if no VL53L1X answers at
    // 0x29 — this doubles as the presence/ID check.
    if (!_tof.init())
      return false;
    _tof.setDistanceMode(VL53L1X::Long);
    _tof.setMeasurementTimingBudget(50000);  // 50 ms per measurement
    _tof.startContinuous(50);                // new sample every 50 ms
    return true;
  }

  void update() override {
    // Non-blocking: pull a sample only once the sensor flags one
    // ready, otherwise keep the previous reading.
    if (!_tof.dataReady())
      return;
    _dist = _tof.read(false);
    _valid = (_tof.ranging_data.range_status == VL53L1X::RangeValid);
  }

  void toJson(JsonObject& o) const override {
    o["distance"] = _dist;
    o["distance_unit"] = "mm";
    o["valid"] = _valid ? 1 : 0;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"dist", static_cast<float>(_dist), "mm"};
    b[1] = {"valid", static_cast<float>(_valid ? 1 : 0), ""};
    n = 2;
  }

 private:
  VL53L1X _tof;         // Pololu library driver instance
  uint16_t _dist = 0;   // last range, mm
  bool _valid = false;  // last sample's range_status was OK
};
