// ============================================================
//  Framework.cpp
// ============================================================
#include "Framework.h"
#include "IPinDevice.h"   // non-I2C pin devices — activated after the I2C scan
#include <cstdio>         // snprintf

Framework::Framework() = default;
Framework::~Framework() { for (auto* p : _plugins) delete p; }

void Framework::addPlugin(IDevice* p) { _plugins.push_back(p); }

void Framework::addMux(uint8_t addr) {
  _muxes.push_back({addr, false});
}

// ── _setMuxChannel ────────────────────────────────────────────
//  Single-byte write to the PCA9548A control register selects
//  which of its 8 downstream channels are connected to the
//  upstream bus.  The byte is a bitmap: 0x01 = ch0 only,
//  0x02 = ch1 only, ..., 0x80 = ch7 only.  Writing 0x00
//  disconnects all channels (the bus's safe "park" state).
//
//  Called with muxAddr == 0 to mean "no mux to select" — used
//  by update() so it can pass every plugin through the same
//  selection call regardless of whether the plugin lives behind
//  a mux or on the root bus.
void Framework::_setMuxChannel(TwoWire* bus, uint8_t muxAddr, int8_t channel) {
  if (muxAddr == 0) return;
  bus->beginTransmission(muxAddr);
  if (channel < 0 || channel > 7) bus->write((uint8_t)0x00);
  else                            bus->write((uint8_t)(1 << channel));
  bus->endTransmission();
}

// ── begin ─────────────────────────────────────────────────────
void Framework::begin() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Force Grove Port-A 5V on.  M5.begin()'s default for Core2
  // depends on M5Unified version and is not reliably true — when
  // it's left off, external sensors ACK the boot scan on residual
  // cap then go unresponsive seconds later, which looks like a
  // plugin bug but is actually the rail browning out.  Always
  // enable it explicitly here.  Safe no-op on CoreS3 / unknown.
  M5.Power.setExtOutput(true);

  _board = &BoardInfo::detect();

  serial.begin();

  Serial.println(F("\n============================================"));
  Serial.printf("  %s  I2C Framework  –  Booting\n", _board->longName);
  Serial.println(F("============================================"));
  Serial.println(F("[Build] 2026-05-24g (cpplint cleanup)"));
  Serial.printf("[Board] Detected: %s\n", _board->longName);

  _initBuses();
  display.begin(*_board);
  _connectWiFi();
  _scanAndBind();

  // ── Activate non-I2C pin devices ──────────────────────────
  //  Pin devices (IPinDevice) advertise no I2C address, so the
  //  scan above never binds them.  Configure their GPIO/PWM/ADC
  //  pins here and mark them active.  There is no detection — a
  //  pin device is trusted because the sketch registered it.
  for (auto* p : _plugins) {
    if (!p->isPinDevice() || p->active) continue;
    auto* pd = static_cast<IPinDevice*>(p);
    if (pd->beginPins()) {
      pd->active = true;
      Serial.printf("[Pin] %-22s ready  (slug '%s')\n",
                    pd->name(), pd->slug());
    } else {
      Serial.printf("[Pin] %-22s beginPins() FAILED\n", pd->name());
    }
  }

  // SDLogger must come after _scanAndBind() so its CSV header
  // reflects the final set of active plugins.  Order is otherwise
  // independent of webApi / mqtt — they don't share resources.
  sdlog.begin(this);
  webApi.begin(this);
  mqtt.begin(this);

  uint8_t active = 0;
  for (auto* p : _plugins) if (p->active) active++;
  Serial.printf("[Framework] Ready – %u plugin(s) active\n", active);
}

