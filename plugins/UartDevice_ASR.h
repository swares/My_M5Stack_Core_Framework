#pragma once
// ============================================================
//  UartDevice_ASR.h  –  M5Stack ASR Module (CI1302)     [UART]
//
//  An offline voice-recognition module built on the CI1302 chip.
//  It listens for its pre-trained wake word and command words and,
//  whenever it recognises one, sends a fixed 5-byte frame over UART
//  at 115200 baud:
//
//      0xAA 0x55 <ID> 0x55 0xAA
//
//  <ID> is the command code — in the stock firmware 0xFF is the
//  wake word ("Hi, M Five"), 0x14 is "turn on", 0x15 is "turn off",
//  and so on.  This device is a passive listener: it parses those
//  frames and reports the most recently recognised command.  It
//  sends nothing back, so no external library is needed.
//
//      fw.addPlugin(new UartDevice_ASR(Serial2, 115200));
//
//  ⚠ The ASR module has a DIP switch that selects which pin pair
//  carries its UART — set it to the Port-C pair (G18 RX / G17 TX
//  on a CoreS3) so the framework's auto-resolved Port-C pins line
//  up.  Re-training the command set needs a custom-firmware flash
//  (see M5Stack's "Module ASR Custom Firmware" guide); the IDs
//  this device reports are whatever that firmware emits.
//
//  Readings:  last_cmd  (ID of the most recent recognised command,
//                         -1 until the first is heard),
//             cmd_count (total commands recognised since boot).
// ============================================================
#include "../src/IUartDevice.h"

class UartDevice_ASR : public IUartDevice {
 public:
  UartDevice_ASR(HardwareSerial& port, uint32_t baud = 115200,
                 int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  const char* name() const override { return "ASR Module (CI1302)"; }
  const char* slug() const override { return "asr"; }

  // Command frames are short and infrequent, but draining the UART
  // every loop keeps the RX buffer clear and the latency low.
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    while (_port->available()) {
      uint8_t b = static_cast<uint8_t>(_port->read());
      // Assemble the fixed 5-byte frame  AA 55 <ID> 55 AA  with a
      // small state machine.  Any unexpected byte resyncs cleanly.
      switch (_idx) {
        case 0:
          _idx = (b == 0xAA) ? 1 : 0;
          break;
        case 1:
          _idx = (b == 0x55) ? 2 : (b == 0xAA ? 1 : 0);
          break;
        case 2:
          _id = b;
          _idx = 3;
          break;
        case 3:
          _idx = (b == 0x55) ? 4 : (b == 0xAA ? 1 : 0);
          break;
        case 4:
          if (b == 0xAA) {
            _lastCmd = _id;
            _count++;
          }
          _idx = 0;
          break;
      }
    }
  }

  void update() override {}  // all work happens in fastPoll()

  void toJson(JsonObject& o) const override {
    o["last_cmd"] = _lastCmd;
    o["cmd_count"] = _count;
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"last_cmd", static_cast<float>(_lastCmd), ""};
    b[1] = {"cmd_count", static_cast<float>(_count), ""};
    n = 2;
  }

 private:
  uint8_t _idx = 0;       // frame-parser state (0-4)
  uint8_t _id = 0;        // ID byte of the frame being assembled
  int16_t _lastCmd = -1;  // most recent recognised command (-1 = none)
  uint32_t _count = 0;    // commands recognised since boot
};
