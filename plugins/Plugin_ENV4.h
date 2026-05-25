#pragma once
// ============================================================
//  Plugin_ENV4.h  –  M5Stack ENV IV Unit
//  SHT40  (temp/humidity)  0x44
//  BMP280 (pressure/temp)  0x76
//
//  ENV4 vs ENV3:
//    ENV III  →  SHT30 + QMP6988  (0x44 + 0x70)
//    ENV IV   →  SHT40 + BMP280   (0x44 + 0x76)
//  Both share 0x44.  Register Plugin_ENV4 BEFORE Plugin_ENV3
//  so the strict BMP280 chip-ID probe gets first refusal — if
//  the unit is actually ENV III, the chip-ID check fails and
//  Plugin_ENV3 takes over.
// ============================================================
#include "../src/IDevice.h"

class Plugin_ENV4 : public IDevice {
 public:
  const char* name() const override { return "ENV IV Unit"; }
  const char* slug() const override { return "env4"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x44;
    buf[1] = 0x76;
    n = 2;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;

    // ── BMP280 chip ID check (0xD0 → 0x58) ──────────────────
    uint8_t id = 0;
    if (!_readReg(0x76, 0xD0, &id, 1) || id != 0x58) {
      Serial.printf(
          "[ENV4] BMP280 chip-id mismatch at 0x76 (got 0x%02X, want 0x58)\n",
          id);
      return false;
    }

    // ── BMP280 factory calibration (regs 0x88..0x9F) ────────
    uint8_t cal[24];
    if (!_readReg(0x76, 0x88, cal, 24)) {
      Serial.println("[ENV4] BMP280 calib read failed");
      return false;
    }
    _t1 = static_cast<uint16_t>(cal[0] | (cal[1] << 8));
    _t2 = static_cast<int16_t>(cal[2] | (cal[3] << 8));
    _t3 = static_cast<int16_t>(cal[4] | (cal[5] << 8));
    _p1 = static_cast<uint16_t>(cal[6] | (cal[7] << 8));
    _p2 = static_cast<int16_t>(cal[8] | (cal[9] << 8));
    _p3 = static_cast<int16_t>(cal[10] | (cal[11] << 8));
    _p4 = static_cast<int16_t>(cal[12] | (cal[13] << 8));
    _p5 = static_cast<int16_t>(cal[14] | (cal[15] << 8));
    _p6 = static_cast<int16_t>(cal[16] | (cal[17] << 8));
    _p7 = static_cast<int16_t>(cal[18] | (cal[19] << 8));
    _p8 = static_cast<int16_t>(cal[20] | (cal[21] << 8));
    _p9 = static_cast<int16_t>(cal[22] | (cal[23] << 8));

    // ── BMP280 ctrl_meas: T×1, P×1, normal mode (0x27) ──────
    wire->beginTransmission(0x76);
    wire->write(0xF4);
    wire->write(0x27);
    wire->endTransmission();

    // ── SHT40 soft reset (0x94) to confirm presence ────────
    wire->beginTransmission(0x44);
    wire->write(0x94);
    uint8_t err = wire->endTransmission();
    if (err != 0) {
      Serial.printf("[ENV4] SHT40 soft-reset NACK at 0x44 (err=%u)\n", err);
      return false;
    }
    delay(2);  // SHT40 power-up after reset
    return true;
  }

  void update() override {
    _readSHT40();
    _readBMP280();
  }

