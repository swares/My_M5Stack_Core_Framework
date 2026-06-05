// ============================================================
//  SDLogger.cpp
// ============================================================
#include "Config.h"        // OUT_SD_LOG — must precede the #if
#if OUT_SD_LOG
#include "SDLogger.h"
#include "Framework.h"
#include <Preferences.h>
#include <cstdio>          // snprintf

// ── begin ─────────────────────────────────────────────────────
//  Wire up the per-board SD SPI pins, mount the card, bump the
//  boot counter in NVS, and open a fresh log file with a CSV
//  header derived from every active plugin's getReadings()
//  schema.  Any failure here disables the logger gracefully —
//  the framework's other output channels keep running.
void SDLogger::begin(Framework* fw) {
  _fw = fw;
  if (!enabled) {
    Serial.println(F("[SD] disabled (OUT_SD_LOG=false)"));
    return;
  }
  const BoardInfo& bi = fw->board();
  if (!bi.hasSdCard) {
    Serial.println(F("[SD] disabled (no SD slot on this board variant)"));
    enabled = false;
    return;
  }

  if (!_mountCard()) {
    // _mountCard prints its own diagnostics; just flip the flag so
    // update() short-circuits without further attempts.
    enabled = false;
    return;
  }

  // Read + increment the NVS boot counter NOW, before _selfTest()
  // runs its cross-boot check — that check compares the prior
  // /sdtest.txt's boot number against this one, so this one has to
  // be valid first.  (Previously the counter was only loaded in
  // _openLogFile(), which runs after _selfTest(), making the
  // cross-boot check always compare against 0.)
  _readBootNumber();

  // Show what's already on the card BEFORE we touch it.  This is
  // the key cross-check: if this listing includes a file you wrote
  // from a PC, the firmware and the PC are looking at the same
  // filesystem.  If it also shows prior /log_*.csv files, earlier
  // boots persisted fine.  If the card looks empty here but you
  // know you put files on it from a PC, the firmware is mounting a
  // different view of the card than the PC does.
  _listRoot("on mount (pre-existing files)");

  // Prove the card actually accepts writes before we trust it with
  // log data.  Card detection (cardType/cardSize) only exercises
  // slow-clock reads — writes can still silently fail on a marginal
  // shared SPI bus.  A failed self-test disables logging loudly
  // rather than spending the whole run writing into a void.
  if (SD_SELFTEST) {
    _stats.selfTest = _selfTest() ? 1 : 0;
    if (_stats.selfTest == 0) {
      Serial.println(F("[SD] self-test FAILED — writes are not persisting."));
      Serial.println(F("[SD]   Most likely SPI signal integrity on shared"));
      Serial.println(F("[SD]   LCD bus.  Try lowering SD_SPI_HZ in Config.h"));
      Serial.println(F("[SD]   (e.g. 1000000) and reflash.  Logger disabled."));
      enabled = false;
      return;
    }
  }

  _openLogFile();
  if (!_file) {
    enabled = false;
    return;
  }
  _writeHeader();

  // Show the card again AFTER creating the log file.  /sdtest.txt
  // and the new /log_*.csv should now appear with non-zero sizes.
  // If they appear here but NOT when you read the card on a PC,
  // the card isn't committing data across a power cycle (typical
  // of counterfeit cards) — and the next boot's cross-boot check
  // will confirm it.
  _listRoot("after creating log file");
  _stats.active     = true;
  _stats.lastWriteMs = millis();
  Serial.printf("[SD] logging to %s (boot #%u)\n",
                _stats.filename.c_str(), _stats.bootNumber);
}

