#pragma once
// ============================================================
//  Plugin_HM3301.h  –  Grove Laser PM2.5 Dust Sensor (HM3301)
//  HM3301 / HM330X laser particulate sensor             0x40
//
//  Seeed's "Grove - Laser PM2.5 Sensor (HM3301)" — a laser-scatter
//  dust sensor that reports PM1.0 / PM2.5 / PM10 mass concentrations
//  plus six particle-count bins.  It is an I2C device (the datasheet
//  writes the address as 0x80/0x81 = the 8-bit write/read forms; the
//  7-bit address used by Wire is 0x40).
//
//  ── Protocol ───────────────────────────────────────────────
//    • Once at init, write 0x88 to the device — this disables the
//      module's UART and selects I2C mode.
//    • Each read is a 29-byte frame (no register pointer — just a
//      requestFrom of 29 bytes).  Values are big-endian uint16:
//        [0..1]   reserved (frame header)
//        [2..3]   sensor number
//        [4..5]   PM1.0  std  (CF=1, "standard particulate")  µg/m³
//        [6..7]   PM2.5  std
//        [8..9]   PM10   std
//        [10..11] PM1.0  atmospheric  (the everyday number)    µg/m³
//        [12..13] PM2.5  atmospheric
//        [14..15] PM10   atmospheric
//        [16..27] particle counts in 0.3/0.5/1.0/2.5/5.0/10 µm
//                 bins, per 0.1 L of air
//        [28]     checksum = (sum of bytes 0..27) & 0xFF
//    Every read is checksum-validated; a bad/short frame is dropped
//    and the previous reading is kept (so the dashboard never shows
//    garbage on a glitched transaction).
//
//  ── Shared address note (0x40) ─────────────────────────────
//    0x40 is also used by INA226 / INA3221 / SERVO2.  The HM3301 has
//    no WHO_AM_I, so begin() detects it permissively — a successful,
//    checksum-valid 29-byte read.  REGISTER THIS *AFTER* the strict,
//    die-ID-gated 0x40 devices in setup() so they get first claim
//    (same strict-before-permissive ordering as TOF→COLOR at 0x29 and
//    HEART→ULTRASONIC at 0x57).
//
//  ── Readings ───────────────────────────────────────────────
//      pm2_5, pm10, pm1_0   atmospheric mass conc., µg/m³  (headline)
//    + JSON: *_std variants and the six particle-count bins (pc_*).
// ============================================================
#include "../src/IDevice.h"

class Plugin_HM3301 : public IDevice {
 public:
  const char* name() const override { return "Laser PM2.5 (HM3301)"; }
  const char* slug() const override { return "pm25"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x40;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Select I2C mode (disable the module's UART).  Harmless if the
    // board already powered up in I2C.
    wire->beginTransmission(addr);
    wire->write(0x88);
    if (wire->endTransmission() != 0) {
      Serial.println(F("[HM3301] begin: no ACK to 0x88 select — not present"));
      return false;
    }
    delay(40);
    // Detection = one checksum-valid 29-byte frame.  A missing device
    // (0xFF run) or any other chip at 0x40 fails the checksum, so this
    // only claims 0x40 when a real HM3301 answers.
    uint8_t f[29];
    if (!_readFrame(f)) {
      Serial.println(F("[HM3301] begin: no valid 29-byte frame — not an HM3301"));
      return false;
    }
    _parse(f);
    return true;
  }

  void update() override {
    uint8_t f[29];
    if (_readFrame(f))  // keep the previous reading on a bad/short frame
      _parse(f);
  }

  void toJson(JsonObject& o) const override {
    // Headline = atmospheric (everyday-environment) concentrations.
    o["pm2_5"] = _atm[1];
    o["pm2_5_unit"] = "µg/m³";
    o["pm10"] = _atm[2];
    o["pm10_unit"] = "µg/m³";
    o["pm1_0"] = _atm[0];
    o["pm1_0_unit"] = "µg/m³";
    // Standard-particulate (CF=1) variants.
    o["pm1_0_std"] = _std[0];
    o["pm2_5_std"] = _std[1];
    o["pm10_std"] = _std[2];
    // Particle-count bins (count per 0.1 L of air).
    o["pc_0_3"] = _pc[0];
    o["pc_0_5"] = _pc[1];
    o["pc_1_0"] = _pc[2];
    o["pc_2_5"] = _pc[3];
    o["pc_5_0"] = _pc[4];
    o["pc_10"]  = _pc[5];
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"pm2_5", static_cast<float>(_atm[1]), "µg/m³"};
    b[1] = {"pm10",  static_cast<float>(_atm[2]), "µg/m³"};
    b[2] = {"pm1_0", static_cast<float>(_atm[0]), "µg/m³"};
    n = 3;
  }

 private:
  uint16_t _std[3] = {0, 0, 0};  // PM1.0 / PM2.5 / PM10, CF=1 standard
  uint16_t _atm[3] = {0, 0, 0};  // PM1.0 / PM2.5 / PM10, atmospheric
  uint16_t _pc[6]  = {0, 0, 0, 0, 0, 0};  // 0.3/0.5/1.0/2.5/5.0/10 µm

  // Read one 29-byte frame and verify its checksum.  Returns false on
  // a short read or checksum mismatch (caller keeps the old values).
  bool _readFrame(uint8_t* f) const {
    if (bus->requestFrom(static_cast<int>(addr), 29) != 29)
      return false;
    for (uint8_t i = 0; i < 29; i++)
      f[i] = bus->read();
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 28; i++)
      sum += f[i];
    return sum == f[28];
  }

  static uint16_t be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
  }

  void _parse(const uint8_t* f) {
    _std[0] = be16(&f[4]);
    _std[1] = be16(&f[6]);
    _std[2] = be16(&f[8]);
    _atm[0] = be16(&f[10]);
    _atm[1] = be16(&f[12]);
    _atm[2] = be16(&f[14]);
    for (uint8_t i = 0; i < 6; i++)
      _pc[i] = be16(&f[16 + i * 2]);
  }
};