  void toJson(JsonObject& o) const override {
    o["temp"] = _temp;
    o["temp_unit"] = "°C";
    o["humidity"] = _hum;
    o["humidity_unit"] = "%";
    o["pressure"] = _hpa;
    o["pressure_unit"] = "hPa";
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"temp", _temp, "°C"};
    b[1] = {"humid", _hum, "%"};
    b[2] = {"press", _hpa, "hPa"};
    n = 3;
  }

 private:
  float _temp = 0, _hum = 0, _hpa = 0;
  uint16_t _t1 = 0, _p1 = 0;
  int16_t _t2 = 0, _t3 = 0;
  int16_t _p2 = 0, _p3 = 0, _p4 = 0, _p5 = 0, _p6 = 0, _p7 = 0, _p8 = 0,
          _p9 = 0;

  bool _readReg(uint8_t dev, uint8_t reg, uint8_t* dst, uint8_t n) {
    bus->beginTransmission(dev);
    bus->write(reg);
    if (bus->endTransmission(false) != 0)
      return false;
    uint8_t got = bus->requestFrom(static_cast<int>(dev), static_cast<int>(n));
    if (got != n)
      return false;
    for (uint8_t i = 0; i < n; i++)
      dst[i] = bus->read();
    return true;
  }

  void _readSHT40() {
    bus->beginTransmission(0x44);
    bus->write(0xFD);  // measure T & RH, high precision
    if (bus->endTransmission() != 0)
      return;
    delay(10);  // ~8.3 ms typ
    if (bus->requestFrom(0x44, 6) != 6)
      return;
    uint8_t b[6];
    for (auto& x : b)
      x = bus->read();
    uint16_t rt = (static_cast<uint16_t>(b[0]) << 8) | b[1];
    uint16_t rh = (static_cast<uint16_t>(b[3]) << 8) | b[4];
    _temp = -45.0f + 175.0f * rt / 65535.0f;
    _hum = -6.0f + 125.0f * rh / 65535.0f;
    if (_hum < 0)
      _hum = 0;
    if (_hum > 100)
      _hum = 100;
  }

  void _readBMP280() {
    uint8_t d[6] = {0};
    if (!_readReg(0x76, 0xF7, d, 6))
      return;
    int32_t adc_P = (static_cast<int32_t>(d[0]) << 12) |
                    (static_cast<int32_t>(d[1]) << 4) | (d[2] >> 4);
    int32_t adc_T = (static_cast<int32_t>(d[3]) << 12) |
                    (static_cast<int32_t>(d[4]) << 4) | (d[5] >> 4);

    // ── Temperature compensation (BMP280 datasheet §3.11.3) ─
    int32_t v1 = ((((adc_T >> 3) - (static_cast<int32_t>(_t1) << 1))) *
                  static_cast<int32_t>(_t2)) >>
                 11;
    int32_t v2 =
        (((((adc_T >> 4) - static_cast<int32_t>(_t1)) *
           ((adc_T >> 4) - static_cast<int32_t>(_t1))) >>
          12) *
         static_cast<int32_t>(_t3)) >>
        14;
    int32_t t_fine = v1 + v2;
    // BMP280 also gives temperature; we keep SHT40's (more accurate for
    // ambient). float bmp_temp = ((t_fine * 5 + 128) >> 8) / 100.0f;

    // ── Pressure compensation ───────────────────────────────
    int64_t p1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t p2 = p1 * p1 * static_cast<int64_t>(_p6);
    p2 = p2 + ((p1 * static_cast<int64_t>(_p5)) << 17);
    p2 = p2 + (static_cast<int64_t>(_p4) << 35);
    p1 = ((p1 * p1 * static_cast<int64_t>(_p3)) >> 8) +
         ((p1 * static_cast<int64_t>(_p2)) << 12);
    p1 = ((static_cast<int64_t>(1) << 47) + p1) * static_cast<int64_t>(_p1) >>
         33;
    if (p1 == 0)
      return;  // divide-by-zero guard
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - p2) * 3125) / p1;
    p1 = (static_cast<int64_t>(_p9) * (p >> 13) * (p >> 13)) >> 25;
    p2 = (static_cast<int64_t>(_p8) * p) >> 19;
    p = ((p + p1 + p2) >> 8) + (static_cast<int64_t>(_p7) << 4);
    // p is in Q24.8 Pa → hPa
    _hpa = static_cast<float>(p) / 25600.0f;
  }
};