// ── _mountCard ────────────────────────────────────────────────
//  Mount the microSD card on the SAME SPI bus the LCD already uses.
//  On M5Stack Core1 and Core2 the display (LovyanGFX/M5GFX behind
//  M5.Display) owns VSPI = SPI3.  The SD card slot is wired to the
//  same physical MOSI/MISO/SCK pins.  Sharing the bus via CS
//  multiplexing (each device with its own chip-select line plus
//  beginTransaction/endTransaction) is the only way both can work;
//  initialising a second SPI peripheral on the same physical pins
//  steals the GPIO matrix routing from the LCD and locks the
//  display at whatever frame it last drew before the SD attempt.
//
//  CoreS3 (ESP32-S3) uses FSPI for the LCD; the same shared-bus
//  approach works there because Arduino's default SPI on S3 also
//  resolves to FSPI.
bool SDLogger::_mountCard() {
  const BoardInfo& bi = _fw->board();

  Serial.printf("[SD] attempting mount (CS=%d, shared LCD bus)\n", bi.sdCs);

  // SD_SPI_HZ (Config.h) — kept conservative because the SD card
  // shares the LCD's SPI bus on Core1/Core2.  Pass the default SPI
  // object explicitly so it's clear we're intentionally sharing
  // the display's bus rather than racing it on a second peripheral.
  Serial.printf("[SD] SPI clock %u Hz\n", static_cast<unsigned>(SD_SPI_HZ));
  if (!SD.begin(bi.sdCs, SPI, SD_SPI_HZ)) {
    Serial.println(F("[SD] mount FAILED — no card, bad format, "
                     "wiring fault, or SPI bus busy.  Logger disabled."));
    return false;
  }

  uint8_t  type = SD.cardType();
  uint64_t sz   = SD.cardSize();
  if (type == CARD_NONE) {
    Serial.println(F("[SD] no card detected after mount."));
    SD.end();
    return false;
  }

  _stats.present       = true;
  _stats.cardType      = type;
  _stats.cardSizeBytes = sz;

  const char* tname = "?";
  switch (type) {
    case CARD_MMC:  tname = "MMC";   break;
    case CARD_SD:   tname = "SDSC";  break;
    case CARD_SDHC: tname = "SDHC";  break;
    default:        tname = "UNKNOWN"; break;
  }
  Serial.printf("[SD] mounted: %s, %llu MB\n",
                tname, static_cast<uint64_t>(sz / (1024ULL * 1024ULL)));
  return true;
}

