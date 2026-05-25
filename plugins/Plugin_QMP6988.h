#pragma once
// ============================================================
//  Plugin_QMP6988.h  –  M5Stack Barometric Pressure Unit (QMP6988)
//
//  A QST QMP6988 digital barometric-pressure + temperature sensor —
//  the standalone Barometric Pressure 2 Unit, and the same chip
//  that ENV III pairs with an SHT30.  Unlike the rough `raw/100`
//  approximation in `Plugin_ENV3`, this plugin does the QMP6988's
//  FULL temperature/pressure compensation, so it reports real Pa.
//
//  ── I2C address ──────────────────────────────────────────────
//  0x70 (the SDO-low address M5 uses).  ⚠ 0x70 is also the base
//  address of PCA9548A I2C hubs — don't run this unit and a PaHUB
//  on the same bus segment.  Bound by a positive CHIP_ID check.
//
//  ── Register map (verified — QMP6988 datasheet Rev C, §4.4) ──
//    0xF7-0xF9  PRESS_TXD2/1/0   raw pressure    (24-bit)
//    0xFA-0xFC  TEMP_TXD2/1/0    raw temperature (24-bit)
//    0xF4  CTRL_MEAS   temp_avg[7:5] press_avg[4:2] power_mode[1:0]
//    0xF1  IIR_CNT     IIR filter coefficient
//    0xE0  RESET       write 0xE6 = soft reset
//    0xD1  CHIP_ID     fixed 0x5C
//    0xA0-0xB8  COE_*  25 bytes of OTP compensation coefficients
//
//  ── Compensation (verified — datasheet §4.3) ─────────────────
//    Dt = TXD2<<16 | TXD1<<8 | TXD0  - 2^23   (raw, signed)
//    Tr = a0 + a1·Dt + a2·Dt²                 (units of 1/256 °C)
//    Pr = b00 + bt1·Tr + bp1·Dp + b11·Dp·Tr + bt2·Tr²
//         + bp2·Dp² + b12·Dp·Tr² + b21·Dp²·Tr + bp3·Dp³   (Pa)
//  16-bit coefficients:  coeff = A + S·raw16 / 32767.
//  a0,b00:  20-bit signed OTP value, coeff = raw20 / 16.
//
//  Readings:  temp (°C), pressure (hPa).
// ============================================================
#include "../src/IDevice.h"

class Plugin_QMP6988 : public IDevice {
 public:
  static constexpr uint8_t REG_CHIP_ID = 0xD1;
  static constexpr uint8_t CHIP_ID = 0x5C;
  static constexpr uint8_t REG_RESET = 0xE0;
  static constexpr uint8_t REG_IIR = 0xF1;
  static constexpr uint8_t REG_CTRL = 0xF4;
  static constexpr uint8_t REG_PRESS_H =
      0xF7;                                 // 6 bytes: press[2..0], temp[2..0]
  static constexpr uint8_t REG_COE = 0xA0;  // 25-byte OTP block 0xA0..0xB8

  const char* name() const override {
    return "Barometric Pressure Unit (QMP6988)";
  }
  const char* slug() const override { return "baro"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x70;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    if (regRead8(REG_CHIP_ID) != CHIP_ID)
      return false;             // not a QMP6988
    regWrite(REG_RESET, 0xE6);  // soft reset
    delay(20);
    if (!_readCoefficients())
      return false;
    regWrite(REG_IIR, 0x02);  // IIR filter coefficient 4 — smooths noise
    // CTRL_MEAS 0x33: temperature 1x, pressure 8x oversampling,
    // power_mode = 11 (normal / continuous).
    regWrite(REG_CTRL, 0x33);
    delay(20);
    return true;
  }