// ── update ────────────────────────────────────────────────────
void Framework::update() {
  M5.update();
  uint32_t now = millis();

  // ── Fast path ─────────────────────────────────────────────
  //  Plugins that opt in via wantsFastPoll() are serviced on EVERY
  //  loop iteration, not just every POLL_MS.  Needed for waveform
  //  work like MAX30100 beat detection, where the sensor's FIFO
  //  would overflow at the slow poll rate.  Select the plugin's mux
  //  channel first, exactly as the regular poll does, so a fast
  //  plugin mounted behind an I2C hub still talks to the right chip.
  for (auto* p : _plugins) {
    if (!p->active || !p->wantsFastPoll()) continue;
    _setMuxChannel(p->bus, p->muxAddr, p->muxChannel);
    p->fastPoll();
  }

  if (now - _lastPoll >= POLL_MS) {
    for (auto* p : _plugins) {
      if (!p->active) continue;
      // Select this plugin's mux channel before talking to it.
      // No-op for root-bus plugins (muxAddr == 0).
      _setMuxChannel(p->bus, p->muxAddr, p->muxChannel);
      p->update();
    }
    _lastPoll = now;
    serial.update(this);
  }

  webApi.update();
  mqtt.update();
  sdlog.update();
  display.update(this);
}

// ── _recoverBus ───────────────────────────────────────────────
//  Manual SCL toggling to free a slave that's stuck holding SDA
//  low from an interrupted prior transaction.  Common scenario:
//  the ESP32 soft-resets in the middle of an I2C read; the slave
//  was about to clock out a data bit, sees SCL go away mid-byte,
//  and keeps SDA pulled low waiting for the next clock.  After
//  reboot, Wire.begin() can't issue a valid START (which needs
//  SDA high) and every transaction fails — sometimes with err=4
//  on endTransmission, sometimes with the slave going silent.
//
//  Pulsing SCL up to 9 times (one per bit in a byte, plus the
//  ACK slot) lets the stuck slave finish its phantom byte and
//  release SDA.  Then we issue a manual STOP and hand the pins
//  back to the Wire driver for begin().  Safe no-op if nothing
//  was stuck to begin with.
void Framework::_recoverBus(uint8_t sda, uint8_t scl) {
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT);

  // Pulse SCL until SDA goes high (max 9 cycles).
  for (uint8_t i = 0; i < 9; i++) {
    if (digitalRead(sda) == HIGH) break;
    digitalWrite(scl, LOW);
    delayMicroseconds(10);
    digitalWrite(scl, HIGH);
    delayMicroseconds(10);
  }

  // Manual STOP: SDA low → high while SCL is high.
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(10);
  digitalWrite(scl, HIGH);
  delayMicroseconds(10);
  digitalWrite(sda, HIGH);
  delayMicroseconds(10);

  // Release pins so Wire can claim them.
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
}

// ── _initBuses ────────────────────────────────────────────────
void Framework::_initBuses() {
  // Recover the internal bus first — if anything is stuck holding
  // SDA low across a soft-reset, the subsequent Wire.begin() can't
  // emit a valid START and the entire bus is hosed until power
  // cycle.  Cheap insurance: a few SCL pulses costs <1 ms.  On
  // boards with a separate external bus, recover that one too.
  _recoverBus(_board->i2cIntSda, _board->i2cIntScl);
  if (!_board->sharedBus()) {
    _recoverBus(_board->i2cExtSda, _board->i2cExtScl);
  }

  _board->intBus->begin(_board->i2cIntSda, _board->i2cIntScl, I2C_INT_FREQ);

  if (_board->sharedBus()) {
    // Core1 — Port-A is wired to the same SDA/SCL pair as the
    // internal chips, so there's only one bus to bring up.  Use the
    // internal speed (the external setting is irrelevant because the
    // bus already has to support the internal chips' timing).
    Serial.printf("[I2C] Shared bus SDA=%d SCL=%d @%luHz  "
                  "(internal + Port-A on same pins)\n",
                  _board->i2cIntSda, _board->i2cIntScl, I2C_INT_FREQ);
  } else {
    _board->extBus->begin(_board->i2cExtSda, _board->i2cExtScl, I2C_EXT_FREQ);
    Serial.printf("[I2C] Internal SDA=%d SCL=%d @%luHz  "
                  "External SDA=%d SCL=%d @%luHz\n",
                  _board->i2cIntSda, _board->i2cIntScl, I2C_INT_FREQ,
                  _board->i2cExtSda, _board->i2cExtScl, I2C_EXT_FREQ);
  }
}

