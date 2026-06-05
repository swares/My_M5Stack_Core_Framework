#pragma once
// ============================================================
//  Plugin_SERVO2.h  –  M5Stack Module13.2 Servo2 (16-channel)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  I2C address 0x40 (default).  Built around a PCA9685 16-channel
//  12-bit PWM controller; the module exposes all 16 channels as
//  servo outputs (per the M5Stack Module13.2 Servo2 datasheet).
//
//  The Servo2's I2C address is DIP-switch selectable across
//  0x40-0x47, so several modules can be stacked.  This plugin
//  binds the default 0x40.  0x43 is deliberately NOT claimed by
//  default — it belongs to the 8Angle module; if you have moved
//  the DIP switch, set the address in i2cAddresses() to match.
//
//  CONTROLLABLE device.  Drive it via the Web API only:
//    GET /api/servo2/set?ch0=1500      channel 0 → 1500 µs pulse
//    GET /api/servo2/set?deg3=90       channel 3 → 90° (→ 1500 µs)
//    GET /api/servo2/set?ch0=1000&ch15=2000   several at once
//  Every parameter is validated:
//    chN  — N is 0-15, value is a pulse width 500-2500 µs
//    degN — N is 0-15, value is an angle 0-180° (→ 500-2500 µs)
//  Anything out of range is rejected; the servo is not moved.
//
//  PCA9685 register map (standard part — see NXP datasheet):
//    0x00 MODE1   0x01 MODE2   0xFE PRE_SCALE
//    0x06 + 4·ch  LEDn_ON_L / _ON_H / _OFF_L / _OFF_H
//  Output frequency is fixed at 50 Hz (20 ms) — standard hobby
//  servo timing — so 4096 PWM counts span the 20 ms period and
//  pulse_counts = µs · 4096 / 20000.
// ============================================================
#include "../src/IDevice.h"

class Plugin_SERVO2 : public IDevice {
 public:
  // PCA9685 has 16 PWM channels; the Servo2 module wires all 16.
  static constexpr uint8_t CHANNELS = 16;