// ── _selfTest ─────────────────────────────────────────────────
//  Write a known string to /sdtest.txt, close it, reopen it, read
//  it back, and compare byte-for-byte.  This is the definitive
//  "can this card actually be written?" check — card detection
//  only exercises slow-clock register reads, which can succeed on
//  a card whose data writes silently corrupt.
//
//  The test file is left on the card on purpose: after a clean
//  power-off you can pop the card into a PC and confirm
//  /sdtest.txt is there with the expected boot number.  If serial
//  said PASS but the PC shows no /sdtest.txt, the problem is
//  downstream of the firmware (card reader, filesystem cache).
bool SDLogger::_selfTest() {
  const char* path = "/sdtest.txt";

  String content = F("M5Stack I2C Framework SD self-test\n");
  content += "boot=";   content += String(_stats.bootNumber);
  content += "\nmillis="; content += String(millis());
  content += "\nspi_hz="; content += String(static_cast<uint32_t>(SD_SPI_HZ));
  content += "\n";

  // ── Cross-boot persistence check ─────────────────────────
  // Before overwriting /sdtest.txt, read whatever a PREVIOUS
  // boot left in it.  The boot counter lives in NVS, so it
  // always increments; if the card truly persists data across
  // power cycles, the prior /sdtest.txt should contain exactly
  // (this boot - 1).  Missing or stale = the card is NOT
  // committing writes to flash across a power-off, which is the
  // classic signature of a counterfeit or failing microSD.
  if (SD.exists(path)) {
    File old = SD.open(path, FILE_READ);
    if (old) {
      String prev;
      while (old.available() && prev.length() < 200) {
        prev += static_cast<char>(old.read());
      }
      old.close();
      int b = prev.indexOf("boot=");
      int32_t prevBoot = (b >= 0) ? prev.substring(b + 5).toInt() : -1;
      if (prevBoot == static_cast<int32_t>(_stats.bootNumber) - 1) {
        Serial.printf("[SD] cross-boot check: prior /sdtest.txt has boot=%d, "
                      "this boot=%u — card PERSISTS across reboots. Good.\n",
                      prevBoot, (unsigned)_stats.bootNumber);
      } else {
        Serial.printf("[SD] cross-boot check: prior /sdtest.txt has boot=%d "
                      "but expected %u — card did NOT persist the last boot.\n",
                      prevBoot, (unsigned)_stats.bootNumber - 1);
      }
    }
  } else {
    Serial.printf("[SD] cross-boot check: no prior /sdtest.txt found.  Either "
                  "this is a first run, or boot #%u's write did not survive "
                  "power-off (suspect a fake/bad card).\n",
                  (unsigned)_stats.bootNumber - 1);
  }

  // Fresh file every boot — remove any prior copy so the readback
  // comparison isn't fooled by leftover content (FILE_WRITE's
  // truncate behaviour varies across core versions).
  if (SD.exists(path)) SD.remove(path);

  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    Serial.println(F("[SD] self-test: cannot open /sdtest.txt for write"));
    return false;
  }
  size_t wrote = f.print(content);
  f.close();                                  // close == real commit
  if (wrote != content.length()) {
    Serial.printf("[SD] self-test: short write (%u of %u bytes)\n",
                  (unsigned)wrote, (unsigned)content.length());
    return false;
  }

  File r = SD.open(path, FILE_READ);
  if (!r) {
    Serial.println(F("[SD] self-test: cannot reopen /sdtest.txt for read"));
    return false;
  }
  String readBack;
  readBack.reserve(content.length() + 4);
  while (r.available()) readBack += static_cast<char>(r.read());
  r.close();

  if (readBack != content) {
    Serial.printf("[SD] self-test: MISMATCH — wrote %u bytes, read %u back\n",
                  (unsigned)content.length(), (unsigned)readBack.length());
    return false;
  }

  Serial.printf("[SD] self-test PASS — %u bytes round-tripped via %s\n",
                (unsigned)content.length(), path);
  return true;
}

// ── _listRoot ─────────────────────────────────────────────────
//  Dump every entry in the SD card's root directory to serial,
//  with sizes.  This is the single most useful SD diagnostic:
//
//    * If a file you created from a PC shows up here, the firmware
//      and the PC are reading the SAME filesystem on the card.
//    * If /log_*.csv files from earlier boots show up here, those
//      boots persisted their data across power cycles.
//    * If the card looks empty here but you know you wrote files
//      to it from a PC, the firmware is mounting a different view
//      of the card (partition / format mismatch).
void SDLogger::_listRoot(const char* label) {
  Serial.printf("[SD] root listing — %s:\n", label);
  File root = SD.open("/");
  if (!root) {
    Serial.println(F("[SD]   (cannot open root directory)"));
    return;
  }
  uint16_t count = 0;
  uint32_t totalBytes = 0;
  for (File e = root.openNextFile(); e; e = root.openNextFile()) {
    if (e.isDirectory()) {
      Serial.printf("[SD]   <DIR>      %s\n", e.name());
    } else {
      uint32_t sz = e.size();
      totalBytes += sz;
      Serial.printf("[SD]   %9u  %s\n", (unsigned)sz, e.name());
    }
    count++;
    e.close();
  }
  root.close();
  Serial.printf("[SD]   %u entr%s, %u bytes total\n",
                count, count == 1 ? "y" : "ies", (unsigned)totalBytes);
}