// ── _connectWiFi ─────────────────────────────────────────────
void Framework::_connectWiFi() {
  if (!webApi.enabled) return;

  // No station SSID configured → run as our own access point so
  // the dashboard is still reachable.  (Config.h: leave WIFI_SSID
  // empty to select this mode.)
  if (WIFI_SSID[0] == '\0') {
    _startAccessPoint();
    return;
  }

  Serial.printf("[WiFi] Connecting to '%s'", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t > WIFI_TIMEOUT_MS) break;
    delay(300);
    Serial.print('.');
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.printf("\n[WiFi] Connected  IP=%s\n", ip.c_str());
    display.showWiFi(WIFI_SSID, ip);
    _syncTime();
  } else {
    Serial.println(F("\n[WiFi] Failed – web API disabled"));
    webApi.enabled = false;
    display.showError("WiFi failed\nWeb API disabled");
    delay(2000);
  }
}

// ── _startAccessPoint ────────────────────────────────────────
//  Brings the device up as a standalone WPA2 access point
//  (Config.h AP_SSID / AP_PASSWORD) instead of joining a network.
//  Selected automatically when WIFI_SSID is empty.  There is no
//  upstream internet, so NTP is deliberately skipped — the SD
//  logger and /api fall back to uptime-relative timestamps,
//  exactly as they do after a failed NTP sync.
void Framework::_startAccessPoint() {
  Serial.printf("[WiFi] No WIFI_SSID set — starting access point '%s'\n",
                AP_SSID);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    // softAP() almost always fails for one reason: AP_PASSWORD is
    // shorter than WPA2's 8-character minimum.
    Serial.println(F("\n[WiFi] softAP() failed — web API disabled.  "
                     "AP_PASSWORD must be 8-63 chars."));
    webApi.enabled = false;
    display.showError("AP start failed\nWeb API disabled");
    delay(2000);
    return;
  }
  _apMode = true;
  String ip = WiFi.softAPIP().toString();
  Serial.printf("[WiFi] Access point up — SSID '%s'  IP=%s\n",
                AP_SSID, ip.c_str());
  display.showWiFi(AP_SSID, ip);
}

