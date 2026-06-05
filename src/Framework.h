#pragma once
// ============================================================
//  Framework.h  –  Core orchestrator
//
//  Manages bus init, plugin scanning, polling, and all output
//  channels (Web, Serial, Display).  Works on both M5Stack CoreS3
//  and M5Stack Core2 — pins and built-in chip identities are
//  resolved at runtime via BoardInfo.
// ============================================================
#include <M5Unified.h>
#include <Wire.h>
#include <vector>

#include "Config.h"
#include "BoardInfo.h"
#include "IDevice.h"
#include "DisplayManager.h"
#include "WebAPI.h"
#include "SerialOut.h"
#include "MQTTOut.h"
#include "SDLogger.h"
#include "AlertManager.h"

class Framework {
 public:
  Framework();
  ~Framework();

  // Register a plugin before begin()
  void addPlugin(IDevice* p);

  // Register a PCA9548A-style I2C hub (8-channel mux) on the
  // external bus.  Call BEFORE begin().  The most common case is
  // the M5Stack PaHUB at address 0x70 — call addMux(0x70) once
  // before adding any plugins that live behind it.
  //
  // After registration the framework will:
  //   1. detect the mux during the boot scan,
  //   2. scan each of its 8 channels for I2C devices,
  //   3. bind any unbound plugins to the (mux, channel, addr)
  //      tuples it finds, and
  //   4. transparently select the right channel before every
  //      call into a plugin behind that mux.
  //
  // You can register multiple muxes (e.g. two PaHUBs at 0x70 and
  // 0x71) but they all share the external bus.  No support for
  // cascading muxes (mux-behind-a-mux).
  //
  // Note: the PCA9548A intercepts at its own address, so devices
  // at 0x70 (or whatever the mux address is) CANNOT be reached
  // through the mux — they have to be on the root bus instead.
  void addMux(uint8_t addr);

  // Call from setup() – initialises everything
  void begin();

  // Call from loop()
  void update();

  // Access plugin list (used by output modules)
  std::vector<IDevice*>& plugins() { return _plugins; }

  // Detected board info (valid after begin())
  const BoardInfo& board() const { return *_board; }

  // True once an NTP sync has succeeded.  When false, getLocalTime()
  // still returns *something* (the ESP32's RTC starts at the Unix
  // epoch after reset and ticks from there), but callers that want
  // real wall-clock time should treat that as a fallback.
  bool timeSynced() const { return _timeSynced; }

  // True when the device is running as its own Wi-Fi access point
  // (Config.h WIFI_SSID left empty) instead of joined to a network
  // as a station.
  bool apMode() const { return _apMode; }

  // True when the device dropped into the AP because it could NOT
  // join its configured Wi-Fi (a provisioned unit with bad/missing
  // network).  WebAPI uses this to serve the setup portal — instead
  // of the dashboard — so the Wi-Fi credentials can be corrected
  // without erasing flash.
  bool forceSetupPortal() const { return _forceSetupPortal; }

  // Format the current time as "YYYY-MM-DDTHH:MM:SS" into the
  // caller's buffer.  Returns the same boolean as timeSynced() —
  // true when the formatted string is real wall-clock, false when
  // it's just uptime-since-reset cast through localtime.
  bool nowIso8601(char* buf, size_t n) const;

  // Fill a JsonDocument with a live I2C scan suitable for /api/scan.
  // Reports the root bus(es) and walks every registered hub's 8
  // downstream channels.  Shared-bus boards (Core1) get a single
  // "shared" array; boards with separate buses get "internal" +
  // "external" arrays.  Hubs are listed in a "hubs" array, with one
  // entry per registered hub address and a "channels" sub-array of
  // device addresses present on each of the 8 downstream channels.
  // Registered mux addresses are deliberately excluded from the root
  // bus arrays — they get reported under "hubs" instead, not twice.
  void scanReport(JsonDocument& doc);

  // ── Manual rescan trigger ─────────────────────────────────
  //  Re-runs the boot-time scan-and-bind from scratch.  Safe to
  //  call from the WebAPI handler — the web server runs in loop()
  //  context so there's no concurrency to worry about.  This is
  //  the ONLY rescan path — the framework no longer rescans
  //  periodically (see Config.h note).
  void rescanAll() { _scanAndBind(); }

  // ── Public output modules (toggleable at runtime) ─────────
  DisplayManager display;
  WebAPI webApi;
  SerialOut serial;
  MQTTOut mqtt;
  SDLogger sdlog;
  AlertManager alerts;

 private:
  std::vector<IDevice*> _plugins;
  const BoardInfo* _board = nullptr;
  uint32_t _lastPoll = 0;

  // Registered I2C hubs (PCA9548A on external bus).  Populated by
  // addMux() before begin(); `present` is filled in during the boot
  // scan after probing each address.  An entry with present=false
  // is harmless — the framework just skips it.
  struct MuxInfo {
    uint8_t addr;
    bool present;
  };
  std::vector<MuxInfo> _muxes;

  void _initBuses();
  void _recoverBus(uint8_t sda, uint8_t scl);
  void _checkFactoryResetHold();  // hold screen/button at boot → wipe + portal
  void _securityAudit();     // guard against default credentials at boot
  void _connectWiFi();
  void _startAccessPoint();  // softAP, used when WIFI_SSID is empty
  void _syncTime();
  void _scanAndBind();
  bool _probe(TwoWire* w, uint8_t addr);

  bool _timeSynced = false;
  bool _apMode = false;  // true once _startAccessPoint() succeeds
  bool _forceSetupPortal = false;  // STA join failed → serve portal on AP

  // Write the channel-select byte to a PCA9548A.  channel = 0..7
  // selects that channel; any other value (e.g. -1) writes 0x00
  // to disable all channels (safe state).  No-op when muxAddr is
  // 0 — used by update() to make root-bus plugins free.
  void _setMuxChannel(TwoWire* bus, uint8_t muxAddr, int8_t channel);
};
