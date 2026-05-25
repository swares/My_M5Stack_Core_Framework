#pragma once
// ============================================================
//  PinDevice_IR.h  –  M5Stack IR Unit (Tx/Rx)  [non-I2C]
//
//  An infrared transmitter LED + receiver on two GPIO lines.
//
//  ⚠ REQUIRES the "IRremoteESP8266" library (install via the
//  Arduino Library Manager — despite the name it fully supports
//  the ESP32).  Because of that dependency this header is NOT
//  #included by the .ino by default — uncomment its #include
//  there together with the registration line.
//
//      fw.addPlugin(new PinDevice_IR(/*rxPin=*/36, /*txPin=*/26));
//
//  RECEIVE — any protocol IRremoteESP8266 can decode is captured;
//  the most recently received code is reported.
//  TRANSMIT — Web API only; sends an NEC code (the common case):
//      GET /api/ir/set?send=20DF10EF      transmit NEC code (hex)
//  Validation:  send — 1-16 hex digits.
//
//  Readings:  rx_count (codes received so far), last_bits.
//  toJson additionally carries last_code (hex) and last_proto.
// ============================================================
#include <cstdio>
#include "../src/IPinDevice.h"
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>

class PinDevice_IR : public IPinDevice {
 public:
  PinDevice_IR(uint8_t rxPin, uint8_t txPin)
      : _rxPin(rxPin), _txPin(txPin), _recv(rxPin), _send(txPin) {}

  const char* name() const override { return "IR Unit (Tx/Rx)"; }
  const char* slug() const override { return "ir"; }
  bool controllable() const override { return true; }

  // Poll the IR decode buffer often so codes aren't missed.
  bool wantsFastPoll() const override { return true; }

  bool beginPins() override {
    _recv.enableIRIn();
    _send.begin();
    return true;
  }

  void fastPoll() override {
    decode_results r;
    if (_recv.decode(&r)) {
      _lastCode = r.value;
      _lastBits = r.bits;
      _lastProto = r.decode_type;
      _rxCount++;
      _recv.resume();  // ready for the next code
    }
  }

  void update() override {}

  void toJson(JsonObject& o) const override {
    char hex[20];
    snprintf(hex, sizeof(hex), "%llX", static_cast<uint64_t>(_lastCode));
    o["last_code"] = hex;                        // char[] → copied
    o["last_proto"] = typeToString(_lastProto);  // String → copied
    o["last_bits"] = _lastBits;
    o["rx_count"] = _rxCount;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"rx_count", static_cast<float>(_rxCount), ""};
    b[1] = {"last_bits", static_cast<float>(_lastBits), ""};
    n = 2;
  }

  bool command(const String& param, const String& value) override {
    if (param == "send") {
      uint64_t code;
      if (!_parseHex(value, code))
        return false;
      _send.sendNEC(code, 32);
      return true;
    }
    return false;
  }

  // ── Control schema ────────────────────────────────────────
  //  A free-text field for the NEC code (1-16 hex digits); the
  //  dashboard pairs it with a Send button.
  void controlSchema(JsonArray& out) const override {
    JsonObject c = out.add<JsonObject>();
    c["id"] = "send";
    c["label"] = "Send NEC code (hex)";
    c["type"] = "text";
    c["placeholder"] = "20DF10EF";
  }

 private:
  uint8_t _rxPin, _txPin;
  IRrecv _recv;
  IRsend _send;
  uint64_t _lastCode = 0;
  uint16_t _lastBits = 0;
  decode_type_t _lastProto = decode_type_t::UNKNOWN;
  uint32_t _rxCount = 0;

  // Parse 1-16 hex digits into a uint64_t.
  static bool _parseHex(const String& v, uint64_t& out) {
    uint16_t len = v.length();
    if (len < 1 || len > 16)
      return false;
    uint64_t acc = 0;
    for (uint16_t i = 0; i < len; i++) {
      char c = v.charAt(i);
      int d;
      if (c >= '0' && c <= '9')
        d = c - '0';
      else if (c >= 'a' && c <= 'f')
        d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        d = c - 'A' + 10;
      else
        return false;
      acc = (acc << 4) | static_cast<uint64_t>(d);
    }
    out = acc;
    return true;
  }
};
