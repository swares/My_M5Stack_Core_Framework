#pragma once
// ============================================================
//  PinDevice_Cotech.h  –  Cotech 36-7959 weather receiver  [non-I2C]
//
//  An OOK + Manchester 433.92 MHz receiver for the Cotech 36-7959
//  family of 8-in-1 outdoor weather stations.  Known clones include:
//      Sainlogic 8-in-1 (FT-0835 / WS019T)
//      SwitchDoc Labs FT020T
//      uctech FT020 / FT-0205
//
//  Hardware: any 433.92 MHz OOK receiver (RXB6 / RX470 / MX-RM-5V).
//      DATA pin -> any free GPIO that supports input + RMT capture.
//
//      fw.addPlugin(new PinDevice_Cotech(/*rxPin=*/26));
//
//  Protocol (per rtl_433 cotech_36_7959.c):
//      Modulation : OOK + Manchester, 500 us half-bit
//      Frame      : 112 bits (14 bytes), CRC8 poly=0x31 init=0xC0
//      Layout     : TYPE:4h ID:8h FLAGS:4h
//                   WIND:8d GUST:8d DIR:8d
//                   RAIN:12d FLAGS:4h
//                   TEMP:12d HUM:8d
//                   LIGHT:16d UV:8d
//                   CRC:8h
//      Each frame is transmitted twice with a ~5400 us gap; this
//      decoder accepts a frame only when two copies match, which
//      gives free error rejection on top of the CRC check.
//
//  Web API (controls):
//      GET /api/cotech/set?filter_id=3      lock onto sensor ID 3
//      GET /api/cotech/set?pair=1           latch the next ID seen
//      GET /api/cotech/set?reset_stats=1    zero rx_count + crc_err
// ============================================================
#include "../src/IPinDevice.h"
#include <Arduino.h>
#include <driver/rmt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>

// -- Tunables ------------------------------------------------
#ifndef COTECH_HALFBIT_US
#define COTECH_HALFBIT_US 500          // nominal half-bit width
#endif
#ifndef COTECH_HALFBIT_TOL_US
#define COTECH_HALFBIT_TOL_US 200      // +/- tolerance for short pulse
#endif
#ifndef COTECH_FULLBIT_TOL_US
#define COTECH_FULLBIT_TOL_US 280      // +/- tolerance for long pulse
#endif
#ifndef COTECH_RMT_IDLE_US
#define COTECH_RMT_IDLE_US 4000        // gap that ends a burst
#endif
#ifndef COTECH_RMT_FILTER_US
#define COTECH_RMT_FILTER_US 90        // hardware glitch filter
#endif
#ifndef COTECH_DUP_WINDOW_MS
#define COTECH_DUP_WINDOW_MS 30        // duplicate frame must arrive within
#endif
// Flip if your receiver inverts the line (idle-high vs idle-low):
// #define COTECH_MANCHESTER_INVERT 1

class PinDevice_Cotech : public IPinDevice {
 public:
  explicit PinDevice_Cotech(uint8_t rxPin,
                            uint8_t filterId = 0,
                            rmt_channel_t chan = RMT_CHANNEL_2)
      : _rxPin(rxPin), _filterId(filterId), _chan(chan) {}

  const char* name() const override { return "Cotech 36-7959 Weather"; }
  const char* slug() const override { return "cotech"; }
  bool controllable() const override { return true; }

  // 433 MHz pulses arrive asynchronously - drain the RMT ring buffer
  // every loop iteration, not at the 500 ms POLL_MS rate.
  bool wantsFastPoll() const override { return true; }

  bool beginPins() override {
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_RX((gpio_num_t)_rxPin, _chan);
    cfg.clk_div = 80;                  // 1 tick = 1 us
    cfg.rx_config.filter_en = true;
    cfg.rx_config.filter_ticks_thresh = COTECH_RMT_FILTER_US;
    cfg.rx_config.idle_threshold = COTECH_RMT_IDLE_US;

    if (rmt_config(&cfg) != ESP_OK) return false;
    if (rmt_driver_install(_chan, 1024, 0) != ESP_OK) return false;
    if (rmt_get_ringbuf_handle(_chan, &_rb) != ESP_OK) return false;
    if (rmt_rx_start(_chan, true) != ESP_OK) return false;
    return true;
  }

