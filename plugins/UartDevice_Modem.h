#pragma once
// ============================================================
//  UartDevice_Modem.h  –  Cellular modem signal monitor   [UART]
//
//  A minimal, READ-ONLY monitor for an AT-command cellular modem.
//  The M5Stack CatM Unit (SIM7080G) and 4G Module (SIM7600G) both
//  work, as does any SIMCom / 3GPP-compliant modem.
//
//  This is NOT a full modem driver — no data session, SMS, GNSS,
//  power sequencing, etc.  It periodically issues two bedrock
//  3GPP TS 27.007 AT commands and reports the result:
//      AT+CSQ    → received signal strength
//      AT+CREG?  → network registration status
//  Those two are identical across SIMCom modems, so this monitor
//  is correct without any device-specific guesswork.
//
//      fw.addPlugin(new UartDevice_Modem(Serial2, 115200, 16, 17));
//
//  Readings:  rssi_dbm, signal_pct (0-100), registered (0/1).
//  No external library.
// ============================================================
#include <stdlib.h>
#include <string.h>
#include "../src/IUartDevice.h"

class UartDevice_Modem : public IUartDevice {
 public:
  UartDevice_Modem(HardwareSerial& port, uint32_t baud = 115200,
                   int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "Cellular Modem"; }
  const char* slug() const override { return "modem"; }
  bool controllable() const override { return true; }  // SMS send

  bool beginUart() override {
    _port->print("ATE0\r");  // disable command echo — quieter parsing
    return true;
  }

