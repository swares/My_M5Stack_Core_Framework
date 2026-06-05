#pragma once
// ============================================================
//  DisplayManager.h  –  All LCD output for the framework
// ============================================================
#include "Config.h"  // for the OUT_DISPLAY build switch

#if OUT_DISPLAY
#include <M5Unified.h>
#include "IDevice.h"
#include "BoardInfo.h"

class Framework;  // forward

// ── Display template IDs ──────────────────────────────────────
enum class Tpl : uint8_t {
  Splash = 0,
  WiFiInfo = 1,
  ScanInfo = 2,
  Plugin = 3,  // auto-selects single/multi-value layout
  Ticker = 4,  // scrolling line of all readings
  Error = 5,
};

// ============================================================
class DisplayManager {
 public:
  void begin(const BoardInfo& board);
  void update(Framework* fw);

  // Poll the outer buttons / title-line touch arrows and update
  // the sensor-detail selection.  Call once per loop() iteration,
  // right after M5.update().
  void handleInput(Framework* fw);

  // ── One-shot screens ──────────────────────────────────────
  void showSplash();
  void showWiFi(const String& ssid, const String& ip);
  void showScan(uint8_t* addrs, uint8_t n, bool isInternal);
  void showError(const String& msg);
  // Full-screen informational banner (amber header).  Used for the
  // random AP-password notice and other non-error call-outs.
  void showNotice(const String& title, const String& body);

  // ── Alert banner (AlertManager LCD sink) ──────────────────
  //  A coloured strip drawn over the top of whatever view is showing.
  //  setAlert() raises it; clearAlert() drops it and forces a clean
  //  redraw.  Severity 0/1/2 = info/warn/critical (→ header/amber/red).
  void setAlert(const String& text, uint8_t severity);
  void clearAlert();

 private:
  bool enabled = OUT_DISPLAY;
  int32_t _W = 0, _H = 0;
  bool _ready = false;
  const BoardInfo* _board = nullptr;

  // ── Sensor-detail view ────────────────────────────────────
  //  Detail shows one read-only sensor full-screen, navigated with
  //  the title-line arrows / outer buttons.  Overview is the
  //  all-sensors ticker (or fixed grid).  Detail is the default.
  enum class View : uint8_t { Detail, Overview };
  View _view = View::Detail;
  uint8_t _focusIdx = 0;      // index among read-only sensors
  bool _dirty = true;          // view needs an immediate redraw
  uint32_t _detailDrawn = 0;   // millis() of the last detail render
  uint32_t _overviewDrawn = 0; // millis() of the last overview render

  // Overview repaint cadence.  The scrolling ticker only needs a new
  // frame at roughly its scroll step (~30 FPS); repainting every loop
  // iteration just hammers the LCD/SD-shared SPI bus for no visible
  // gain.  The fixed grid changes only when readings refresh, so it
  // throttles to POLL_MS instead (see update()).
  static constexpr uint32_t OVERVIEW_FRAME_MS = 33;

  // Scroll ticker state
  int32_t _scrollX = 0;
  uint32_t _scrollMs = 0;
  String _ticker;
  uint32_t _tickerBuilt = 0;

  // Alert banner state (AlertManager LCD sink).
  bool _alertActive = false;
  uint8_t _alertSev = 0;
  String _alertText;

  // ── Internal renderers ────────────────────────────────────
  //  All render directly on M5.Display (no off-screen sprite —
  //  see DisplayManager.cpp for rationale).
  void _renderDetail(Framework* fw);
  void _renderReadings(IDevice* p);
  void _renderTime(Framework* fw);
  void _renderTicker(Framework* fw);
  void _renderFixed(Framework* fw);

  void _buildTicker(Framework* fw);
  void _header(const String& title, uint16_t col);
  void _headerNav(const String& title);
  void _footer();
  void _drawAlertBanner();  // overlay strip for an active alert

  // Nth (idx) active read-only sensor — a plugin that is active
  // and not controllable.  Always fills `total` with the read-only
  // sensor count; returns nullptr if idx is past the end.
  IDevice* _roSensor(Framework* fw, uint8_t idx, uint8_t& total);

  // Total detail-view panels: every active read-only sensor, plus
  // one for the clock when a synced wall-clock time is available.
  uint8_t _panelCount(Framework* fw);

  // Title used in the framework header / ticker.  Filled in by begin().
  String _frameworkTitle = "M5Stack I2C Framework";

  // Device IP, captured by showWiFi() (station or AP mode).  Shown
  // in the footer so it stays on screen, not just the boot splash.
  String _ip;

  // Layout
  static constexpr int HEADER_H = 46;  // title-band height (px)
  static constexpr int ARROW_W = 56;   // nav-arrow touch zone width

  // Palette
  static constexpr uint16_t C_BG = 0x0841;
  static constexpr uint16_t C_HDR = 0x04BF;
  static constexpr uint16_t C_ACNT = 0xFD20;
  static constexpr uint16_t C_TEXT = 0xFFFF;
  static constexpr uint16_t C_DIM = 0x8410;
  static constexpr uint16_t C_ERR = 0xF800;
  static constexpr uint16_t C_OK = 0x07E0;
  static constexpr uint16_t C_BAND = 0x2104;
};

#else                 // !OUT_DISPLAY
#include <Arduino.h>  // String / uint8_t for the stub signatures
// ── Stub — LCD output compiled out via Config.h (OUT_DISPLAY = false).
//  Mirrors the real class's public surface so Framework keeps
//  building; nothing is drawn to the screen.
class Framework;
class BoardInfo;
class DisplayManager {
 public:
  bool enabled = false;
  void begin(const BoardInfo&) {}
  void update(Framework*) {}
  void handleInput(Framework*) {}
  void showSplash() {}
  void showWiFi(const String&, const String&) {}
  void showScan(uint8_t*, uint8_t, bool) {}
  void showError(const String&) {}
  void showNotice(const String&, const String&) {}
  void setAlert(const String&, uint8_t) {}
  void clearAlert() {}
};
#endif                // OUT_DISPLAY
