#pragma once
// ============================================================
//  BoardInfo.h  –  Runtime detection of the host M5Stack board.
//
//  Uses M5Unified's M5.getBoard() (must be called AFTER M5.begin())
//  to determine which physical board the firmware is running on and
//  exposes the correct internal/external I2C pin assignments plus a
//  few small board-specific identity strings.
//
//  Supported boards (auto-detected):
//    • M5Stack CoreS3       – internal SDA=12 SCL=11 | Port-A SDA=2  SCL=1
//    • M5Stack Core2        – internal SDA=21 SCL=22 | Port-A SDA=32 SCL=33
//    • M5Stack Core2 v1.1   – same pin layout as Core2
//    • M5Stack Tough        – Core2-family industrial board; same pin
//      layout as Core2 (M5Unified reports it as its own board type)
//    • M5Stack Core (Basic / Gray, "Core1") – internal SDA=21 SCL=22,
//      AND Port-A is wired to the SAME pins.  intBus == extBus on this
//      board; the framework treats them as one shared bus.
//
//  Also resolves each board's Port-C (Grove UART) pin pair, used by
//  IUartDevice so serial devices need no hard-coded GPIO numbers.
//
//  Unknown boards fall back to the Core2 pin map (most common) and
//  print a warning to Serial.
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <M5Unified.h>

enum class BoardKind : uint8_t {
  Unknown = 0,
  CoreS3,
  Core2,  // covers Core2, Core2 v1.1 and Core2 for AWS
  Core1,  // original M5Stack Basic / Gray (single shared I2C bus)
  Tough,  // M5Stack Tough — Core2-family, Core2 pin layout
};

struct BoardInfo {
  BoardKind kind = BoardKind::Unknown;
  const char* shortName = "M5Stack";  // e.g. "CoreS3", "Core2", "Core"
  const char* longName = "M5Stack";   // e.g. "M5Stack CoreS3"

  int8_t i2cIntSda = -1;
  int8_t i2cIntScl = -1;
  int8_t i2cExtSda = -1;
  int8_t i2cExtScl = -1;

  // Port-C (Grove UART) pin pair, taken from the board's M5-Bus map.
  // IUartDevice uses these to open a serial device on the correct
  // pins without the sketch hard-coding board-specific GPIO numbers.
  int8_t portCRx = -1;
  int8_t portCTx = -1;

  // TwoWire instances to use for each "bus role".  On boards with
  // separate internal/external buses these point to different
  // controllers (Wire and Wire1).  On Core1 they point to the same
  // controller — Port-A and the internal chips physically share the
  // same SDA/SCL pair, so there is only one bus.  Plugin code should
  // always go through these pointers rather than referencing Wire or
  // Wire1 directly.
  TwoWire* intBus = &Wire;
  TwoWire* extBus = &Wire1;

  // True if intBus and extBus point at the same controller — i.e. the
  // two "buses" are really one shared bus.  Lets Framework skip a
  // redundant second begin() and treat the address space as a single
  // namespace.
  bool sharedBus() const { return intBus == extBus; }

  // What kind of built-in chips are on the internal bus
  const char* imuName = "IMU";
  const char* pmicName = "PMIC";

  // True if the board has an I2C-accessible PMIC (AXP192 / AXP2101).
  // Core1 uses an IP5306 which mostly isn't readable, so Plugin_PMIC
  // should treat it as "not really present" and not try to register.
  bool hasI2cPmic = true;

  // ── SD card SPI pins ───────────────────────────────────────
  //  Per-board microSD pin assignments.  All three supported boards
  //  expose a microSD slot but the SPI pin maps differ:
  //    Core1 (Basic/Gray): MOSI=23 MISO=19 SCK=18 CS=4
  //    Core2 / v1.1      : MOSI=23 MISO=38 SCK=18 CS=4
  //    CoreS3            : MOSI=37 MISO=35 SCK=36 CS=4
  //  SDLogger.cpp uses these to bring up a dedicated SPI controller
  //  for the card, leaving the LCD/touch SPI bus untouched.  Boards
  //  with no SD slot (or no detection yet) set hasSdCard=false.
  int8_t sdMosi = -1;
  int8_t sdMiso = -1;
  int8_t sdSck = -1;
  int8_t sdCs = -1;
  bool hasSdCard = false;

  // ── Reserved internal-bus I2C addresses ───────────────────
  //  Addresses occupied by built-in chips on this board's INTERNAL
  //  bus that the framework has NO plugin for — chiefly the Core2 /
  //  CoreS3 / Tough FT6336U touch controller at 0x38.  The boot
  //  scan refuses to bind any module plugin to these, so a
  //  stackable module that shares the address (GoPlus2 at 0x38) is
  //  never mistaken for one of the board's own chips.  Chips that
  //  DO have a plugin are NOT listed — their plugins claim them
  //  normally (PMIC / RTC / IMU, and the Core2 v1.1's INA3221 at
  //  0x40, which Plugin_INA3221 positively identifies).  External
  //  (Port-A) addresses are never reserved — a Grove unit there is
  //  legitimate.
  uint8_t reservedAddr[8] = {0};
  uint8_t reservedCount = 0;

  // Resolve once after M5.begin().  Cheap to call repeatedly.
  static const BoardInfo& detect();
};