  // Drain the modem's responses every loop and parse any line that
  // carries a +CSQ or +CREG result.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    while (_port->available()) {
      char c = static_cast<char>(_port->read());
      // The CMGS prompt ("> ") is NOT line-terminated — catch it raw.
      if (_smsState == SMS_PROMPT && c == '>') {
        _port->print(_smsText);
        _port->write(0x1A);  // Ctrl-Z ends the message body
        _smsState = SMS_RESULT;
        _smsStartMs = millis();
        _len = 0;
        continue;
      }
      if (c == '\r' || c == '\n') {
        if (_len > 0) {
          _line[_len] = '\0';
          _parseLine(_line);   // CSQ / CREG monitor
          _smsLine(_line);     // SMS send state machine
          _len = 0;
        }
      } else if (_len < LINE_BUF) {
        _line[_len++] = c;
      }
    }
    // Abandon a stuck send so the modem never wedges the channel.
    if (_smsState != SMS_IDLE && millis() - _smsStartMs > 30000) {
      _smsState = SMS_IDLE;
      _smsFailed++;
      Serial.println(F("[Modem] SMS send timed out"));
    }
  }

  // ── Control: send an SMS ──────────────────────────────────
  //  GET /api/modem/set?sms=<number>,<text>   (AlertManager's SMS sink
  //  calls the same command).  Async — kicks off the AT sequence and
  //  returns at once; fastPoll() drives it to completion.
  bool command(const String& param, const String& value) override {
    if (param != "sms") return false;
    if (_smsState != SMS_IDLE) {
      Serial.println(F("[Modem] SMS rejected — a send is in flight"));
      return false;
    }
    int comma = value.indexOf(',');
    if (comma <= 0) {
      Serial.println(F("[Modem] sms needs <number>,<text>"));
      return false;
    }
    String num = value.substring(0, comma);
    num.trim();
    String text = value.substring(comma + 1);
    if (num.length() == 0 || text.length() == 0) return false;
    if (!_dailyCapOk()) {
      Serial.println(F("[Modem] SMS daily cap reached — dropping"));
      return false;
    }
    num.toCharArray(_smsNum, sizeof(_smsNum));
    _smsText = text;
    _port->print("AT+CMGF=1\r");   // text mode; '>' prompt comes after CMGS
    _smsState = SMS_CMGF;
    _smsStartMs = millis();
    Serial.printf("[Modem] SMS -> %s : %.40s\n", _smsNum, text.c_str());
    return true;
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject a = out.add<JsonObject>();
    a["id"] = "sms";
    a["label"] = "Send SMS";
    a["type"] = "text";
    a["placeholder"] = "+15551234567,your message";
  }

  // Once per poll cycle ask the modem something — alternating so
  // both values refresh, and never blocking on the reply (fastPoll
  // picks the reply up asynchronously).
  void update() override {
    _port->print(_toggle ? "AT+CSQ\r" : "AT+CREG?\r");
    _toggle = !_toggle;
  }

  void toJson(JsonObject& o) const override {
    o["rssi_dbm"] = _rssiDbm;
    o["signal_pct"] = _signalPct;
    o["registered"] = _registered ? 1 : 0;
    o["creg_stat"] = _cregStat;
    o["sms_busy"] = _smsState != SMS_IDLE ? 1 : 0;
    o["sms_sent"] = _smsSent;
    o["sms_failed"] = _smsFailed;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"rssi_dbm", static_cast<float>(_rssiDbm), "dBm"};
    b[1] = {"signal_pct", static_cast<float>(_signalPct), "%"};
    b[2] = {"registered", static_cast<float>(_registered ? 1 : 0), ""};
    n = 3;
  }

 private:
  static constexpr uint8_t LINE_BUF = 64;
  char _line[LINE_BUF + 1] = {0};
  uint8_t _len = 0;
  bool _toggle = true;

  int _rssiDbm = 0;  // 0 = unknown
  int _signalPct = 0;
  bool _registered = false;
  int _cregStat = 0;

  // Parse one response line.  Recognised:
  //   +CSQ: <rssi>,<ber>            rssi 0-31, 99 = unknown
  //   +CREG: <n>,<stat>  (or the URC form  +CREG: <stat>)
  void _parseLine(const char* s) {
    if (_prefix(s, "+CSQ:")) {
      int32_t rssi = _firstInt(s + 5);
      if (rssi >= 0 && rssi <= 31) {
        _rssiDbm = -113 + static_cast<int>(2 * rssi);
        _signalPct = static_cast<int>((rssi * 100) / 31);
      } else {  // 99 → not known / not detectable
        _rssiDbm = 0;
        _signalPct = 0;
      }
    } else if (_prefix(s, "+CREG:")) {
      // Take the last of the leading comma-separated integers as
      // <stat> — covers "+CREG: 0,1" and the URC form "+CREG: 1".
      int32_t nums[2] = {0, 0};
      uint8_t cnt = _leadingInts(s + 6, nums, 2);
      int32_t stat = (cnt >= 2) ? nums[1] : nums[0];
      _cregStat = static_cast<int>(stat);
      _registered = (stat == 1 || stat == 5);  // 1 = home, 5 = roaming
    }
  }

  // ── SMS send state machine ────────────────────────────────
  enum SmsState : uint8_t { SMS_IDLE = 0, SMS_CMGF, SMS_PROMPT, SMS_RESULT };
  SmsState _smsState = SMS_IDLE;
  char     _smsNum[24] = {0};
  String   _smsText;
  uint32_t _smsStartMs = 0;
  uint32_t _smsSent = 0, _smsFailed = 0;
  // Cost ceiling: at most SMS_DAILY_MAX sends per rolling ~24 h.
  static constexpr uint8_t SMS_DAILY_MAX = 20;
  uint32_t _smsWindowStart = 0;
  uint16_t _smsWindowCount = 0;

  bool _dailyCapOk() {
    uint32_t now = millis();
    if (_smsWindowStart == 0 || now - _smsWindowStart > 86400000UL) {
      _smsWindowStart = now;
      _smsWindowCount = 0;
    }
    if (_smsWindowCount >= SMS_DAILY_MAX) return false;
    _smsWindowCount++;  // count the attempt (cost is per attempt)
    return true;
  }

  // Advance the send on each modem reply line.
  void _smsLine(const char* s) {
    if (_smsState == SMS_CMGF) {
      if (_eq(s, "OK")) {
        _port->print("AT+CMGS=\"");
        _port->print(_smsNum);
        _port->print("\"\r");
        _smsState = SMS_PROMPT;
        _smsStartMs = millis();
      } else if (_contains(s, "ERROR")) {
        _smsState = SMS_IDLE;
        _smsFailed++;
      }
    } else if (_smsState == SMS_RESULT) {
      if (_prefix(s, "+CMGS:") || _eq(s, "OK")) {
        _smsState = SMS_IDLE;
        _smsSent++;
        Serial.printf("[Modem] SMS sent to %s\n", _smsNum);
      } else if (_contains(s, "ERROR")) {
        _smsState = SMS_IDLE;
        _smsFailed++;
        Serial.println(F("[Modem] SMS send returned ERROR"));
      }
    }
  }

  static bool _eq(const char* a, const char* b) { return strcmp(a, b) == 0; }
  static bool _contains(const char* h, const char* n) {
    return strstr(h, n) != nullptr;
  }

  static bool _prefix(const char* s, const char* p) {
    while (*p) {
      if (*s++ != *p++)
        return false;
    }
    return true;
  }
  // First integer found in s (skips leading non-digit, non-minus).
  static int32_t _firstInt(const char* s) {
    while (*s && !(*s >= '0' && *s <= '9') && *s != '-')
      s++;
    if (!*s)
      return -1;
    return atol(s);
  }
  // Parse up to `max` comma-separated leading integers; returns count.
  static uint8_t _leadingInts(const char* s, int32_t* out, uint8_t max) {
    uint8_t c = 0;
    while (c < max) {
      while (*s == ' ')
        s++;
      if (!(*s == '-' || (*s >= '0' && *s <= '9')))
        break;
      out[c++] = atol(s);
      if (*s == '-')
        s++;
      while (*s >= '0' && *s <= '9')
        s++;
      while (*s == ' ')
        s++;
      if (*s != ',')
        break;
      s++;
    }
    return c;
  }
};
