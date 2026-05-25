#pragma once
// ============================================================
//  Plugin_MultiGas.h  –  Grove Multichannel Gas Sensor V2
//
//  Seeed's Grove - Gas Sensor V2 (Multichannel): four independent
//  MEMS gas elements behind an onboard STM32F030 that exposes them
//  over I2C.  Each element is read as a raw 10-bit ADC count — the
//  sensor is, per Seeed, a QUALITATIVE detector (trend / relative
//  change), not a calibrated ppm instrument.
//
//   element   gas                     reading key
//   GM-102B   nitrogen dioxide (NO2)   no2
//   GM-302B   ethanol / C2H5OH         alcohol
//   GM-502B   volatile organics (VOC)  voc
//   GM-702B   carbon monoxide (CO)     co
//
//  ── Raw protocol (no external library) ───────────────────────
//  Command bytes and the read format are transcribed from Seeed's
//  open-source "Multichannel_Gas_GMXXX" library — the authoritative
//  driver for the STM32F030 firmware (the wiki ships only that
//  library, not a register table).  Per channel:
//    write 1 command byte  →  read back 4 bytes, assembled
//    little-endian into a uint32_t ADC count (0..1023).
//  A STOP separates the write and the read, exactly as the library
//  does it.  No init/handshake is needed — the STM32 powers all
//  four heaters at boot.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x08, fixed.  ⚠ 0x08 is ALSO used by Plugin_EARTH and
//  Plugin_FACES2 in this framework — only one device can sit there.
//  The STM32F030 has no ID register, so begin() cannot do a clean
//  WHO_AM_I; instead it does a HEURISTIC presence check — all four
//  channels must answer and return a plausible 10-bit value, and
//  not all-zero.  Because that heuristic is weaker than a real ID
//  check, the plugin is registered commented-out (opt-in); enable
//  it, before Plugin_EARTH, only on a bus that actually has this
//  unit.
//
//  ── Warmup ───────────────────────────────────────────────────
//  "Users MUST preheat the module before measuring."  The heaters
//  run from power-on, so warmup here is a pure timer: begin()
//  records the start, and `warming` stays 1 (with `warmup_s`
//  counting down) until WARMUP_MS elapses.  Readings are still
//  published during warmup — they are just not yet trustworthy.
//  Note a brand-new sensor also needs a long initial burn-in
//  (GM-302B datasheet: "preheat time less than 48 hrs"); that
//  cannot be enforced in firmware and is left to the operator.
//
//  Readings:  no2, alcohol, voc, co  (raw ADC, 0-1023),
//             warming (1 = still preheating).
// ============================================================
#include "../src/IDevice.h"

class Plugin_MultiGas : public IDevice {
 public:
  // Per-channel command bytes (Seeed Multichannel_Gas_GMXXX).
  static constexpr uint8_t CMD_NO2 = 0x01;         // GM-102B
  static constexpr uint8_t CMD_ALCOHOL = 0x03;     // GM-302B
  static constexpr uint8_t CMD_VOC = 0x05;         // GM-502B
  static constexpr uint8_t CMD_CO = 0x07;          // GM-702B
  static constexpr uint16_t ADC_MAX = 1023;        // 10-bit onboard ADC
  static constexpr uint32_t WARMUP_MS = 180000UL;  // 3-min preheat

  const char* name() const override {
    return "Multichannel Gas Unit V2 (GM-x02B)";
  }
  const char* slug() const override { return "multigas"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x08;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Heuristic presence check — no ID register on the STM32F030.
    // Every channel must answer, stay inside the 10-bit ADC range
    // (small margin), and the four must not be uniformly zero.
    uint32_t v[4];
    const uint8_t cmd[4] = {CMD_NO2, CMD_ALCOHOL, CMD_VOC, CMD_CO};
    bool anyNonZero = false;
    for (uint8_t i = 0; i < 4; i++) {
      if (!_readChannel(cmd[i], v[i]))
        return false;  // no answer
      if (v[i] > static_cast<uint32_t>(ADC_MAX) + 64)
        return false;  // out of range
      if (v[i] != 0)
        anyNonZero = true;
    }
    if (!anyNonZero)
      return false;  // all-zero → not it
    _no2 = v[0];
    _alcohol = v[1];
    _voc = v[2];
    _co = v[3];
    _startMs = millis();
    return true;
  }

  void update() override {
    uint32_t v;
    if (_readChannel(CMD_NO2, v))
      _no2 = static_cast<uint16_t>(v);
    if (_readChannel(CMD_ALCOHOL, v))
      _alcohol = static_cast<uint16_t>(v);
    if (_readChannel(CMD_VOC, v))
      _voc = static_cast<uint16_t>(v);
    if (_readChannel(CMD_CO, v))
      _co = static_cast<uint16_t>(v);
  }

  void toJson(JsonObject& o) const override {
    o["no2"] = _no2;
    o["alcohol"] = _alcohol;
    o["voc"] = _voc;
    o["co"] = _co;
    o["warming"] = _warming() ? 1 : 0;
    o["warmup_s"] = _warmupRemainingS();
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"no2", static_cast<float>(_no2), ""};
    b[1] = {"alcohol", static_cast<float>(_alcohol), ""};
    b[2] = {"voc", static_cast<float>(_voc), ""};
    b[3] = {"co", static_cast<float>(_co), ""};
    b[4] = {"warming", static_cast<float>(_warming() ? 1 : 0), ""};
    n = 5;
  }

 private:
  uint16_t _no2 = 0, _alcohol = 0, _voc = 0, _co = 0;
  uint32_t _startMs = 0;

  bool _warming() const { return (millis() - _startMs) < WARMUP_MS; }
  uint16_t _warmupRemainingS() const {
    uint32_t e = millis() - _startMs;
    return (e >= WARMUP_MS) ? 0 : static_cast<uint16_t>((WARMUP_MS - e) / 1000);
  }

  // One channel: write the command byte (STOP), then read 4 bytes
  // and assemble them little-endian — the Seeed library's format.
  bool _readChannel(uint8_t cmd, uint32_t& out) {
    bus->beginTransmission(addr);
    bus->write(cmd);
    if (bus->endTransmission() != 0)
      return false;  // NACK → absent
    if (bus->requestFrom(addr, static_cast<uint8_t>(4)) != 4)
      return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < 4; i++)
      v |= static_cast<uint32_t>(bus->read() & 0xFF) << (8 * i);
    out = v;
    return true;
  }
};
