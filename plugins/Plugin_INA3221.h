#pragma once
// ============================================================
//  Plugin_INA3221.h  –  INA3221 3-channel power monitor
//
//  Built-in on the M5Stack Core2 v1.1 (the AXP2101 + INA3221
//  power solution) at I2C address 0x40 on the internal bus.  The
//  INA3221 is a Texas Instruments 3-channel bus-voltage / shunt
//  monitor; on the Core2 v1.1 channel 1 carries the battery.
//
//  ── Detection ────────────────────────────────────────────────
//  begin() positively identifies the chip by reading its die-ID
//  register (0xFF == 0x3220).  This matters: 0x40 is also the
//  default address of a PCA9685 (the Servo2 module), so a bare
//  I2C ACK is not proof.  Plugin_INA3221 is registered BEFORE
//  Plugin_SERVO2 and only binds on a real die-ID match — so a
//  Core2 v1.1 binds the INA3221 here, while a genuine Servo2 on a
//  board with no INA3221 still falls through to Plugin_SERVO2.
//  (This replaces the old "reserve 0x40" scan hack — the address
//  is now handled by a real, self-identifying driver.)
//
//  ── Registers (TI INA3221, big-endian 16-bit) ───────────────
//    0x01 / 0x03 / 0x05   Shunt voltage ch 1/2/3 (signed, 40 µV LSB)
//    0x02 / 0x04 / 0x06   Bus   voltage ch 1/2/3 (8 mV LSB)
//    0xFF                 Die ID = 0x3220
//  Bus and shunt voltages are absolute measurements — accurate
//  with no board-specific calibration.  Turning a shunt voltage
//  into a current needs the board's shunt resistance, which M5Stack
//  does not publish; rather than guess, this plugin reports shunt
//  voltage in mV directly, and takes the BATTERY current from
//  M5Unified's M5.Power.getBatteryCurrent() — which M5Stack already
//  calibrates against this very INA3221 on the Core2 v1.1.
//
//  Read-only.  Internal bus.  Builtin mount.
// ============================================================
#include "../src/IDevice.h"
#include <M5Unified.h>

class Plugin_INA3221 : public IDevice {
 public:
  static constexpr uint8_t REG_SHUNT_CH1 = 0x01;  // +2 per channel
  static constexpr uint8_t REG_BUS_CH1 = 0x02;    // +2 per channel
  static constexpr uint8_t REG_DIE_ID = 0xFF;
  static constexpr int16_t INA3221_DIE = 0x3220;

  const char* name() const override { return "INA3221 Power Monitor"; }
  const char* slug() const override { return "ina3221"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x40;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Builtin; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Positive ID — only a real INA3221 answers 0x3220 at 0xFF.
    // Anything else at 0x40 (e.g. a PCA9685 / Servo2) is rejected,
    // so the scan moves on and Plugin_SERVO2 can claim it instead.
    return regRead16BE(REG_DIE_ID) == INA3221_DIE;
  }

  void update() override {
    for (uint8_t ch = 0; ch < 3; ch++) {
      _bus[ch] = _busV(regRead16BE(REG_BUS_CH1 + ch * 2));
      _shunt[ch] = _shuntMv(regRead16BE(REG_SHUNT_CH1 + ch * 2));
    }
    // Battery current — M5Unified reads it from this INA3221
    // (channel 1) with M5Stack's own shunt calibration.  Signed:
    // positive = charging, negative = discharging.
    _batMa = static_cast<float>(M5.Power.getBatteryCurrent());
  }

  void toJson(JsonObject& o) const override {
    o["bat_mA"] = _batMa;
    o["ch1_bus_V"] = _bus[0];
    o["ch2_bus_V"] = _bus[1];
    o["ch3_bus_V"] = _bus[2];
    o["ch1_shunt_mV"] = _shunt[0];
    o["ch2_shunt_mV"] = _shunt[1];
    o["ch3_shunt_mV"] = _shunt[2];
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"bat_current", _batMa, "mA"};
    b[1] = {"ch1_bus", _bus[0], "V"};
    b[2] = {"ch2_bus", _bus[1], "V"};
    b[3] = {"ch3_bus", _bus[2], "V"};
    n = 4;
  }

 private:
  float _bus[3] = {0, 0, 0};    // bus voltage per channel (V)
  float _shunt[3] = {0, 0, 0};  // shunt voltage per channel (mV)
  float _batMa = 0;             // battery current (mA, signed)

  // Bus-voltage register → volts.  13-bit value in bits [15:3],
  // 8 mV per LSB.
  static float _busV(int16_t raw) { return (raw >> 3) * 0.008f; }
  // Shunt-voltage register → millivolts.  Signed 13-bit value in
  // bits [15:3], 40 µV per LSB.
  static float _shuntMv(int16_t raw) { return (raw >> 3) * 0.04f; }
};
