#pragma once
// ============================================================
//  Plugin_ADS1115.h  –  M5Stack Ammeter / Voltmeter Unit (ADS1115)
//
//  A 4-channel 16-bit delta-sigma ADC.  Both the Ammeter Unit and
//  the Voltmeter Unit are built on a TI ADS1115 — this plugin reads
//  the raw per-channel voltages; turning those into amps or volts
//  needs the unit's own shunt / divider ratio (see SCALE below).
//
//  ── I2C address ──────────────────────────────────────────────
//  0x48 by default (ADDR->GND); the ADS1115 also answers 0x49/0x4A/
//  0x4B per its ADDR pin.  ⚠ No ID register — at 0x48 it cannot be
//  told apart from an ADS1110 — so register only the unit you have.
//
//  ── Protocol (verified — TI ADS1115 datasheet) ───────────────
//  16-bit registers: 0x00 conversion, 0x01 config.  Each channel is
//  read single-shot: write config (OS=1, MUX = single-ended ch,
//  PGA +/-4.096 V, single-shot, 128 SPS), wait, read conversion.
//  Voltage = code * 4.096 V / 32768.
//
//  Readings:  ch0..ch3 voltage (V).  ch0_scaled also applies SCALE
//  as a quick engineering value — set SCALE to your unit's ratio.
// ============================================================
#include "../src/IDevice.h"
#include "../src/Config.h"  // POLL_MS — sweep cadence for the fastPoll state machine

class Plugin_ADS1115 : public IDevice {
 public:
  static constexpr uint8_t CHANNELS = 4;
  static constexpr uint8_t REG_CONV = 0x00;
  static constexpr uint8_t REG_CONFIG = 0x01;
  // PGA +/-4.096 V -> 4.096/32768 V per code.
  static constexpr float LSB_V = 4.096f / 32768.0f;
  // Optional convenience: ch0 voltage * SCALE is published as
  // ch0_scaled.  1.0 = pass-through.  Set this to the Voltmeter's
  // divider ratio, or the Ammeter's (1 / shunt-ohms), to read volts
  // or amps directly — the value is unit-specific, so verify it.
  static constexpr float SCALE = 1.0f;

  const char* name() const override {
    return "Ammeter/Voltmeter Unit (ADS1115)";
  }
  const char* slug() const override { return "ads1115"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x48;
    n = 1;
  }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Presence probe — a bare addressed write the ADS1115 must ACK.
    bus->beginTransmission(addr);
    return bus->endTransmission() == 0;
  }

  // Conversions are non-blocking.  The ADS1115 needs ~8 ms per single-
  // shot conversion; doing all four channels in update() meant blocking
  // the cooperative loop for ~4×9 ms every poll.  Instead a small state
  // machine in fastPoll() starts ONE channel's conversion, waits out the
  // ~9 ms WITHOUT blocking (it just returns and is re-entered next loop),
  // reads it, then steps to the next channel.  A full 4-channel sweep
  // completes in a few tens of ms of wall-clock and repeats every
  // POLL_MS, so the data rate matches the old code with zero loop stall.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    uint32_t now = millis();
    switch (_st) {
      case IDLE:
        // Begin a fresh sweep once a poll interval has elapsed.
        if (now - _lastSweep >= POLL_MS) {
          _ch = 0;
          _st = START;
        }
        return;
      case START:
        if (_startConv(_ch)) {
          _convAt = now;
          _st = WAIT;
        } else {
          _nextChannel(now);  // bus error — keep prior value, move on
        }
        return;
      case WAIT:
        if (now - _convAt < CONV_MS) return;  // conversion still running
        _v[_ch] = _readConv(_ch);
        _nextChannel(now);
        return;
    }
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    for (uint8_t ch = 0; ch < CHANNELS; ch++)
      o[String("ch") + ch] = _v[ch];
    o["ch0_scaled"] = _v[0] * SCALE;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* k[CHANNELS] = {"ch0", "ch1", "ch2", "ch3"};
    for (uint8_t ch = 0; ch < CHANNELS; ch++)
      b[ch] = {k[ch], _v[ch], "V"};
    n = CHANNELS;
  }

 private:
  float _v[CHANNELS] = {0};

  // ── Non-blocking conversion state machine (see fastPoll) ──────
  enum St : uint8_t { IDLE = 0, START, WAIT };
  St       _st = IDLE;
  uint8_t  _ch = 0;          // channel currently being swept
  uint32_t _convAt = 0;      // millis() the active conversion was started
  uint32_t _lastSweep = 0;   // millis() the last full sweep finished
  static constexpr uint32_t CONV_MS = 9;  // 128 SPS -> ~8 ms convert + margin

  // Advance to the next channel, or finish the sweep.
  void _nextChannel(uint32_t now) {
    if (++_ch >= CHANNELS) {
      _lastSweep = now;
      _st = IDLE;
    } else {
      _st = START;  // START is serviced on the next fastPoll() entry
    }
  }

  // Kick off a single-shot conversion on one single-ended channel.
  // Returns false (so the caller skips the channel) on a bus error.
  bool _startConv(uint8_t ch) {
    // config: OS=1 | MUX=100+ch (single-ended) | PGA=001 (+/-4.096V)
    //         | MODE=1 (single-shot) | DR=100 (128 SPS) | comp off
    uint16_t cfg = 0x8000 | (static_cast<uint16_t>(0x04 | ch) << 12) |
                   0x0200 | 0x0100 | 0x0080 | 0x0003;
    bus->beginTransmission(addr);
    bus->write(REG_CONFIG);
    bus->write(static_cast<uint8_t>(cfg >> 8));
    bus->write(static_cast<uint8_t>(cfg & 0xFF));
    return bus->endTransmission() == 0;
  }

  // Read the finished conversion -> volts.  On any bus error the
  // channel's previous value is kept.
  float _readConv(uint8_t ch) {
    bus->beginTransmission(addr);
    bus->write(REG_CONV);
    if (bus->endTransmission(false) != 0)
      return _v[ch];
    if (bus->requestFrom(static_cast<int>(addr), 2) != 2)
      return _v[ch];
    uint8_t hi = bus->read();
    uint8_t lo = bus->read();
    int16_t raw = static_cast<int16_t>((hi << 8) | lo);
    return raw * LSB_V;
  }
};