  void update() override {
    uint8_t d[6];
    if (!regRead(REG_PRESS_H, d, 6))
      return;
    // 24-bit unsigned raw values, then offset by 2^23 (datasheet §4.3).
    int32_t dp = (static_cast<int32_t>(d[0]) << 16) |
                 (static_cast<int32_t>(d[1]) << 8) | d[2];
    int32_t dt = (static_cast<int32_t>(d[3]) << 16) |
                 (static_cast<int32_t>(d[4]) << 8) | d[5];
    float Dp = static_cast<float>(dp - 8388608);  // 2^23
    float Dt = static_cast<float>(dt - 8388608);

    float Tr = _a0 + _a1 * Dt + _a2 * Dt * Dt;
    _temp = Tr / 256.0f;

    float Pr = _b00 + _bt1 * Tr + _bp1 * Dp + _b11 * Dp * Tr + _bt2 * Tr * Tr +
               _bp2 * Dp * Dp + _b12 * Dp * Tr * Tr + _b21 * Dp * Dp * Tr +
               _bp3 * Dp * Dp * Dp;
    _hpa = Pr / 100.0f;
  }

  void toJson(JsonObject& o) const override {
    o["temp"] = _temp;
    o["pressure"] = _hpa;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"temp", _temp, "°C"};
    b[1] = {"pressure", _hpa, "hPa"};
    n = 2;
  }

 private:
  float _temp = 0, _hpa = 0;
  // Compensation coefficients, resolved once from OTP in begin().
  float _a0 = 0, _a1 = 0, _a2 = 0;
  float _b00 = 0, _bt1 = 0, _bt2 = 0, _bp1 = 0, _b11 = 0;
  float _bp2 = 0, _b12 = 0, _b21 = 0, _bp3 = 0;

  // Convert one 16-bit OTP coefficient: coeff = A + S·raw / 32767.
  static float _conv(int16_t raw, float A, float S) {
    return A + (S * static_cast<float>(raw)) / 32767.0f;
  }
  // Sign-extend a 20-bit value held in a uint32_t.
  static int32_t _ext20(uint32_t v) {
    return (v & 0x80000) ? static_cast<int32_t>(v | 0xFFF00000UL)
                         : static_cast<int32_t>(v);
  }

  // Read the 25-byte OTP coefficient block and resolve every term.
  // A/S conversion factors are from the datasheet §4.3 table.
  bool _readCoefficients() {
    uint8_t c[25];
    if (!regRead(REG_COE, c, 25))
      return false;  // 0xA0..0xB8
    // 20-bit offset coefficients a0 / b00 (raw / 16).
    uint32_t b00r = (static_cast<uint32_t>(c[0]) << 12) |
                    (static_cast<uint32_t>(c[1]) << 4) | ((c[24] >> 4) & 0x0F);
    uint32_t a0r = (static_cast<uint32_t>(c[18]) << 12) |
                   (static_cast<uint32_t>(c[19]) << 4) | (c[24] & 0x0F);
    _b00 = _ext20(b00r) / 16.0f;
    _a0 = _ext20(a0r) / 16.0f;
    // 16-bit coefficients — index pairs are [MSB, LSB] in 0xA0+ order.
    _bt1 = _conv(_i16(c, 2), 1.0e-1f, 9.1e-2f);
    _bt2 = _conv(_i16(c, 4), 1.2e-8f, 1.2e-6f);
    _bp1 = _conv(_i16(c, 6), 3.3e-2f, 1.9e-2f);
    _b11 = _conv(_i16(c, 8), 2.1e-7f, 1.4e-7f);
    _bp2 = _conv(_i16(c, 10), -6.3e-10f, 3.5e-10f);
    _b12 = _conv(_i16(c, 12), 2.9e-13f, 7.6e-13f);
    _b21 = _conv(_i16(c, 14), 2.1e-15f, 1.2e-14f);
    _bp3 = _conv(_i16(c, 16), 1.3e-16f, 7.9e-17f);
    _a1 = _conv(_i16(c, 20), -6.3e-3f, 4.3e-4f);
    _a2 = _conv(_i16(c, 22), -1.9e-11f, 1.2e-10f);
    return true;
  }
  // Big-endian int16 from the COE buffer at offset i (MSB then LSB).
  static int16_t _i16(const uint8_t* c, uint8_t i) {
    return static_cast<int16_t>((static_cast<uint16_t>(c[i]) << 8) | c[i + 1]);
  }
};
