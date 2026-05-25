#pragma once
// ============================================================
//  Plugin_MPU6886.h  –  M5Stack 6-Axis IMU Unit (MPU6886)
//
//  A Grove / Port-A 6-axis inertial unit: a TDK InvenSense MPU6886
//  3-axis accelerometer + 3-axis gyroscope.  This is the STANDALONE
//  UNIT — distinct from the MPU6886 some Cores carry on-board, which
//  the framework already reads through M5.Imu (Plugin_IMU).  A Port-A
//  unit is invisible to M5.Imu, so it needs its own plugin.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x68 — the MPU6886's AD0-low address (M5's IMU unit ties AD0 to
//  GND).  Bound on the EXTERNAL (Port-A) bus only, so it can never
//  be confused with a Core's on-board MPU6886 on the internal bus.
//
//  ── I2C register map (verified — InvenSense MPU6886 datasheet) ─
//    0x1B  GYRO_CONFIG    gyro full-scale select
//    0x1C  ACCEL_CONFIG   accel full-scale select
//    0x3B  ACCEL_XOUT_H   14 bytes: accel XYZ, temp, gyro XYZ
//                         (all int16, big-endian)
//    0x6B  PWR_MGMT_1     reset / sleep / clock select
//    0x75  WHO_AM_I       fixed 0x19 — positive chip ID
//  Configured for +/-8 g and +/-2000 deg/s at begin().
//
//  Readings:  accel_x/y/z (g), gyro_x/y/z (deg/s), temp (C).
// ============================================================
#include "../src/IDevice.h"

class Plugin_MPU6886 : public IDevice {
 public:
  // ── Register map (verified — MPU6886 datasheet) ───────────
  static constexpr uint8_t REG_GYRO_CONFIG = 0x1B;
  static constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
  static constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
  static constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
  static constexpr uint8_t REG_WHO_AM_I = 0x75;
  static constexpr uint8_t WHO_AM_I_VALUE = 0x19;

  // Full-scale ranges set below, and their per-LSB scale factors:
  //   ACCEL_CONFIG 0x10 -> +/-8 g      -> 8/32768 g per count
  //   GYRO_CONFIG  0x18 -> +/-2000 d/s -> 2000/32768 deg/s per count
  static constexpr float ACCEL_LSB = 8.0f / 32768.0f;
  static constexpr float GYRO_LSB = 2000.0f / 32768.0f;

  const char* name() const override { return "6-Axis IMU Unit (MPU6886)"; }
  const char* slug() const override { return "imu6886"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x68;
    n = 1;
  }
  // Port-A unit — external bus only, so a Core's on-board MPU6886
  // (internal bus, also 0x68) is never mistaken for this one.
  I2CBus preferredBus() const override { return I2CBus::External; }
  MountType mount() const override { return MountType::Pluggable; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    if (regRead8(REG_WHO_AM_I) != WHO_AM_I_VALUE)
      return false;                  // not an MPU6886
    regWrite(REG_PWR_MGMT_1, 0x80);  // device reset
    delay(100);
    regWrite(REG_PWR_MGMT_1, 0x01);  // wake, clock = auto/PLL
    delay(10);
    regWrite(REG_ACCEL_CONFIG, 0x10);  // +/-8 g
    regWrite(REG_GYRO_CONFIG, 0x18);   // +/-2000 deg/s
    delay(10);
    return true;
  }

  void update() override {
    uint8_t d[14];
    if (!regRead(REG_ACCEL_XOUT_H, d, 14))
      return;
    _ax = _i16(d, 0) * ACCEL_LSB;
    _ay = _i16(d, 2) * ACCEL_LSB;
    _az = _i16(d, 4) * ACCEL_LSB;
    _temp = _i16(d, 6) / 326.8f + 25.0f;  // MPU6886 temperature formula
    _gx = _i16(d, 8) * GYRO_LSB;
    _gy = _i16(d, 10) * GYRO_LSB;
    _gz = _i16(d, 12) * GYRO_LSB;
  }

  void toJson(JsonObject& o) const override {
    o["accel_x"] = _ax;
    o["accel_y"] = _ay;
    o["accel_z"] = _az;
    o["gyro_x"] = _gx;
    o["gyro_y"] = _gy;
    o["gyro_z"] = _gz;
    o["temp"] = _temp;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"accel_x", _ax, "g"};
    b[1] = {"accel_y", _ay, "g"};
    b[2] = {"accel_z", _az, "g"};
    b[3] = {"gyro_x", _gx, "deg/s"};
    b[4] = {"gyro_y", _gy, "deg/s"};
    b[5] = {"gyro_z", _gz, "deg/s"};
    b[6] = {"temp", _temp, "C"};
    n = 7;
  }

 private:
  float _ax = 0, _ay = 0, _az = 0;
  float _gx = 0, _gy = 0, _gz = 0;
  float _temp = 0;

  // Big-endian int16 from a byte buffer at offset i.
  static int16_t _i16(const uint8_t* d, uint8_t i) {
    return static_cast<int16_t>((d[i] << 8) | d[i + 1]);
  }
};
