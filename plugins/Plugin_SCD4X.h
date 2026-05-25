#pragma once
// ============================================================
//  Plugin_SCD4X.h  –  M5Stack CO2 Unit (Sensirion SCD40 / SCD41)
//
//  A photoacoustic NDIR CO2 sensor with on-board temperature and
//  humidity.  Covers BOTH the CO2 Unit (SCD40) and the CO2L Unit
//  (SCD41) — they share the I2C address, command set and data
//  format; only the SCD41's extra low-power modes differ, and this
//  plugin uses the common periodic-measurement mode.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x62 (fixed — not re-addressable).
//
//  ── Protocol (verified — Sensirion SCD4x datasheet) ──────────
//  Not a register device: the host sends a 16-bit command (big-
//  endian) and, for reads, clocks back N x (2 data bytes + 1 CRC).
//    0x21B1  start_periodic_measurement
//    0xEC05  read_measurement       -> 9 bytes: CO2, Temp, RH (+CRC each)
//    0xE4B8  get_data_ready_status  -> 3 bytes
//    0x3F86  stop_periodic_measurement
//  CRC-8: polynomial 0x31, init 0xFF.
//  Conversions:  CO2 = raw ppm;  T = -45 + 175*raw/65535 C;
//                RH = 100*raw/65535 %.
//
//  ⚠ Periodic mode yields a fresh sample only every ~5 s — the
//  readings hold their last value between updates, and the first
//  valid sample appears roughly 5 s after boot.
//
//  Readings:  co2 (ppm), temp (C), humidity (%).
// ============================================================
#include "../src/IDevice.h"

class Plugin_SCD4X : public IDevice {
 public:
  static constexpr uint16_t CMD_START_PERIODIC = 0x21B1;
  static constexpr uint16_t CMD_READ_MEAS = 0xEC05;
  static constexpr uint16_t CMD_DATA_READY = 0xE4B8;
  static constexpr uint16_t CMD_STOP_PERIODIC = 0x3F86;

  const char* name() const override { return "CO2 Unit (SCD4x)"; }
  const char* slug() const override { return "co2"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x62;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // A measurement may still be running from a previous boot — the
    // SCD4x rejects most commands while measuring, so stop it first
    // (this also serves as the ACK presence probe), then start a
    // fresh periodic measurement.
    if (!_sendCmd(CMD_STOP_PERIODIC))
      return false;
    delay(500);  // stop takes ~500 ms
    if (!_sendCmd(CMD_START_PERIODIC))
      return false;
    return true;
  }

  void update() override {
    if (!_dataReady())
      return;  // no new sample — keep last
    if (!_sendCmd(CMD_READ_MEAS))
      return;
    delay(2);
    if (bus->requestFrom(static_cast<int>(addr), 9) != 9)
      return;
    uint8_t d[9];
    for (uint8_t i = 0; i < 9; i++)
      d[i] = bus->read();
    // Validate each 2-byte word's CRC before trusting it.
    if (_crc8(&d[0]) != d[2] || _crc8(&d[3]) != d[5] || _crc8(&d[6]) != d[8])
      return;
    uint16_t co2 = (d[0] << 8) | d[1];
    uint16_t rt = (d[3] << 8) | d[4];
    uint16_t rh = (d[6] << 8) | d[7];
    _co2 = co2;
    _temp = -45.0f + 175.0f * rt / 65535.0f;
    _hum = 100.0f * rh / 65535.0f;
  }

  void toJson(JsonObject& o) const override {
    o["co2"] = _co2;
    o["temp"] = _temp;
    o["humidity"] = _hum;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"co2", static_cast<float>(_co2), "ppm"};
    b[1] = {"temp", _temp, "C"};
    b[2] = {"humidity", _hum, "%"};
    n = 3;
  }

 private:
  uint16_t _co2 = 0;
  float _temp = 0, _hum = 0;

  // Send a bare 16-bit command; true if the SCD4x ACKed.
  bool _sendCmd(uint16_t cmd) {
    bus->beginTransmission(addr);
    bus->write(static_cast<uint8_t>(cmd >> 8));
    bus->write(static_cast<uint8_t>(cmd & 0xFF));
    return bus->endTransmission() == 0;
  }

  // get_data_ready_status: bits 0-10 non-zero -> a sample is ready.
  bool _dataReady() {
    if (!_sendCmd(CMD_DATA_READY))
      return false;
    delay(1);
    if (bus->requestFrom(static_cast<int>(addr), 3) != 3)
      return false;
    uint8_t d[3];
    for (uint8_t i = 0; i < 3; i++)
      d[i] = bus->read();
    if (_crc8(&d[0]) != d[2])
      return false;
    return (((d[0] << 8) | d[1]) & 0x07FF) != 0;
  }

  // Sensirion CRC-8 over a 2-byte word: poly 0x31, init 0xFF.
  static uint8_t _crc8(const uint8_t* d) {
    uint8_t crc = 0xFF;
    for (uint8_t i = 0; i < 2; i++) {
      crc ^= d[i];
      for (uint8_t b = 0; b < 8; b++)
        crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                           : static_cast<uint8_t>(crc << 1);
    }
    return crc;
  }
};
