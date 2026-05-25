#pragma once
// ============================================================
//  Plugin_IMU.h  –  Built-in IMU on the host M5Stack board
//
//  Board-aware:
//    • CoreS3 : BMI270 (accel/gyro @ 0x68) + BMM150 (mag @ 0x10)
//    • Core2  : MPU6886 (accel/gyro @ 0x68)
//
//  Both chips are driven through M5Unified's M5.Imu wrapper, so
//  the read code is identical regardless of board.  The plugin
//  name() string is filled in at begin() time from BoardInfo.
//
//  Internal bus only.
// ============================================================
#include <cstdio>
#include "../src/IDevice.h"
#include "../src/BoardInfo.h"
#include <M5Unified.h>

class Plugin_IMU : public IDevice {
 public:
  const char* name() const override { return _displayName; }
  const char* slug() const override { return "imu"; }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Builtin; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    // BMI270 = 0x68 or 0x69 | BMM150 = 0x10 | MPU6886 = 0x68 or 0x69
    // Use the superset so both boards are covered.
    buf[0] = 0x68;
    buf[1] = 0x69;
    buf[2] = 0x10;
    n = 3;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;

    // Build a human-readable name from detected board info.
    const auto& bi = BoardInfo::detect();
    snprintf(_displayName, sizeof(_displayName), "IMU (%s)", bi.imuName);

    // IMU is already initialised by M5.begin(); confirm it is
    // present and enabled on either board.
    if (!M5.Imu.isEnabled())
      return false;
    _calibrateGyro();  // measure zero-rate bias — board must be still
    return true;
  }

  void update() override {
    M5.Imu.update();
    auto d = M5.Imu.getImuData();
    ax = d.accel.x;
    ay = d.accel.y;
    az = d.accel.z;
    // Subtract the zero-rate bias measured at boot.
    gx = d.gyro.x - _gxBias;
    gy = d.gyro.y - _gyBias;
    gz = d.gyro.z - _gzBias;
    // BMM150 (CoreS3) provides magnetometer; MPU6886 (Core2) does not.
    // If the underlying driver doesn't expose mag, these stay at zero.
    mx = d.mag.x;
    my = d.mag.y;
    mz = d.mag.z;
    _hasMag = (mx != 0.0f || my != 0.0f || mz != 0.0f);
  }

  void toJson(JsonObject& o) const override {
    o["accel_x"] = ax;
    o["accel_x_unit"] = "g";
    o["accel_y"] = ay;
    o["accel_y_unit"] = "g";
    o["accel_z"] = az;
    o["accel_z_unit"] = "g";
    o["gyro_x"] = gx;
    o["gyro_x_unit"] = "°/s";
    o["gyro_y"] = gy;
    o["gyro_y_unit"] = "°/s";
    o["gyro_z"] = gz;
    o["gyro_z_unit"] = "°/s";
    // Zero-rate bias removed from the gyro readings above.
    o["gyro_bias_x"] = _gxBias;
    o["gyro_bias_y"] = _gyBias;
    o["gyro_bias_z"] = _gzBias;
    if (_hasMag) {
      o["mag_x"] = mx;
      o["mag_x_unit"] = "uT";
      o["mag_y"] = my;
      o["mag_y_unit"] = "uT";
      o["mag_z"] = mz;
      o["mag_z_unit"] = "uT";
    }
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"accel_x", ax, "g"};
    b[1] = {"accel_y", ay, "g"};
    b[2] = {"accel_z", az, "g"};
    b[3] = {"gyro_x", gx, "°/s"};
    b[4] = {"gyro_y", gy, "°/s"};
    b[5] = {"gyro_z", gz, "°/s"};
    n = 6;
    if (_hasMag) {
      b[6] = {"mag_x", mx, "uT"};
      b[7] = {"mag_y", my, "uT"};
      b[8] = {"mag_z", mz, "uT"};
      n = 9;
    }
  }

 private:
  float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
  float mx = 0, my = 0, mz = 0;
  bool _hasMag = false;
  char _displayName[24] = "IMU";

  // Gyro zero-rate bias, measured once at begin().  The MPU6886 /
  // BMI270 gyros have a temperature- and unit-dependent offset that
  // would otherwise read tens of °/s on a perfectly still board.
  float _gxBias = 0, _gyBias = 0, _gzBias = 0;

  // Sample the gyro for ~1 second while the board is held still and
  // store the per-axis average as the zero-rate bias.  If the
  // readings vary too much (the board was disturbed mid-sample) the
  // average is not a valid bias — the calibration is skipped with a
  // warning, leaving the bias at zero.
  void _calibrateGyro() {
    constexpr uint8_t N = 100;           // samples
    constexpr uint16_t STEP_MS = 10;     // ~1 s total
    constexpr float MOVE_LIMIT = 20.0f;  // °/s spread ⇒ "moved"
    float sum[3] = {0, 0, 0};
    float lo[3] = {1e9f, 1e9f, 1e9f};
    float hi[3] = {-1e9f, -1e9f, -1e9f};
    for (uint8_t i = 0; i < N; i++) {
      M5.Imu.update();
      auto d = M5.Imu.getImuData();
      float g[3] = {d.gyro.x, d.gyro.y, d.gyro.z};
      for (uint8_t k = 0; k < 3; k++) {
        sum[k] += g[k];
        if (g[k] < lo[k])
          lo[k] = g[k];
        if (g[k] > hi[k])
          hi[k] = g[k];
      }
      delay(STEP_MS);
    }
    float spread = 0;
    for (uint8_t k = 0; k < 3; k++)
      if (hi[k] - lo[k] > spread)
        spread = hi[k] - lo[k];
    if (spread > MOVE_LIMIT) {
      Serial.printf(
          "[IMU] gyro calibration skipped — board moved "
          "(%.1f °/s spread).  Keep it still and reboot.\n",
          spread);
      _gxBias = _gyBias = _gzBias = 0;
      return;
    }
    _gxBias = sum[0] / N;
    _gyBias = sum[1] / N;
    _gzBias = sum[2] / N;
    Serial.printf("[IMU] gyro bias calibrated: x=%.2f y=%.2f z=%.2f °/s\n",
                  _gxBias, _gyBias, _gzBias);
  }
};