  void update() override {}            // 500 ms tick - nothing to do

  void fastPoll() override {
    size_t bytes = 0;
    rmt_item32_t* items = (rmt_item32_t*)
        xRingbufferReceive(_rb, &bytes, 0);
    if (!items) return;
    _decodeBurst(items, bytes / sizeof(rmt_item32_t));
    vRingbufferReturnItem(_rb, items);
  }

  void toJson(JsonObject& o) const override {
    o["have_data"] = _haveData;
    o["sensor_id"] = _sensorId;
    o["filter_id"] = _filterId;
    o["paired"]    = _filterId != 0 && !_pairArmed;
    if (_haveData) {
      o["temp_c"]     = _tempC;
      o["temp_f"]     = _tempC * 1.8f + 32.0f;
      o["humidity"]   = _humidity;
      o["wind_mps"]   = _windMps;
      o["gust_mps"]   = _gustMps;
      o["wind_dir"]   = _windDir;
      o["rain_mm"]    = _rainMm;
      o["uv"]         = _uv;
      o["light_lux"]  = _lightLux;
      o["batt_low"]   = _battLow;
      o["last_rx_ms"] = _lastRxMs;
      o["age_s"]      = (millis() - _lastRxMs) / 1000;
    }
    o["rx_count"] = _rxCount;
    o["crc_err"]  = _crcErr;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    n = 0;
    if (!_haveData) {
      b[n++] = {"rx_count", static_cast<float>(_rxCount), ""};
      b[n++] = {"crc_err",  static_cast<float>(_crcErr),  ""};
      return;
    }
    b[n++] = {"temp_c",    _tempC,                          "C"};
    b[n++] = {"humidity",  static_cast<float>(_humidity),   "%"};
    b[n++] = {"wind_mps",  _windMps,                        "m/s"};
    b[n++] = {"gust_mps",  _gustMps,                        "m/s"};
    b[n++] = {"wind_dir",  static_cast<float>(_windDir),    "deg"};
    b[n++] = {"rain_mm",   _rainMm,                         "mm"};
    b[n++] = {"uv",        static_cast<float>(_uv),         ""};
    b[n++] = {"light_lux", static_cast<float>(_lightLux),   "lux"};
    b[n++] = {"batt_low",  static_cast<float>(_battLow),    ""};
    b[n++] = {"sensor_id", static_cast<float>(_sensorId),   ""};
    b[n++] = {"rx_count",  static_cast<float>(_rxCount),    ""};
    b[n++] = {"crc_err",   static_cast<float>(_crcErr),     ""};
  }

  bool command(const String& param, const String& value) override {
    if (param == "filter_id") {
      long v = value.toInt();
      if (v < 0 || v > 255) return false;
      _filterId  = static_cast<uint8_t>(v);
      _pairArmed = false;
      return true;
    }
    if (param == "pair") {
      _pairArmed = true;
      return true;
    }
    if (param == "reset_stats") {
      _rxCount = 0;
      _crcErr  = 0;
      return true;
    }
    return false;
  }

  void controlSchema(JsonArray& out) const override {
    // Lock-onto-sensor-ID slider (0 = accept any)
    JsonObject f = out.add<JsonObject>();
    f["id"]    = "filter_id";
    f["label"] = "Sensor ID filter (0 = any)";
    f["type"]  = "slider";
    f["min"]   = 0;
    f["max"]   = 255;
    f["step"]  = 1;
    f["value"] = _filterId;

    // Pair button: latch the next valid sensor ID seen
    JsonObject p = out.add<JsonObject>();
    p["id"]    = "pair";
    p["label"] = "Pair (latch next ID seen)";
    p["type"]  = "button";
    p["query"] = "pair=1";

    // Reset counters
    JsonObject r = out.add<JsonObject>();
    r["id"]    = "reset_stats";
    r["label"] = "Reset rx_count / crc_err";
    r["type"]  = "button";
    r["query"] = "reset_stats=1";
  }

