#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── SD card CSV logging ──────────────────────────────────────
//  When OUT_SD_LOG is true and a microSD card is detected at boot,
//  the framework writes one CSV file per boot to the card root.
//  Filename: SD_LOG_PREFIX + four-digit boot number + ".csv",
//  e.g. "log_0001.csv", "log_0002.csv", ... The boot number is
//  persisted across reboots in NVS (via the Arduino Preferences
//  library) so each power-cycle starts a fresh file.
//
//  Format:
//    First line  – header: "time_s,<slug1>_<key1>,<slug1>_<key2>,..."
//    Each subsequent line – uptime seconds + one column per
//    reading from every active plugin, in registration order.
//
//  Cadence: SD_LOG_INTERVAL_MS controls how often a row is
//  appended.  Set this independent of POLL_MS (the framework's
//  sensor poll cycle).  Default 5000 ms matches the MQTT publish
//  cadence so the log file mirrors what the broker sees.
//
//  Commit: every SD_LOG_FLUSH_EVERY_N rows the logger does a full
//  close + reopen of the file.  close() is the only operation that
//  reliably writes the FAT directory entry across all arduino-esp32
//  core versions — a plain fsync()/flush() sometimes leaves the
//  data clusters written but the directory never updated, so a card
//  read on a PC shows no file at all.  close+reopen eliminates that.
//
//  Higher N = fewer close/reopen cycles (less card wear) but up to
//  N rows at risk if power is lost between commits.  Lower N = each
//  row hits the card sooner.  Default 10 (~50 s at 5 s cadence).
//  Set to 1 for every-row durability; the close/reopen cost is
//  negligible at this logging rate.
//
//  You can also force a commit any time via GET /api/sdcard/flush,
//  or cleanly close + unmount for safe card removal via
//  GET /api/sdcard/eject.
[[maybe_unused]] constexpr unsigned long SD_LOG_INTERVAL_MS = 5000;
[[maybe_unused]] constexpr unsigned SD_LOG_FLUSH_EVERY_N = 10;
// leading slash = root of SD
[[maybe_unused]] constexpr char SD_LOG_PREFIX[] = "/log_";

// SD card SPI clock.  On Core1/Core2 the microSD shares the SPI
// bus with the LCD; the extra trace length + capacitance can make
// a fast clock unreliable for WRITES even when card detection
// (slow-clock reads) works fine — the classic symptom is byte
// counters that climb while the card stays empty.  4 MHz is a safe
// default on the shared bus.  Drop to 1000000 (1 MHz) if the boot
// SD self-test still fails; raise toward 20000000 only on a board
// with a dedicated, short SD bus.
[[maybe_unused]] constexpr unsigned long SD_SPI_HZ = 4000000;

// SD_SELFTEST = true runs a write/read-back round-trip at boot
// (creates /sdtest.txt, reads it back, verifies byte-for-byte).
// Leave it on — /sdtest.txt is also handy to check from a PC: if
// the serial log says PASS but /sdtest.txt isn't on the card when
// you read it, something is wrong downstream of the firmware.
#define SD_SELFTEST true
