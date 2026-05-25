#pragma once
// ============================================================
//  IUartDevice.h  –  Base class for UART (serial) devices
//
//  Many M5Stack TIER-2 units are neither I2C nor simple GPIO —
//  they speak a serial protocol over a hardware UART: GPS units
//  (NMEA), barcode scanners (ASCII), fingerprint readers, the
//  cellular / LoRa modems (AT commands), RS-485 / RS-232 bridges.
//
//  IUartDevice extends IPinDevice — so it is a non-I2C device the
//  framework already knows how to activate (via the beginPins()
//  pass after the I2C scan), needing NO framework changes — and
//  adds ownership of a HardwareSerial port.
//
//  ⚠ ONE UART DEVICE AT A TIME.  The ESP32 has one console UART
//  (USB serial, used by SerialOut) plus two free ones; on an
//  M5Stack the practical external serial port is Port-C.  Register
//  at most one IUartDevice and give it a HardwareSerial:
//      fw.addPlugin(new UartDevice_GPS(Serial2, 9600));
//  The Port-C RX/TX pins are resolved automatically for the detected
//  board (via BoardInfo) — Core1 16/17, Core2 & Tough 13/14, CoreS3
//  18/17 — so no GPIO numbers need be hard-coded.  Pass explicit pins
//  as the 3rd/4th constructor argument only to override.
//
//  beginPins() — the IPinDevice activation hook — opens the serial
//  port, then calls beginUart() for any device-specific setup.
// ============================================================
#include "IPinDevice.h"
#include "BoardInfo.h"

class IUartDevice : public IPinDevice {
 public:
  // port  : the HardwareSerial instance to use (e.g. Serial2).
  // baud  : serial speed.
  // rxPin / txPin : the port's pins.  Leave at the -1 default to
  //                 auto-select the detected board's Port-C pins;
  //                 pass explicit pins only to override.
  IUartDevice(HardwareSerial& port, uint32_t baud, int8_t rxPin = -1,
              int8_t txPin = -1)
      : _port(&port), _baud(baud), _rxPin(rxPin), _txPin(txPin) {}

  // IPinDevice activation hook — open the port, then hand off to
  // the concrete device's beginUart().
  bool beginPins() override {
    // Pins left at the -1 default → use the host board's Port-C pins
    // (from BoardInfo) so a UART device runs on any Core without
    // board-specific GPIOs in the sketch.  Explicit pins are honoured.
    int8_t rx = _rxPin, tx = _txPin;
    if (rx < 0 || tx < 0) {
      const BoardInfo& b = BoardInfo::detect();
      rx = b.portCRx;
      tx = b.portCTx;
    }
    // Enlarge the RX buffer BEFORE begin() if the device asked for
    // it — setRxBufferSize() has no effect once begin() has run.
    if (rxBufferSize() > 0)
      _port->setRxBufferSize(rxBufferSize());
    if (rx >= 0 && tx >= 0) {
      _port->begin(_baud, SERIAL_8N1, rx, tx);
      // "probing" — this prints the instant the serial port is OPENED,
      // BEFORE beginUart() has checked whether the device actually
      // answers.  It is NOT a detection: it prints identically whether
      // the module is plugged in or not.  The real verdict is the
      // later "[Pin] <name> ready" / "[Pin] <name> beginPins() FAILED".
      Serial.printf("[UART] %-20s probing Port-C  RX=%d TX=%d @ %u baud\n",
                    name(), static_cast<int>(rx), static_cast<int>(tx), _baud);
    } else {
      _port->begin(_baud);  // no known Port-C — HardwareSerial default
    }
    return beginUart();
  }

  // Optional device-specific setup, run once after the port opens.
  virtual bool beginUart() { return true; }

  // Optional RX buffer size (bytes) for the HardwareSerial port.
  // The ESP32 default is 256 B — ample for slow/occasional traffic
  // (GPS, barcode) but too small for a device that streams bursts
  // faster than the main loop polls it.  Override to return a
  // larger value; 0 (the default) keeps the HardwareSerial default.
  // beginPins() applies this just before begin(), as it must.
  virtual size_t rxBufferSize() const { return 0; }

 protected:
  HardwareSerial* _port;
  uint32_t _baud;
  int8_t _rxPin, _txPin;
};
