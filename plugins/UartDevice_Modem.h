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
#include "../src/IUartDevice.h"

class UartDevice_Modem : public IUartDevice {
 public:
  UartDevice_Modem(HardwareSerial& port, uint32_t baud = 115200,
                   int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "Cellular Modem"; }
  const char* slug() const override { return "modem"; }

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
      if (c == '\r' || c == '\n') {
        if (_len > 0) {
          _line[_len] = '\0';
          _parseLine(_line);
          _len = 0;
        }
      } else if (_len < LINE_BUF) {
        _line[_len++] = c;
      }
    }
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