  const char* name() const override { return "Servo2 Module (16ch)"; }
  const char* slug() const override { return "servo2"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x40;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;

    // MODE1: clear (wake, normal mode) — also confirms the chip
    // ACKs, which is our presence check.
    if (!regWrite(0x00, 0x00))
      return false;
    delay(5);

    // Set the prescaler for 50 Hz.  PRE_SCALE can only be written
    // while the chip is asleep (MODE1 SLEEP bit set).
    //   prescale = round(25 MHz / (4096 · 50 Hz)) − 1 = 121
    uint8_t oldmode = regRead8(0x00);
    regWrite(0x00, (oldmode & 0x7F) | 0x10);  // enter sleep
    regWrite(0xFE, 121);                      // prescale → 50 Hz
    regWrite(0x00, oldmode);                  // wake
    delay(5);
    // RESTART | AUTO-INCREMENT | ALLCALL — AI lets us write the
    // four LEDn registers of a channel in one I2C transaction.
    regWrite(0x00, oldmode | 0xA1);

    // Park every channel "full off" (no pulse) so nothing is
    // forced to a position the moment the module powers up.
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      _channelOff(ch);
      _us[ch] = 0;
    }
    return true;
  }

  void update() override {}  // output device — nothing to poll

  void toJson(JsonObject& o) const override {
    for (uint8_t ch = 0; ch < CHANNELS; ch++)
      o[String("ch") + ch] = _us[ch];  // 0 = parked/off
    o["freq_hz"] = 50;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* k[CHANNELS] = {
        "ch0", "ch1", "ch2",  "ch3",  "ch4",  "ch5",  "ch6",  "ch7",
        "ch8", "ch9", "ch10", "ch11", "ch12", "ch13", "ch14", "ch15"};
    for (uint8_t ch = 0; ch < CHANNELS; ch++)
      b[ch] = {k[ch], static_cast<float>(_us[ch]), "us"};
    n = CHANNELS;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // chN  — N is 0-15, value is microseconds (500-2500).
    int ch = _chanOf(param, "ch");
    if (ch >= 0) {
      if (!_allDigits(value))
        return false;
      int32_t us = value.toInt();
      if (us < 500 || us > 2500)
        return false;  // out of range
      return _setChannelUs(static_cast<uint8_t>(ch),
                           static_cast<uint16_t>(us));
    }
    // degN — N is 0-15, value is an angle 0-180.
    ch = _chanOf(param, "deg");
    if (ch >= 0) {
      if (!_allDigits(value))
        return false;
      int32_t deg = value.toInt();
      if (deg < 0 || deg > 180)
        return false;  // out of range
      // 0° → 500 µs, 180° → 2500 µs.
      uint16_t us = static_cast<uint16_t>(500 + (deg * 2000L) / 180);
      return _setChannelUs(static_cast<uint8_t>(ch), us);
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  //  One 0-180 degree slider per channel (the dashboard sends
  //  degN, which command() maps to a 500-2500 us pulse), plus a
  //  "center all" quick action.  A parked channel (_us == 0)
  //  reports 90 as a neutral starting position.
  void controlSchema(JsonArray& out) const override {
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("deg") + ch;
      c["label"] = String("Channel ") + ch;
      c["type"] = "slider";
      c["min"] = 0;
      c["max"] = 180;
      c["step"] = 1;
      c["unit"] = "deg";
      c["group"] = "Channels";
      int deg = 90;
      if (_us[ch])
        deg = static_cast<int>(
            ((static_cast<int32_t>(_us[ch]) - 500) * 180L) / 2000L);
      c["value"] = deg;
    }
    JsonObject ctr = out.add<JsonObject>();
    ctr["label"] = "Center all (90)";
    ctr["type"] = "button";
    ctr["group"] = "Quick actions";
    String q;
    for (uint8_t ch = 0; ch < CHANNELS; ch++) {
      if (ch)
        q += '&';
      q += "deg";
      q += ch;
      q += "=90";
    }
    ctr["query"] = q;
  }

 private:
  uint16_t _us[CHANNELS] = {0};  // last-set pulse width per channel

  // If `param` is `prefix` followed by a 1-2 digit decimal in the
  // range 0..CHANNELS-1, return that channel index; else -1.  This
  // accepts both single-digit ("ch7") and two-digit ("ch15") forms.
  static int _chanOf(const String& param, const char* prefix) {
    uint16_t plen = static_cast<uint16_t>(strlen(prefix));
    if (!param.startsWith(prefix))
      return -1;
    uint16_t len = param.length();
    if (len < plen + 1 || len > plen + 2)
      return -1;  // need 1-2 digits
    for (uint16_t i = plen; i < len; i++)
      if (!isDigit(param.charAt(i)))
        return -1;
    int32_t ch = param.substring(plen).toInt();
    return (ch >= 0 && ch < CHANNELS) ? static_cast<int>(ch) : -1;
  }
  static bool _allDigits(const String& v) { return cmd::allDigits(v); }

  // Write the four LEDn registers of one channel in a single
  // auto-incrementing transaction.
  bool _writeChannel(uint8_t ch, uint16_t on, uint16_t off) {
    bus->beginTransmission(addr);
    bus->write(0x06 + 4 * ch);
    bus->write(on & 0xFF);
    bus->write(on >> 8);
    bus->write(off & 0xFF);
    bus->write(off >> 8);
    return bus->endTransmission() == 0;
  }
  // "Full off": OFF register bit 12 set — output held low, no pulse.
  bool _channelOff(uint8_t ch) { return _writeChannel(ch, 0, 0x1000); }

  bool _setChannelUs(uint8_t ch, uint16_t us) {
    // 4096 counts span the 20 ms (50 Hz) period.
    uint16_t off =
        static_cast<uint16_t>((static_cast<uint32_t>(us) * 4096) / 20000);
    if (off > 4095)
      off = 4095;
    if (!_writeChannel(ch, 0, off))
      return false;
    _us[ch] = us;
    return true;
  }
};
