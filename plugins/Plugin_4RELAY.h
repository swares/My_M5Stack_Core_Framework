#pragma once
// ============================================================
//  Plugin_4RELAY.h  –  M5Stack 4-Relay Module (13.2)
//
//  STACKABLE module — sits on the M-Bus / internal I2C bus.
//  I2C address 0x26.  STM32-based; four 5 A relays each with a
//  status LED.
//
//  CONTROLLABLE device.  Drive it via the Web API only:
//    GET /api/4relay/set?relay1=1          turn relay 1 on
//    GET /api/4relay/set?relay3=off        turn relay 3 off
//    GET /api/4relay/set?relays=10         relays as a 0-15 bitmap
//    GET /api/4relay/set?led2=1            status LED 2 on
//    GET /api/4relay/set?leds=15           LEDs as a 0-15 bitmap
//  Every parameter is validated; anything out of range is
//  rejected and the hardware is left untouched.
//
//  Register map (per M5Stack's 4-Relay convention):
//    0x10  MODE     – 0 = async (LEDs independent of relays)
//                     1 = sync  (each LED follows its relay)
//    0x11  CONTROL  – single command byte:
//                     bits 0-3 = relays 1-4
//                     bits 4-7 = status LEDs 1-4
//
//  ⚠ Address clash: the Weight Unit (HX711) is also at 0x26.
//  Both cannot share one bus.  The framework binds whichever
//  plugin is registered first and begins successfully — if you
//  run a 4-Relay module, make sure no Weight Unit is attached.
// ============================================================
#include "../src/IDevice.h"

class Plugin_4RELAY : public IDevice {
 public:
  const char* name() const override { return "4-Relay Module"; }
  const char* slug() const override { return "4relay"; }

  void i2cAddresses(uint8_t* buf, uint8_t& n) const override {
    buf[0] = 0x26;
    n = 1;
  }
  I2CBus preferredBus() const override { return I2CBus::Internal; }
  MountType mount() const override { return MountType::Stackable; }
  bool controllable() const override { return true; }

  bool begin(TwoWire* wire, uint8_t a) override {
    bus = wire;
    addr = a;
    // Async mode: status LEDs are independent of relay state, so
    // the API can drive relays and LEDs separately.
    if (!regWrite(0x10, 0x00))
      return false;
    // Safe initial state — everything off.
    _state = 0x00;
    return regWrite(0x11, _state);
  }

  void update() override {
    // Re-read the control byte so the reported state reflects
    // reality even if the module was power-cycled under us.
    _state = regRead8(0x11);
  }

  void toJson(JsonObject& o) const override {
    for (uint8_t i = 0; i < 4; i++) {
      o[String("relay") + (i + 1)] = (_state >> i) & 1;
      o[String("led") + (i + 1)] = (_state >> (i + 4)) & 1;
    }
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    static const char* k[4] = {"relay1", "relay2", "relay3", "relay4"};
    for (uint8_t i = 0; i < 4; i++)
      b[i] = {k[i], static_cast<float>((_state >> i) & 1), ""};
    n = 4;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    // relays / leds  — value is a 0-15 nibble bitmap.  Checked
    // BEFORE the indexed form so the trailing 's' isn't mistaken
    // for a relay index.
    if (param == "relays" || param == "leds") {
      int v = _parseNibble(value);
      if (v < 0)
        return false;  // not 0-15
      uint8_t s = _state;
      if (param == "relays")
        s = (s & 0xF0) | static_cast<uint8_t>(v);
      else
        s = (s & 0x0F) | (static_cast<uint8_t>(v) << 4);
      return _write(s);
    }
    // relayN / ledN  — N is 1-4, value is a boolean token.
    bool isRelay = (param.length() == 6 && param.startsWith("relay"));
    bool isLed = (param.length() == 4 && param.startsWith("led"));
    if (isRelay || isLed) {
      char idxCh = param.charAt(param.length() - 1);
      if (idxCh < '1' || idxCh > '4')
        return false;  // bad index
      int on = _parseBool(value);
      if (on < 0)
        return false;  // bad value
      uint8_t bit = (idxCh - '1') + (isRelay ? 0 : 4);
      uint8_t s = _state;
      if (on)
        s |= (1 << bit);
      else
        s &= ~(1 << bit);
      return _write(s);
    }
    return false;  // unknown param
  }

  // ── Control schema ────────────────────────────────────────
  //  Eight toggles (4 relays + 4 status LEDs) plus two quick
  //  actions.  Each toggle's value mirrors the live CONTROL byte.
  void controlSchema(JsonArray& out) const override {
    for (uint8_t i = 0; i < 4; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("relay") + (i + 1);
      c["label"] = String("Relay ") + (i + 1);
      c["type"] = "toggle";
      c["group"] = "Relays";
      c["value"] = (_state >> i) & 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
      JsonObject c = out.add<JsonObject>();
      c["id"] = String("led") + (i + 1);
      c["label"] = String("Status LED ") + (i + 1);
      c["type"] = "toggle";
      c["group"] = "Status LEDs";
      c["value"] = (_state >> (i + 4)) & 1;
    }
    JsonObject on = out.add<JsonObject>();
    on["label"] = "All relays on";
    on["type"] = "button";
    on["query"] = "relays=15";
    on["group"] = "Quick actions";
    JsonObject off = out.add<JsonObject>();
    off["label"] = "All off";
    off["type"] = "button";
    off["query"] = "relays=0&leds=0";
    off["group"] = "Quick actions";
  }

 private:
  uint8_t _state = 0;  // mirror of CONTROL register 0x11

  bool _write(uint8_t s) {
    if (!regWrite(0x11, s))
      return false;
    _state = s;
    return true;
  }

  // "1/on/true" → 1, "0/off/false" → 0, anything else → -1.
  static int _parseBool(const String& v) {
    String t = v;
    t.toLowerCase();
    if (t == "1" || t == "on" || t == "true")
      return 1;
    if (t == "0" || t == "off" || t == "false")
      return 0;
    return -1;
  }
  // Decimal 0-15 → that value, anything else → -1.
  static int _parseNibble(const String& v) {
    for (uint16_t i = 0; i < v.length(); i++)
      if (!isDigit(v.charAt(i)))
        return -1;
    if (v.length() == 0)
      return -1;
    int32_t n = v.toInt();
    return (n >= 0 && n <= 15) ? static_cast<int>(n) : -1;
  }
};