// ── _syncTime ────────────────────────────────────────────────
//  Pull wall-clock time from NTP after the WiFi link comes up.
//  Used by SDLogger to build a datetime-stamped filename and to
//  put an ISO 8601 timestamp on every row; also surfaced through
//  the WebAPI so the dashboard can show the device's clock.
//
//  configTzTime() sets the timezone via POSIX TZ string and kicks
//  off an asynchronous SNTP query.  We then poll getLocalTime()
//  for up to NTP_TIMEOUT_MS milliseconds.  Failure is non-fatal —
//  the framework runs fine with uptime-only timestamps.
void Framework::_syncTime() {
  Serial.printf("[Time] syncing with %s (tz=%s) ...", NTP_SERVER, NTP_TZ);
  configTzTime(NTP_TZ, NTP_SERVER);

  uint32_t t = millis();
  struct tm tm;
  while (!getLocalTime(&tm, 100)) {
    if (millis() - t > NTP_TIMEOUT_MS) {
      Serial.println(F(" timeout (no NTP)"));
      _timeSynced = false;
      return;
    }
    Serial.print('.');
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
  Serial.printf(" %s\n", buf);
  _timeSynced = true;

  // Push the freshly-synced wall-clock into the board's hardware
  // RTC (BM8563 on Core2 / CoreS3 / Tough).  Without this the
  // BM8563 just keeps whatever it was last set to, so Plugin_RTC
  // and the dashboard would show a stale clock; writing it here
  // keeps the hardware RTC correct and gives the board a sensible
  // fallback time if a later boot can't reach NTP.  The local
  // wall-clock is stored — the same value the framework displays.
  // M5.Rtc.isEnabled() is false on boards with no RTC chip (the
  // Core1), where this is simply skipped.
  if (M5.Rtc.isEnabled()) {
    M5.Rtc.setDateTime(&tm);
    Serial.println(F("[Time] hardware RTC set from NTP"));
  }
}

// ── nowIso8601 ────────────────────────────────────────────────
//  Format the current wall-clock as "YYYY-MM-DDTHH:MM:SS" into a
//  caller-provided buffer.  Returns true if the time has been
//  NTP-synced (so the value is real wall-clock), false if not (so
//  the value reflects whatever the ESP32's RTC came up with after
//  reset, typically 1970-01-01).  Used by SDLogger and WebAPI.
bool Framework::nowIso8601(char* buf, size_t n) const {
  struct tm tm;
  if (!getLocalTime(&tm, 0)) {
    if (n > 0) buf[0] = '\0';
    return false;
  }
  strftime(buf, n, "%Y-%m-%dT%H:%M:%S", &tm);
  return _timeSynced;
}

// ── _probe ────────────────────────────────────────────────────
bool Framework::_probe(TwoWire* w, uint8_t addr) {
  w->beginTransmission(addr);
  return w->endTransmission() == 0;
}

// ── _scanAndBind ──────────────────────────────────────────────
//  Boot-time bind of every plugin to whichever physical device
//  ACKs on the matching bus + address.  When two plugins share
//  an I2C address (Heart vs Ultrasonic at 0x57; ToF vs Color at
//  0x29), only one is allowed to claim the slot — the first one
//  (in registration order) whose begin() succeeded gets the
//  slot; subsequent plugins sharing that address are skipped.
//
//  Also exposed via Framework::rescanAll() so the WebAPI can
//  manually re-run a bind pass after the user has plugged in a
//  new sensor.  Re-running is safe but not hot-plug safe — i.e.
//  the chip you plugged in needs to have been powered before
//  this call for its I2C state machine to be valid.
void Framework::_scanAndBind() {
  Serial.println(F("[Scan] Scanning I2C buses..."));

  // Park every registered hub (all channels off) BEFORE probing the
  // root bus.  A PCA9548A holds its last channel selection for as
  // long as it stays powered, and a firmware upload resets the ESP32
  // WITHOUT dropping Port-A's 5V — so the hub usually still has
  // whatever channel the previous run's fast-path left enabled.
  // Probing the root bus without parking first lets that channel's
  // devices leak into the root scan as phantoms; a permissive plugin
  // then binds to a "root" device that is really behind the hub.
  // scanReport() already guards the live /api/scan this way — the
  // boot scan must do the same.
  for (auto& mux : _muxes) {
    _setMuxChannel(_board->extBus, mux.addr, -1);
  }

  uint8_t intA[32], extA[32];
  uint8_t intN = 0, extN = 0;

  for (uint8_t a = 1; a < 127; a++) {
    if (_probe(_board->intBus, a) && intN < 32) intA[intN++] = a;
    // Avoid double-scanning when intBus and extBus are the same
    // physical controller (Core1 shared-bus case).
    if (!_board->sharedBus() &&
        _probe(_board->extBus, a) && extN < 32) extA[extN++] = a;
  }

  // Raw internal-bus device count, captured before the mux
  // addresses are stripped out below.  Used by the bus-health
  // check after the scan summary: a healthy M5Stack always has
  // on-board chips on the internal bus, so a raw count of zero
  // means the bus is electrically dead, not merely empty.
  const uint8_t rawIntN = intN;

  // ── Mux detection ─────────────────────────────────────────
  //  For each registered I2C hub: probe its address, mark it
  //  present or not, and strip it from the regular scan list so
  //  sensor plugins don't try to bind to the mux itself.  Also
  //  park each present mux with all channels disabled — the
  //  channel-by-channel scan below selects them one at a time.
  auto stripAddr = [](uint8_t* arr, uint8_t& n, uint8_t a) {
    for (uint8_t i = 0; i < n; ) {
      if (arr[i] == a) {
        for (uint8_t j = i; j + 1 < n; j++) arr[j] = arr[j + 1];
        n--;
      } else {
        i++;
      }
    }
  };
  for (auto& mux : _muxes) {
    uint8_t* scanArr = _board->sharedBus() ? intA : extA;
    uint8_t& scanN   = _board->sharedBus() ? intN : extN;
    bool found = false;
    for (uint8_t i = 0; i < scanN; i++) {
      if (scanArr[i] == mux.addr) {
        found = true;
        break;
      }
    }
    mux.present = found;
    if (mux.present) {
      Serial.printf("[Scan] I2C hub detected at 0x%02X\n", mux.addr);
      _setMuxChannel(_board->extBus, mux.addr, -1);
      stripAddr(scanArr, scanN, mux.addr);
    } else {
      Serial.printf("[Scan] I2C hub at 0x%02X registered but not on bus\n",
                    mux.addr);
    }
  }

  if (_board->sharedBus()) {
    Serial.printf("[Scan] Shared bus: %u device(s)\n", intN);
  } else {
    Serial.printf("[Scan] Internal: %u  External: %u\n", intN, extN);
  }

  // ── Bus-health check ──────────────────────────────────────
  //  Every M5Stack board carries on-board chips on the internal
  //  I2C bus (PMIC / IMU / touch / RTC, model depending — on the
  //  Core1 the IP5306 at 0x75 alone is always present).  So the
  //  raw internal scan should never come back completely empty.
  //  When it does, the bus is electrically dead rather than just
  //  unpopulated: almost always a faulty or unpowered device on
  //  the wires is holding SDA or SCL low, which corrupts every
  //  other device's signalling.  On a shared-bus Core1 a single
  //  bad stacked module takes the whole bus — hub and sensors
  //  included — down with it.
  if (rawIntN == 0) {
    Serial.println(F("[I2C] *** WARNING: bus appears HELD LOW ***"));
    Serial.println(F("[I2C]   Internal scan found 0 devices — not even the"));
    Serial.println(F("[I2C]   on-board power chip.  The bus is electrically"));
    Serial.println(F("[I2C]   dead, not just empty."));
    Serial.println(F("[I2C]   Likely cause: a faulty or UNPOWERED stacked"));
    Serial.println(F("[I2C]   module clamping SDA/SCL low.  Remove stacked"));
    Serial.println(F("[I2C]   modules one at a time to find the culprit."));
  }

  // Dump raw addresses found — useful diagnostic when a plugin
  // doesn't bind: tells you whether the chip is even on the bus
  // (and what unhandled chips like IP5306 happen to be present).
  if (intN > 0) {
    Serial.print(F("[Scan] Addrs (internal):"));
    for (uint8_t i = 0; i < intN; i++) Serial.printf(" 0x%02X", intA[i]);
    Serial.println();
  }
  if (!_board->sharedBus() && extN > 0) {
    Serial.print(F("[Scan] Addrs (external):"));
    for (uint8_t i = 0; i < extN; i++) Serial.printf(" 0x%02X", extA[i]);
    Serial.println();
  }

  display.showScan(intA, intN, true);
  if (!_board->sharedBus()) display.showScan(extA, extN, false);

  // ── Strip the host board's reserved built-in addresses ────
  //  Some boards carry chips on the internal bus that the framework
  //  has no plugin for — chiefly the Core2 / CoreS3 FT6336U touch
  //  controller at 0x38.  Its bare I2C ACK would otherwise be
  //  claimed by a module plugin that shares the address (the
  //  GoPlus2 module is also 0x38), so the device shows up
  //  "attached" when it is really the board's own silicon.  Drop
  //  these from the internal bind list — they still appear in the
  //  raw address dump above for diagnostics.  External (Port-A)
  //  addresses are never reserved.  (The Core2 v1.1's INA3221 at
  //  0x40 is NOT reserved — Plugin_INA3221 claims it with a
  //  positive die-ID check.)
  for (uint8_t i = 0; i < _board->reservedCount; i++) {
    uint8_t ra = _board->reservedAddr[i];
    bool present = false;
    for (uint8_t j = 0; j < intN; j++) {
      if (intA[j] == ra) {
        present = true;
        break;
      }
    }
    if (present) {
      Serial.printf("[Scan] 0x%02X reserved — %s built-in chip, "
                     "not offered to plugins\n", ra, _board->shortName);
      stripAddr(intA, intN, ra);
    }
  }

  auto isClaimed = [this](TwoWire* w, uint8_t a) {
    for (auto* p : _plugins) {
      if (p->active && p->bus == w && p->addr == a) return true;
    }
    return false;
  };

  // On a shared-bus board, any "external" plugin should fall back to
  // looking at the internal address list — they live on the same bus.
  const uint8_t* effExtA = _board->sharedBus() ? intA : extA;
  uint8_t        effExtN = _board->sharedBus() ? intN : extN;

  for (auto* p : _plugins) {
    if (p->active) continue;

    uint8_t pAddrs[8]; uint8_t pN = 0;
    p->i2cAddresses(pAddrs, pN);

    for (uint8_t ai = 0; ai < pN; ai++) {
      uint8_t a = pAddrs[ai];
      I2CBus bus = p->preferredBus();

      if (bus == I2CBus::Internal || bus == I2CBus::Both) {
        for (uint8_t di = 0; di < intN; di++) {
          if (intA[di] != a) continue;
          if (isClaimed(_board->intBus, a)) {
            Serial.printf("[Scan] %-22s 0x%02X  (internal) skipped — claimed\n",
                          p->name(), a);
            goto nextPlugin;
          }
          Serial.printf("[Scan] %-22s 0x%02X  (internal)\n", p->name(), a);
          if (p->begin(_board->intBus, a)) {
            p->active = true; p->addr = a; p->bus = _board->intBus;
          } else {
            Serial.printf("[Scan]   begin() failed for %s\n", p->name());
          }
          goto nextPlugin;
        }
      }

      if (bus == I2CBus::External || bus == I2CBus::Both) {
        for (uint8_t di = 0; di < effExtN; di++) {
          if (effExtA[di] != a) continue;
          if (isClaimed(_board->extBus, a)) {
            Serial.printf("[Scan] %-22s 0x%02X  (external) skipped — claimed\n",
                          p->name(), a);
            goto nextPlugin;
          }
          Serial.printf("[Scan] %-22s 0x%02X  (external)\n", p->name(), a);
          if (p->begin(_board->extBus, a)) {
            p->active = true; p->addr = a; p->bus = _board->extBus;
          } else {
            Serial.printf("[Scan]   begin() failed for %s\n", p->name());
          }
          goto nextPlugin;
        }
      }
    }
    nextPlugin: { /* goto target - advance to the next plugin */ }
  }

  // ── Mux channel binding ───────────────────────────────────
  //  For each present hub, iterate its 8 downstream channels.
  //  On each channel we re-probe the bus and try to bind any
  //  still-unbound external plugin to whatever turned up.  This
  //  lets two NCIR2 sensors at the same 0x5A address co-exist
  //  on different channels: register two Plugin_NCIR2 instances
  //  and the framework binds the first to ch0/0x5A and the
  //  second to ch1/0x5A, etc.  Mux is left parked (channels
  //  off) after enumeration; update() re-selects per plugin.
  for (auto& mux : _muxes) {
    if (!mux.present) continue;

    for (uint8_t ch = 0; ch < 8; ch++) {
      _setMuxChannel(_board->extBus, mux.addr, ch);

      uint8_t chA[32]; uint8_t chN = 0;
      for (uint8_t a = 1; a < 127; a++) {
        // Skip any registered mux address — the mux intercepts
        // its own address regardless of channel selection, so it
        // would always falsely appear "present on every channel".
        bool isMuxAddr = false;
        for (auto& m : _muxes) {
          if (m.addr == a) {
            isMuxAddr = true;
            break;
          }
        }
        if (isMuxAddr) continue;
        if (_probe(_board->extBus, a) && chN < 32) chA[chN++] = a;
      }
      if (chN == 0) continue;

      Serial.printf("[Scan] Hub 0x%02X ch%u:", mux.addr, ch);
      for (uint8_t i = 0; i < chN; i++) Serial.printf(" 0x%02X", chA[i]);
      Serial.println();

      for (auto* p : _plugins) {
        if (p->active) continue;
        I2CBus pref = p->preferredBus();
        if (pref != I2CBus::External && pref != I2CBus::Both) continue;

        uint8_t pAddrs[8]; uint8_t pN = 0;
        p->i2cAddresses(pAddrs, pN);

        for (uint8_t ai = 0; ai < pN; ai++) {
          uint8_t a = pAddrs[ai];
          bool match = false;
          for (uint8_t i = 0; i < chN; i++) {
            if (chA[i] == a) {
              match = true;
              break;
            }
          }
          if (!match) continue;

          // Has any previously-registered plugin already claimed this
          // exact (hub, channel, addr) tuple?  Two plugins that share an
          // I2C address (e.g. Heart + Ultrasonic at 0x57, ToF + Color at
          // 0x29) must not both bind to the same physical device — they'd
          // corrupt each other's I2C transactions every loop.  Registration
          // order = priority: the strict plugin (with a WHO_AM_I / PART_ID
          // check) is registered first and wins; the permissive plugin is
          // skipped here just like on the root bus.
          bool alreadyClaimed = false;
          for (auto* other : _plugins) {
            if (other != p && other->active
                && other->bus == _board->extBus
                && other->muxAddr == mux.addr
                && other->muxChannel == ch
                && other->addr == a) {
              alreadyClaimed = true;
              break;
            }
          }
          if (alreadyClaimed) {
            Serial.printf("[Scan] %-22s 0x%02X  (hub 0x%02X ch%u) "
                          "skipped — claimed\n",
                          p->name(), a, mux.addr, ch);
            break;  // this plugin is done; move on
          }

          // ── Root-bus collision guard ──────────────────────────
          //  A hub channel does NOT isolate a device from the root
          //  bus.  When a channel is selected the hub joins it to the
          //  upstream wires, so every root-bus device is on the bus
          //  too.  If the SAME address also answers on the root bus,
          //  the two chips are BOTH live whenever this channel is
          //  selected — a hard I2C address collision that corrupts
          //  every transaction with either one.  No firmware can fix
          //  this (an I2C chip cannot be muted); the user must move a
          //  unit.  Detect it for certain: park the hub (all channels
          //  off) and see whether the address still ACKs from root.
          _setMuxChannel(_board->extBus, mux.addr, -1);
          bool rootCollision = _probe(_board->extBus, a);
          _setMuxChannel(_board->extBus, mux.addr, ch);
          if (rootCollision) {
            Serial.printf("[Scan] *** 0x%02X ADDRESS CONFLICT: also live on "
                          "the root bus ***\n", a);
            Serial.printf("[Scan]   %s on hub 0x%02X ch%u NOT bound — it "
                          "collides with the root-bus device at 0x%02X "
                          "whenever ch%u is selected.\n",
                          p->name(), mux.addr, ch, a, ch);
            Serial.printf("[Scan]   Fix: move the root-bus 0x%02X unit onto a "
                          "spare hub channel (unplugging is not enough — the "
                          "chip collides even with no plugin bound).\n", a);
            break;  // cannot bind this plugin here — move on
          }

          Serial.printf("[Scan] %-22s 0x%02X  (hub 0x%02X ch%u)\n",
                        p->name(), a, mux.addr, ch);
          if (p->begin(_board->extBus, a)) {
            p->active     = true;
            p->addr       = a;
            p->bus        = _board->extBus;
            p->muxAddr    = mux.addr;
            p->muxChannel = ch;
          } else {
            Serial.printf("[Scan]   begin() failed for %s\n", p->name());
          }
          break;  // this plugin is done; move on
        }
      }
    }

    // Park the mux with all channels disabled before moving on
    // to the next mux (or returning).  update() will re-select
    // each plugin's channel as needed.
    _setMuxChannel(_board->extBus, mux.addr, -1);
  }
}

// ── scanReport ───────────────────────────────────────────────
//  Live I2C topology report used by /api/scan.  Probes the root
//  bus(es) and every channel of every registered hub.  Mux
//  addresses are filtered out of the root listing so they aren't
//  reported twice (they get their own "hubs" object).  All hubs
//  are parked (all channels off) at start and end so this is
//  side-effect-free with respect to plugin update() ordering.
void Framework::scanReport(JsonDocument& doc) {
  doc["uptime_s"] = millis() / 1000;

  // Park every mux before probing the root bus — otherwise a hub
  // left on a channel from the last update() would surface that
  // channel's devices as ghosts in the root listing.
  for (auto& m : _muxes) {
    if (m.present) _setMuxChannel(_board->extBus, m.addr, -1);
  }

  auto isRegisteredMux = [&](uint8_t a) {
    for (auto& m : _muxes) if (m.addr == a) return true;
    return false;
  };

  auto probeRange = [&](TwoWire* w, JsonArray& arr) {
    for (uint8_t a = 1; a < 127; a++) {
      if (isRegisteredMux(a)) continue;
      if (_probe(w, a)) {
        char buf[6]; snprintf(buf, sizeof(buf), "0x%02X", a);
        arr.add(buf);
      }
    }
  };

  // ── Root bus(es) ─────────────────────────────────────────
  if (_board->sharedBus()) {
    JsonArray shared = doc["shared"].to<JsonArray>();
    probeRange(_board->intBus, shared);
  } else {
    JsonArray internal_ = doc["internal"].to<JsonArray>();
    probeRange(_board->intBus, internal_);
    JsonArray external_ = doc["external"].to<JsonArray>();
    probeRange(_board->extBus, external_);
  }

  // ── Hubs and their channels ──────────────────────────────
  if (!_muxes.empty()) {
    JsonArray hubs = doc["hubs"].to<JsonArray>();
    for (auto& m : _muxes) {
      JsonObject h = hubs.add<JsonObject>();
      char abuf[6]; snprintf(abuf, sizeof(abuf), "0x%02X", m.addr);
      h["addr"]    = abuf;
      h["present"] = m.present;
      if (!m.present) continue;
      JsonArray chs = h["channels"].to<JsonArray>();
      for (uint8_t ch = 0; ch < 8; ch++) {
        _setMuxChannel(_board->extBus, m.addr, ch);
        JsonObject c = chs.add<JsonObject>();
        c["ch"] = ch;
        JsonArray devs = c["devices"].to<JsonArray>();
        for (uint8_t a = 1; a < 127; a++) {
          // Skip registered mux addresses — the mux intercepts its
          // own address on every channel.  We also skip them so a
          // hub doesn't "see itself" in its own channel listing.
          if (isRegisteredMux(a)) continue;
          if (_probe(_board->extBus, a)) {
            char buf[6]; snprintf(buf, sizeof(buf), "0x%02X", a);
            devs.add(buf);
          }
        }
      }
      // Park this mux before moving on / returning.
      _setMuxChannel(_board->extBus, m.addr, -1);
    }
  }
}
