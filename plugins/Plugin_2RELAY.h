#pragma once
// ============================================================
//  Plugin_2RELAY.h  –  M5Stack Module13.2 2Relay  (SKU M124)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  STM32F030F4P6-based; two AC/DC relays rated 250 VAC @ 5 A.
//
//  ── I2C address ──────────────────────────────────────────────
//  This plugin binds 0x25 — the default address baked into
//  M5Stack's "M5Module-2Relay-13.2" Arduino library
//  (MODULE_2RELAY_ADDR = 0x25), confirmed by the boot scan on real
//  hardware.  (The product's docs page lists 0x26; that is a
//  copy-paste slip from the 4-Relay module's page.)  If your unit
//  has been re-addressed, change the value in i2cAddresses().
//  Because it is 0x25, this module does NOT clash with the 4-Relay
//  or Weight units, which are both at 0x26.
//
//  ── I2C register map (verified — M5Module-2Relay-13.2) ───────
//    0x00  RELAY 1   R/W  one byte: 0xFF = on, 0x00 = off
//    0x01  RELAY 2   R/W  one byte: 0xFF = on, 0x00 = off
//    0xFE  VERSION   R    firmware version (one byte)
//    0xFF  ADDR-CFG  W    write to re-address the module
//
//  ── ⚠ No repeated-start I2C ──────────────────────────────────
//  This plugin issues ONLY plain, STOP-terminated register writes
//  — the exact form M5Stack's library uses (writeBytes()).  It
//  deliberately performs NO register reads: the module's STM32F030
//  firmware is unreliable with the repeated-start framing that the
//  IDevice regRead() helper uses, and an earlier version that read
//  the firmware-version register in begin() made begin() fail on
//  hardware.  The 2Relay is a pure output device, so the relay
//  state mirror (_state) — seeded in begin(), updated by command()
//  — is authoritative and no read-back is needed.
//
//  CONTROLLABLE device.  Drive it via the Web API only:
//    GET /api/2relay/set?relay1=1          relay 1 on
//    GET /api/2relay/set?relay2=off        relay 2 off
//    GET /api/2relay/set?relays=3          both relays from a 0-3
//                                          bitmap (bit0=R1, bit1=R2)
//  Every parameter is validated; anything out of range is rejected
//  and the hardware is left untouched.
// ============================================================
#include "../src/IDevice.h"

class Plugin_2RELAY : public IDevice {
 public:
  // ── Register map (verified — M5Module-2Relay-13.2) ────────
  static constexpr uint8_t REG_RELAY = 0x00;  // 0x00 = R1, 0x01 = R2

  const char* name() const override { return "2-Relay Module"; }
  const char* slug() const override { return "2relay"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x25;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Drive both relays to a known-open state.  These are plain,
    // STOP-terminated register writes (the form M5Stack's library
    // uses); the first write's ACK doubles as the presence check.
    // No register reads here — see the "No repeated-start" note.
    return _writeState(0x00);
  }

  // Output device — write-driven, nothing to poll.  The _state
  // mirror is authoritative (see the header note on why we do not
  // read the relay registers back).
  void update() override {}

  void toJson(JsonObject& o) const override {
    o["relay1"] = (_state >> 0) & 1;
    o["relay2"] = (_state >> 1) & 1;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"relay1", static_cast<float>((_state >> 0) & 1), ""};
    b[1] = {"relay2", static_cast<float>((_state >> 1) & 1), ""};
    n = 2;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // relays — value is a 0-3 bitmap (bit0 = relay1, bit1 = relay2).
    // Checked BEFORE the indexed form so the trailing 's' isn't
    // mistaken for a relay index.
    if (param == "relays") {
      int v = _parseBits(value, 3);
      if (v < 0)
        return false;  // not 0-3
      return _writeState(static_cast<uint8_t>(v));
    }
    // relayN — N is 1-2, value is a boolean token.
    if (param.length() == 6 && param.startsWith("relay")) {
      char idxCh = param.charAt(5);
      if (idxCh < '1' || idxCh > '2')
        return false;  // bad index
      int on = _parseBool(value);
      if (on < 0)
        return false;  // bad value
      uint8_t bit = idxCh - '1';
      uint8_t s = _state;
      if (on)
        s |= (1 << bit);
      else
        s &= ~(1 << bit);
      return _writeState(s);
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  void controlSchema(JsonArray& out) const override {
    for (uint8_t i = 0; i < 2; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("relay") + (i + 1);
      c["label"] = String("Relay ") + (i + 1);
      c["type"] = "toggle";
      c["group"] = "Relays";
      c["value"] = (_state >> i) & 1;
    }
    JsonObject on = out.add<JsonObject>();
    on["label"] = "Both on";
    on["type"] = "button";
    on["query"] = "relays=3";
    on["group"] = "Quick actions";
    JsonObject off = out.add<JsonObject>();
    off["label"] = "Both off";
    off["type"] = "button";
    off["query"] = "relays=0";
    off["group"] = "Quick actions";
  }

 private:
  uint8_t _state = 0;  // bit0 = relay1, bit1 = relay2

  // Apply a 0-3 relay bitmap by writing one byte per channel —
  // bit i -> relay register 0x00+i (0xFF on / 0x00 off).  regWrite
  // is a plain, STOP-terminated transaction (beginTransmission /
  // write reg / write val / endTransmission) — exactly the form the
  // M5Module-2Relay-13.2 library's writeBytes() uses.
  bool _writeState(uint8_t s) {
    if (!regWrite(REG_RELAY + 0, (s & 0x01) ? 0xFF : 0x00))
      return false;
    if (!regWrite(REG_RELAY + 1, (s & 0x02) ? 0xFF : 0x00))
      return false;
    _state = s & 0x03;
    return true;
  }

  // Thin adapters over the shared cmd:: validators (src/CmdParse.h).
  static int _parseBool(const String& v) { return cmd::parseBool(v); }
  // Decimal 0..maxv -> that value, anything else -> -1.
  static int _parseBits(const String& v, int maxv) {
    int32_t n;
    return cmd::parseInt(v, 0, maxv, n) ? static_cast<int>(n) : -1;
  }
};
