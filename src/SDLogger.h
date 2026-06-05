#pragma once
// ============================================================
//  SDLogger.h  –  Per-boot CSV logger to microSD card
//
//  Detects the microSD card at boot, opens a fresh CSV file
//  named "<SD_LOG_PREFIX><boot>.csv" (e.g. "/log_0042.csv"),
//  writes a header row built from every active plugin's
//  getReadings() output, and then appends one row every
//  SD_LOG_INTERVAL_MS milliseconds with the current readings.
//
//  Boot number is persisted across reboots in NVS via the
//  Arduino Preferences library — power-cycle the device and
//  the next file picks up at log_0043.csv, log_0044.csv, ...
//
//  Designed to coexist with the LCD: uses the default Arduino
//  SPI object (which on Core1/Core2 is VSPI, the same peripheral
//  M5GFX/LovyanGFX uses for the display) so the card and screen
//  share the bus via CS multiplexing — exactly how M5Stack's own
//  SD examples do it.  Trying to allocate a separate SPI
//  peripheral on the same physical pins corrupts the display
//  because only one peripheral can drive each pin at a time.
//
//  Failures are non-fatal — if the card isn't present, isn't
//  formatted, or fills up, the logger marks itself inactive
//  and the rest of the framework keeps running.
// ============================================================
#include "Config.h"  // for the OUT_SD_LOG build switch

#if OUT_SD_LOG
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

class Framework;

class SDLogger {
 public:
  bool enabled = OUT_SD_LOG;

  void begin(Framework* fw);
  void update();

  // Runtime status snapshot for /api/sdcard.
  struct Stats {
    bool present = false;  // card detected + filesystem mounted
    bool active = false;   // present AND file currently open
    uint8_t cardType = 0;  // SD_NONE, SD_MMC, SD_SD, SD_SDHC
    uint64_t cardSizeBytes = 0;
    uint32_t bootNumber = 0;
    String filename;  // "/log_NNNN.csv"
    uint32_t rowsWritten = 0;
    uint32_t bytesWritten = 0;
    uint32_t writeFailures = 0;
    uint32_t lastWriteMs = 0;
    int8_t selfTest = -1;  // -1 not run, 0 FAIL, 1 PASS
  };
  const Stats& stats() const { return _stats; }

  // Force-commit any buffered writes to the card NOW (in addition
  // to the automatic commit every SD_LOG_FLUSH_EVERY_N rows).
  // Internally a close + reopen-append, which is the only thing
  // that reliably writes the FAT directory entry across all
  // arduino-esp32 core versions.  Returns false if not active.
  bool flush();

  // Cleanly close the log file and unmount the card.  After this
  // the microSD is 100% safe to physically remove.  Logging does
  // NOT resume until the next reboot — call this, pull the card,
  // copy your data, reinsert, and power-cycle.  Returns false if
  // the logger wasn't active.
  bool eject();

  // Append one alert-event CSV line to /alerts.csv (a separate file
  // from the sensor log, opened+closed per write).  AlertManager's SD
  // sink.  No-op if no card is mounted.
  void logAlert(const String& line);

 private:
  Framework* _fw = nullptr;
  File _file;
  uint32_t _lastWrite = 0;
  uint16_t _rowsSinceFlush = 0;
  Stats _stats;

  bool _mountCard();
  void _readBootNumber();  // pull + increment the NVS boot counter
  bool _selfTest();        // write/read-back round-trip via /sdtest.txt
  void _listRoot(const char* label);  // dump SD root dir to serial
  void _openLogFile();
  void _writeHeader();
  void _writeRow();

  // close() + SD.open(..., FILE_APPEND).  close() guarantees the
  // directory entry + FAT are written to the card; reopening in
  // append mode continues the same file.  This is what makes a
  // pulled card actually contain the rows the status counters
  // claim.  Sets _stats.active=false if the reopen fails.
  bool _commit();
};

#else                 // !OUT_SD_LOG
#include <Arduino.h>  // String + fixed-width ints for the Stats struct
// ── Stub — SD logging compiled out via Config.h (OUT_SD_LOG = false).
//  Mirrors the real class's public surface, including the Stats
//  struct /api/sdcard reads, so WebAPI and Framework keep building.
class Framework;
class SDLogger {
 public:
  bool enabled = false;
  void begin(Framework*) {}
  void update() {}
  struct Stats {
    bool present = false;
    bool active = false;
    uint8_t cardType = 0;
    uint64_t cardSizeBytes = 0;
    uint32_t bootNumber = 0;
    String filename;
    uint32_t rowsWritten = 0;
    uint32_t bytesWritten = 0;
    uint32_t writeFailures = 0;
    uint32_t lastWriteMs = 0;
    int8_t selfTest = -1;
  };
  const Stats& stats() const { return _stats; }
  bool flush() { return false; }
  bool eject() { return false; }
  void logAlert(const String&) {}

 private:
  Stats _stats;
};
#endif                // OUT_SD_LOG