 private:
  // --------------- decode pipeline ---------------
  void _decodeBurst(const rmt_item32_t* items, size_t nItems) {
    // 1. Flatten the (dur0,lvl0,dur1,lvl1) tuples into a stream of
    //    half-bit symbols (each = the line level for one ~500 us cell).
    //    A ~1000 us pulse emits the same symbol twice.
    uint8_t halves[260];               // 112 bits = 224 halves; pad
    size_t hN = 0;

    for (size_t i = 0; i < nItems && hN < sizeof(halves) - 2; ++i) {
      const rmt_item32_t& it = items[i];
      if (!_pushPulse(halves, hN, it.duration0, it.level0)) break;
      if (it.duration1 == 0) break;    // RMT terminator
      if (!_pushPulse(halves, hN, it.duration1, it.level1)) break;
    }
    if (hN < 224) return;              // not enough symbols

    // 2. Manchester convention.  rtl_433 OOK_MC_ZEROBIT: low-then-high
    //    pair = bit 0, high-then-low = bit 1.  Flip via #define if your
    //    receiver inverts the line.
#ifndef COTECH_MANCHESTER_INVERT
    const uint8_t MC_0 = 0x01;
    const uint8_t MC_1 = 0x02;
#else
    const uint8_t MC_0 = 0x02;
    const uint8_t MC_1 = 0x01;
#endif

    // 3. Slide a 112-bit window across the half-bit stream looking for
    //    a CRC-good frame.  The burst may start with garbage or with
    //    one extra half-bit of preamble; sliding finds the alignment.
    const size_t maxStart = hN - 224;
    for (size_t s = 0; s <= maxStart; ++s) {
      uint8_t frame[14] = {0};
      if (!_pairToBytes(halves + s, frame, MC_0, MC_1)) continue;
      if (_crc8(frame, 13) != frame[13]) {
        if (s == 0) ++_crcErr;         // count once per burst
        continue;
      }
      _onValidFrame(frame);
      return;                          // first good frame wins
    }
  }

  // Append n copies (1 or 2) of `level` to the half-bit buffer.
  // false = pulse width unreasonable, abort the burst.
  bool _pushPulse(uint8_t* halves, size_t& hN,
                  uint16_t durUs, uint8_t level) {
    int copies;
    if (durUs < COTECH_HALFBIT_US - COTECH_HALFBIT_TOL_US)          return false;
    else if (durUs < COTECH_HALFBIT_US + COTECH_HALFBIT_TOL_US)     copies = 1;
    else if (durUs < 2 * COTECH_HALFBIT_US + COTECH_FULLBIT_TOL_US) copies = 2;
    else                                                            return false;

    while (copies-- && hN < 260) halves[hN++] = level ? 1 : 0;
    return true;
  }

  // Pair consecutive halves into bits.  Returns false on mis-alignment
  // (any 00 or 11 pair means we've slid off the Manchester boundary).
  bool _pairToBytes(const uint8_t* h, uint8_t* out,
                    uint8_t mc0, uint8_t mc1) {
    for (int b = 0; b < 112; ++b) {
      const uint8_t pair = (h[2 * b] << 1) | h[2 * b + 1];
      uint8_t v;
      if      (pair == mc0) v = 0;
      else if (pair == mc1) v = 1;
      else                  return false;
      const int byteIdx = b >> 3;
      const int bitIdx  = 7 - (b & 7);
      if (v) out[byteIdx] |= (1 << bitIdx);
      // else: caller zero-initialised the frame buffer, no clear needed
    }
    return true;
  }

  // CRC-8, poly=0x31, init=0xC0  (per rtl_433 cotech_36_7959).
  static uint8_t _crc8(const uint8_t* d, size_t n) {
    uint8_t crc = 0xC0;
    for (size_t i = 0; i < n; ++i) {
      crc ^= d[i];
      for (int j = 0; j < 8; ++j)
        crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                           : static_cast<uint8_t>(crc << 1);
    }
    return crc;
  }