// ── _openLogFile ──────────────────────────────────────────────
//  Compose a filename and open it for append.  Two filename modes:
//
//    NTP synced  →  /log_YYYYMMDD_HHMMSS.csv
//                   (sortable by name == sortable by time; every
//                    reboot makes a fresh file without colliding)
//
//    NTP missing →  /log_boot0001.csv  (fallback)
//                   (NVS-persisted boot counter so we still don't
//                    overwrite earlier offline-boot files)
//
//  Either way the boot counter is incremented and stored in stats
//  so the API/dashboard can show "this is the 47th boot".
// ── _readBootNumber ───────────────────────────────────────────
//  Load the persistent boot counter from NVS, increment it, store
//  it back.  Called early in begin() so _selfTest()'s cross-boot
//  check and _openLogFile()'s filename both see a valid number.
void SDLogger::_readBootNumber() {
  Preferences prefs;
  prefs.begin("sdlogger", false);
  uint32_t boot = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", boot);
  prefs.end();
  _stats.bootNumber = boot;
}

void SDLogger::_openLogFile() {
  char fn[40];
  if (_fw->timeSynced()) {
    struct tm tm;
    if (getLocalTime(&tm, 0)) {
      char ts[24];
      strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);
      snprintf(fn, sizeof(fn), "%s%s.csv", SD_LOG_PREFIX, ts);
    } else {
      // shouldn't happen if _timeSynced is true, but belt-and-braces
      snprintf(fn, sizeof(fn), "%sboot%04u.csv",
             SD_LOG_PREFIX, (unsigned)_stats.bootNumber);
    }
  } else {
    snprintf(fn, sizeof(fn), "%sboot%04u.csv",
             SD_LOG_PREFIX, (unsigned)_stats.bootNumber);
  }
  _stats.filename = fn;

  _file = SD.open(fn, FILE_APPEND);
  if (!_file) {
    Serial.printf("[SD] open %s FAILED — logger disabled\n", fn);
    _stats.writeFailures++;
    return;
  }
}

// ── _writeHeader ──────────────────────────────────────────────
//  Build "time_s,<slug>_<key>,<slug>_<key>,..." from every active
//  plugin's getReadings() output.  Plugins that are registered but
//  unbound get skipped — their columns would be empty for every row
//  anyway.  Header is written once per file, not once per row.
void SDLogger::_writeHeader() {
  if (!_file) return;

  // datetime first (ISO 8601 wall-clock or empty if NTP didn't
  // sync), then uptime seconds, then one column per reading.
  // Including BOTH datetime and uptime is intentional — offline
  // boots still have a usable monotonic axis even without NTP,
  // and synced runs let you correlate against external clocks.
  String header = "datetime,time_s";
  for (auto* p : _fw->plugins()) {
    if (!p->active) continue;
    SensorVal vals[16];
    uint8_t n = 0;
    p->getReadings(vals, n);
    for (uint8_t i = 0; i < n; i++) {
      header += ',';
      header += p->slug();
      header += '_';
      header += vals[i].key;
    }
  }
  header += '\n';

  size_t wrote = _file.print(header);
  _stats.bytesWritten += wrote;
  // Commit immediately so the file appears on the card the moment
  // logging starts — not only after the first SD_LOG_FLUSH_EVERY_N
  // data rows.  Pull the card 3 seconds after boot and you'll still
  // see at least the header.
  _commit();
}

// ── update ────────────────────────────────────────────────────
//  Called every loop tick from Framework.  Throttles writes to
//  SD_LOG_INTERVAL_MS and flushes the file every Nth row.
void SDLogger::update() {
  if (!enabled || !_stats.active) return;
  uint32_t now = millis();
  if (now - _lastWrite < SD_LOG_INTERVAL_MS) return;
  _lastWrite = now;
  _writeRow();
}

