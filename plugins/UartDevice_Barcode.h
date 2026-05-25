#pragma once
// ============================================================
//  UartDevice_Barcode.h  –  UART barcode scanner   [UART]
//
//  A scanner that emits the decoded barcode as ASCII over UART,
//  terminated by CR and/or LF — the common default.  Works with
//  the M5Stack Barcode Unit (Honeywell N4313) and similar serial
//  scanners.  No external library.
//
//      fw.addPlugin(new UartDevice_Barcode(Serial2, 9600, 16, 17));
//
//  Readings:  scan_count, code_len.
//  toJson additionally carries last_code (the decoded string).
// ============================================================
#include <string.h>
#include "../src/IUartDevice.h"

class UartDevice_Barcode : public IUartDevice {
 public:
  UartDevice_Barcode(HardwareSerial& port, uint32_t baud = 9600,
                     int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "Barcode Scanner"; }
  const char* slug() const override { return "barcode"; }

  // Drain the UART every loop so a fast scan isn't lost.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    while (_port->available()) {
      char c = static_cast<char>(_port->read());
      if (c == '\r' || c == '\n') {
        if (_len > 0) {  // a complete code arrived
          _code[_len] = '\0';
          _scanCount++;
          _len = 0;
        }
      } else if (_len < MAX_LEN) {
        _code[_len++] = c;
      }
      // characters past MAX_LEN are dropped until the next terminator
    }
  }

  void update() override {}

  void toJson(JsonObject& o) const override {
    o["last_code"] = _code;  // stable member buffer
    o["scan_count"] = _scanCount;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"scan_count", static_cast<float>(_scanCount), ""};
    b[1] = {"code_len", static_cast<float>(strlen(_code)), ""};
    n = 2;
  }

 private:
  static constexpr uint8_t MAX_LEN = 64;
  char _code[MAX_LEN + 1] = {0};  // last complete code (NUL-terminated)
  uint8_t _len = 0;               // chars buffered for the in-progress code
  uint32_t _scanCount = 0;
};