  // --------------- frame interpretation ---------------
  //
  // Bit layout (bit 0 = MSB of byte 0):
  //
  //  [0..3]    TYPE       4 bits
  //  [4..11]   ID         8 bits  (changes on battery reset)
  //  [12]      BATT_LOW   1 bit   (1 = low)
  //  [13]      DIR_MSB    1 bit   (9th bit of wind direction)
  //  [14]      GUST_MSB   1 bit   (9th bit of gust)
  //  [15]      WIND_MSB   1 bit   (9th bit of avg wind)
  //  [16..23]  WIND_LO    8 bits  -> wind = ((WIND_MSB<<8)|WIND_LO)/10 m/s
  //  [24..31]  GUST_LO    8 bits  -> gust = ((GUST_MSB<<8)|GUST_LO)/10 m/s
  //  [32..39]  DIR_LO     8 bits  -> dir  =  (DIR_MSB<<8)|DIR_LO       deg
  //  [40..51]  RAIN       12 bits -> rain = RAIN * 0.3                 mm
  //  [52..55]  FLAGS2     4 bits  (reserved / unknown)
  //  [56..67]  TEMP       12 bits -> temp = (TEMP - 400) / 10.0        C
  //  [68..75]  HUM        8 bits  -> humidity %
  //  [76..91]  LIGHT      16 bits -> light in lux (raw value)
  //  [92..99]  UV         8 bits  -> UV index (raw)
  //  [100..107] CRC       8 bits
  //
  void _onValidFrame(const uint8_t* f) {
    ++_rxCount;

    const uint8_t  sensorId = ((f[0] & 0x0F) << 4) | (f[1] >> 4);
    const uint8_t  flags    =  f[1] & 0x0F;
    const bool     battLow  =  (flags & 0x08) != 0;
    const uint16_t dirMsb   =  (flags & 0x04) ? 0x100 : 0;
    const uint16_t gustMsb  =  (flags & 0x02) ? 0x100 : 0;
    const uint16_t windMsb  =  (flags & 0x01) ? 0x100 : 0;
    const uint16_t windRaw  =  windMsb | f[2];
    const uint16_t gustRaw  =  gustMsb | f[3];
    const uint16_t windDir  =  dirMsb  | f[4];
    const uint16_t rainRaw  =  ((uint16_t)f[5] << 4) | (f[6] >> 4);
    const uint16_t tempRaw  =  ((uint16_t)(f[7] & 0x0F) << 8) | f[8];
    const uint8_t  humidity =  f[9];
    const uint16_t lightRaw =  ((uint16_t)f[10] << 8) | f[11];
    const uint8_t  uvRaw    =  f[12];

    // Duplicate-frame guard: every transmission is sent twice ~5400 us
    // apart; only commit when the second copy matches the first.
    const uint32_t now = millis();
    const bool dupOk =
        _lastFrameMs != 0 &&
        (now - _lastFrameMs) <= COTECH_DUP_WINDOW_MS &&
        memcmp(f, _lastFrame, 14) == 0;
    memcpy(_lastFrame, f, 14);
    _lastFrameMs = now;
    if (!dupOk) return;

    // Pair / filter on the matched frame
    if (_pairArmed) {
      _filterId  = sensorId;
      _pairArmed = false;
    }
    if (_filterId != 0 && sensorId != _filterId) return;

    _sensorId = sensorId;
    _battLow  = battLow;
    _windMps  = windRaw * 0.1f;
    _gustMps  = gustRaw * 0.1f;
    _windDir  = windDir;
    _rainMm   = rainRaw * 0.3f;
    _tempC    = (static_cast<int>(tempRaw) - 400) * 0.1f;
    _humidity = humidity;
    _lightLux = lightRaw;
    _uv       = uvRaw;
    _lastRxMs = now;
    _haveData = true;
  }

  // -- members ---------------------------------------
  uint8_t         _rxPin;
  uint8_t         _filterId = 0;
  rmt_channel_t   _chan;
  RingbufHandle_t _rb       = nullptr;

  bool            _pairArmed = false;

  bool            _haveData  = false;
  float           _tempC     = 0;
  uint8_t         _humidity  = 0;
  float           _windMps   = 0;
  float           _gustMps   = 0;
  uint16_t        _windDir   = 0;
  float           _rainMm    = 0;
  uint8_t         _uv        = 0;
  uint16_t        _lightLux  = 0;
  bool            _battLow   = false;
  uint8_t         _sensorId  = 0;

  uint32_t        _rxCount   = 0;
  uint32_t        _crcErr    = 0;
  uint32_t        _lastRxMs  = 0;

  uint8_t         _lastFrame[14] = {0};
  uint32_t        _lastFrameMs   = 0;
};