// ── _writeRow ─────────────────────────────────────────────────
//  Append one CSV row matching the header layout: uptime in
//  seconds, then every active plugin's readings in the same
//  order as they appeared in _writeHeader().  Numeric values use
//  4 decimals (Arduino's default for float -> Print); strings get
//  quoted if they happen to contain a comma.
void SDLogger::_writeRow() {
  if (!_file) return;

  String row;
  row.reserve(128);

  // ── datetime column ─────────────────────────────────────
  // ISO 8601 if NTP-synced, blank string otherwise.  Blank
  // (not "1970-01-01...") makes the offline-boot case obvious
  // to anyone scanning the CSV and avoids fake-precise fields
  // that could mislead downstream analyses.
  char ts[24];
  if (_fw->nowIso8601(ts, sizeof(ts))) {
    row += ts;
  }
  row += ',';

  // ── uptime column ───────────────────────────────────────
  row += String(millis() / 1000);

  for (auto* p : _fw->plugins()) {
    if (!p->active) continue;
    SensorVal vals[16];
    uint8_t n = 0;
    p->getReadings(vals, n);
    for (uint8_t i = 0; i < n; i++) {
      row += ',';
      row += String(vals[i].value, 4);
    }
  }
  row += '\n';

  size_t wrote = _file.print(row);
  if (wrote != row.length()) {
    _stats.writeFailures++;
    Serial.printf("[SD] short write: wrote %u of %u bytes\n",
                  (unsigned)wrote, (unsigned)row.length());
  }
  _stats.bytesWritten += wrote;
  _stats.rowsWritten++;
  _stats.lastWriteMs = millis();

  if (++_rowsSinceFlush >= SD_LOG_FLUSH_EVERY_N) {
    _commit();
  }
}

// ── _commit ───────────────────────────────────────────────────
//  Close the file then immediately reopen it in append mode.
//
//  Why not just _file.flush()?  flush() maps to fsync(), which is
//  *supposed* to write the FAT directory entry — but whether it
//  actually does is inconsistent across arduino-esp32 core
//  versions and SD-over-SPI quirks.  The symptom of a flush that
//  didn't commit the directory entry is exactly what was seen:
//  the firmware's byte/row counters climb, yet a card read on a
//  PC shows no file at all (the data clusters were written but
//  the directory never recorded the file's existence/size).
//
//  close() has no such ambiguity — it always finalises the
//  directory entry.  Reopening with FILE_APPEND seeks to end and
//  carries on.  At the framework's logging rate (one row every
//  SD_LOG_INTERVAL_MS) the close/reopen cost is negligible.
bool SDLogger::_commit() {
  if (!_file) return false;
  _file.close();
  _file = SD.open(_stats.filename.c_str(), FILE_APPEND);
  if (!_file) {
    Serial.printf("[SD] reopen FAILED after commit (%s) — logging stopped\n",
                  _stats.filename.c_str());
    _stats.active = false;
    _stats.writeFailures++;
    return false;
  }
  _rowsSinceFlush = 0;
  return true;
}

// ── flush ─────────────────────────────────────────────────────
//  Manual commit trigger (close + reopen).  Exposed via
//  GET /api/sdcard/flush so you can guarantee everything written
//  so far is on the card without waiting for the automatic
//  every-N-rows commit.
bool SDLogger::flush() {
  if (!_stats.active || !_file) return false;
  return _commit();
}

// ── eject ─────────────────────────────────────────────────────
//  Cleanly close the file and unmount the card so it can be
//  physically removed with zero risk of a half-written FAT.
//  Logging stops until the next reboot — there's no card-detect
//  line on these boards to notice a reinsert, and the framework
//  deliberately avoids hot-plug.  Exposed via GET /api/sdcard/eject.
// ── logAlert ──────────────────────────────────────────────────
//  Append one CSV line to /alerts.csv, opened + closed per write so
//  the row is committed immediately (alerts are rare, so the cost of
//  a per-write open/close is irrelevant).  Kept separate from the
//  sensor log so neither file's format corrupts the other.
void SDLogger::logAlert(const String& line) {
  if (!enabled || !_stats.present) return;
  File f = SD.open("/alerts.csv", FILE_APPEND);
  if (!f) return;
  f.println(line);
  f.close();
}

bool SDLogger::eject() {
  if (!_stats.active) return false;
  if (_file) _file.close();
  SD.end();
  _stats.active = false;
  Serial.println(F("[SD] ejected — card safe to remove.  "
                   "Reboot to resume logging."));
  return true;
}
#endif  // OUT_SD_LOG
