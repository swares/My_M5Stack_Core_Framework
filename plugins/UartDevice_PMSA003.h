#pragma once
// ============================================================
//  UartDevice_PMSA003.h  –  M5Stack PM2.5 Air Quality Module
//                            (PMSA003)                       [UART]
//
//  A Plantower PMSA003 laser particulate sensor.  In its default
//  "active" mode it streams a fixed 32-byte measurement frame over
//  UART roughly once a second — no polling, no commands needed, so
//  this device is a pure passive parser (no library required).
//
//      fw.addPlugin(new UartDevice_PMSA003(Serial2, 9600));
//
//  Port-C RX/TX pins are auto-resolved per board (see IUartDevice);
//  only the sensor's TX → host RX line carries data — the host
//  never has to talk back.
//
//  ── Frame format (verified — Plantower PMS digital protocol) ──
//   32 bytes; every 16-bit field is big-endian (high byte first):
//     0      0x42            start byte 1
//     1      0x4D            start byte 2
//     2-3    frame length    (= 2*13 + 2 = 28)
//     4-5    PM1.0  (CF=1, "standard particle")        ug/m3
//     6-7    PM2.5  (CF=1)
//     8-9    PM10   (CF=1)
//     10-11  PM1.0  (atmospheric environment)          ug/m3
//     12-13  PM2.5  (atmospheric environment)
//     14-15  PM10   (atmospheric environment)
//     16-27  particle counts per 0.1 L for six size bins
//     28-29  reserved
//     30-31  checksum = unsigned sum of bytes 0..29
//   The "atmospheric environment" PM values (bytes 10-15) are the
//   ones intended for ambient air-quality reporting, so they are
//   surfaced as pm1_0 / pm2_5 / pm10.  The CF=1 "standard" values
//   are also published in /api/pmsa003 (toJson) as *_cf.
//
//  Readings:  pm1_0, pm2_5, pm10 (ug/m3).
// ============================================================
#include "../src/IUartDevice.h"

class UartDevice_PMSA003 : public IUartDevice {
 public:
  UartDevice_PMSA003(HardwareSerial& port, uint32_t baud = 9600,
                     int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "PM2.5 Air Quality (PMSA003)"; }
  const char* slug() const override { return "pmsa003"; }

  // The sensor streams continuously — drain the UART every loop so
  // the RX buffer never overflows and drops a frame.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    while (_port->available()) {
      uint8_t b = static_cast<uint8_t>(_port->read());
      if (_idx == 0) {  // hunting for start byte 1
        if (b == 0x42)
          _buf[_idx++] = b;
      } else if (_idx == 1) {  // start byte 2 must follow
        if (b == 0x4D)
          _buf[_idx++] = b;
        else if (b != 0x42)
          _idx = 0;  // not 0x4D and not a fresh
                     // 0x42 — false start, resync
        // (b == 0x42 → keep _idx at 1: treat it as a new start byte 1)
      } else {
        _buf[_idx++] = b;
        if (_idx >= FRAME_LEN) {
          _parseFrame();
          _idx = 0;
        }
      }
    }
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["pm1_0"] = _pm1_0;
    o["pm2_5"] = _pm2_5;
    o["pm10"] = _pm10;
    o["pm1_0_cf"] = _pm1_0_cf;
    o["pm2_5_cf"] = _pm2_5_cf;
    o["pm10_cf"] = _pm10_cf;
    o["frames"] = _frames;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"pm1_0", static_cast<float>(_pm1_0), "ug/m3"};
    b[1] = {"pm2_5", static_cast<float>(_pm2_5), "ug/m3"};
    b[2] = {"pm10", static_cast<float>(_pm10), "ug/m3"};
    n = 3;
  }

 private:
  static constexpr uint8_t FRAME_LEN = 32;

  uint8_t _buf[FRAME_LEN] = {0};
  uint8_t _idx = 0;                            // bytes assembled so far
  uint16_t _pm1_0 = 0, _pm2_5 = 0, _pm10 = 0;  // atmospheric env.
  uint16_t _pm1_0_cf = 0, _pm2_5_cf = 0, _pm10_cf = 0;  // CF=1 standard
  uint32_t _frames = 0;  // valid frames since boot

  static uint16_t _be16(const uint8_t* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
  }

  // Validate one complete 32-byte frame and fold it into the
  // readings.  A bad checksum just drops the frame — the next one
  // is ~1 s away.
  void _parseFrame() {
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 30; i++)
      sum += _buf[i];
    if (sum != _be16(&_buf[30]))
      return;  // corrupt — discard
    _pm1_0_cf = _be16(&_buf[4]);
    _pm2_5_cf = _be16(&_buf[6]);
    _pm10_cf = _be16(&_buf[8]);
    _pm1_0 = _be16(&_buf[10]);
    _pm2_5 = _be16(&_buf[12]);
    _pm10 = _be16(&_buf[14]);
    _frames++;
  }
};
