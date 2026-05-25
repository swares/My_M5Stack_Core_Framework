// ============================================================
//  DisplayManager.cpp
//
//  Renders directly on M5.Display (no off-screen back-buffer).
//
//  Rationale: a 320x240x16 sprite (~150 KB) doesn't reliably fit
//  in the Core2's internal DMA-capable SRAM after M5.begin() +
//  WiFi prep, and a PSRAM-resident M5Canvas hits known issues
//  with certain LovyanGFX rendering paths (e.g. the legacy GLCD
//  font / Font0).  Direct rendering on M5.Display is rock-solid
//  on both CoreS3 and Core2; the small amount of redraw flicker
//  is acceptable for a 2 Hz dashboard, and we minimise it by
//  bracketing each frame with startWrite()/endWrite() so the SPI
//  bus stays claimed for the duration of a full screen update.
// ============================================================
#include "Config.h"        // OUT_DISPLAY — must precede the #if
#if OUT_DISPLAY
#include "DisplayManager.h"
#include "Framework.h"
#include <algorithm>       // std::max
#include <cstdio>          // snprintf

// ── begin ─────────────────────────────────────────────────────
void DisplayManager::begin(const BoardInfo& board) {
  _board = &board;
  _frameworkTitle = String(board.shortName) + "  I2C Framework";
  if (!enabled) return;
  _W = M5.Display.width();
  _H = M5.Display.height();
  M5.Display.setBrightness(DISPLAY_BRIGHTNESS);

  Serial.printf("[Display] direct-render mode  %dx%d  brightness=%d\n",
                static_cast<int>(_W), static_cast<int>(_H), DISPLAY_BRIGHTNESS);

  _ready = true;
  showSplash();
}

// ── showSplash ────────────────────────────────────────────────
void DisplayManager::showSplash() {
  if (!_ready) return;
  M5.Display.startWrite();
  M5.Display.fillScreen(C_BG);
  // Title band
  M5.Display.fillRect(0, 0, _W, 50, C_HDR);
  M5.Display.setTextColor(C_TEXT, C_HDR);
  M5.Display.drawCentreString(_board ? _board->longName : "M5Stack",
                              _W/2, 8,  &fonts::FreeSansBold9pt7b);
  M5.Display.setTextColor(C_ACNT, C_HDR);
  M5.Display.drawCentreString("I2C Framework", _W/2, 28, &fonts::FreeSans9pt7b);
  // Sub text
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.drawCentreString("Initialising...", _W/2, 70, &fonts::Font2);
  // Decorative line
  M5.Display.drawFastHLine(20, 95, _W-40, C_BAND);
  M5.Display.drawCentreString("Auto-detecting I2C devices",
                              _W/2, 105, &fonts::Font2);
  M5.Display.endWrite();
}

// ── showWiFi ──────────────────────────────────────────────────
void DisplayManager::showWiFi(const String& ssid, const String& ip) {
  if (!_ready) return;
  M5.Display.startWrite();
  M5.Display.fillScreen(C_BG);
  _header("WiFi Connected", C_OK);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.drawString("SSID : " + ssid,            8,  55, &fonts::Font2);
  M5.Display.drawString("IP   : " + ip,              8,  75, &fonts::Font2);
  M5.Display.drawString("Port : " + String(WEB_SERVER_PORT),
                        8, 95, &fonts::Font2);
  M5.Display.setTextColor(C_DIM, C_BG);
  M5.Display.drawString("http://" + ip + ":" + String(WEB_SERVER_PORT),
                        8, 120, &fonts::Font2);
  M5.Display.endWrite();
  delay(3000);
}

// ── showScan ──────────────────────────────────────────────────
void DisplayManager::showScan(uint8_t* addrs, uint8_t n, bool isInternal) {
  if (!_ready) return;
  String hdr = isInternal ? "Internal I2C Bus" : "External (Port-A)";
  M5.Display.startWrite();
  M5.Display.fillScreen(C_BG);
  _header(hdr, C_HDR);
  int x = 8, y = 55;
  M5.Display.setTextColor(n ? C_ACNT : C_DIM, C_BG);
  if (!n) {
    M5.Display.drawString("No devices found", x, y, &fonts::Font2);
  }
  for (uint8_t i = 0; i < n; i++) {
    char buf[8];
    snprintf(buf, sizeof(buf), "0x%02X", addrs[i]);
    M5.Display.drawString(buf, x, y, &fonts::Font2);
    x += 56;
    if (x > _W - 56) {
      x = 8;
      y += 22;
    }
    if (y > _H - 20) break;
  }
  M5.Display.endWrite();
  delay(1500);
}

// ── showError ─────────────────────────────────────────────────
void DisplayManager::showError(const String& msg) {
  if (!_ready) return;
  M5.Display.startWrite();
  M5.Display.fillScreen(C_ERR);
  M5.Display.setTextColor(C_TEXT, C_ERR);
  M5.Display.drawCentreString("ERROR", _W/2, 20, &fonts::FreeSansBold9pt7b);
  M5.Display.setTextColor(0xFFFF, C_ERR);
  // crude word-wrap at 28 chars
  String tmp = msg;
  int y = 60;
  while (tmp.length() && y < _H - 20) {
    String line = tmp.substring(0, 28);
    M5.Display.drawCentreString(line, _W/2, y, &fonts::Font2);
    tmp = (tmp.length() > 28) ? tmp.substring(28) : "";
    y += 18;
  }
  M5.Display.endWrite();
}

// ── _header ───────────────────────────────────────────────────
void DisplayManager::_header(const String& title, uint16_t col) {
  M5.Display.fillRect(0, 0, _W, 46, col);
  M5.Display.setTextColor(C_TEXT, col);
  M5.Display.drawCentreString(title, _W/2, 14, &fonts::FreeSansBold9pt7b);
}

// ── _footer ───────────────────────────────────────────────────
void DisplayManager::_footer() {
  M5.Display.fillRect(0, _H-16, _W, 16, C_BAND);
  M5.Display.setTextColor(C_DIM, C_BAND);
  char buf[32];
  uint32_t s = millis()/1000;
  snprintf(buf, sizeof(buf), "up %02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
  M5.Display.drawString(buf, 4, _H-14, &fonts::Font2);
}

// ── _renderPlugin ─────────────────────────────────────────────
void DisplayManager::_renderPlugin(IDevice* p) {
  SensorVal vals[16];
  uint8_t   cnt = 0;
  p->getReadings(vals, cnt);

  M5.Display.fillScreen(C_BG);
  _header(p->name(), C_HDR);
  _footer();

  if (!cnt) {
    M5.Display.setTextColor(C_DIM, C_BG);
    M5.Display.drawCentreString("No data", _W/2, _H/2, &fonts::Font2);
    return;
  }

  if (cnt == 1) {
    // Big single value
    M5.Display.setTextColor(C_ACNT, C_BG);
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "%.3f", vals[0].value);
    M5.Display.drawCentreString(vbuf, _W/2, 60, &fonts::FreeSansBold18pt7b);
    M5.Display.setTextColor(C_DIM, C_BG);
    String lbl = String(vals[0].key) + "  [" + vals[0].unit + "]";
    M5.Display.drawCentreString(lbl, _W/2, 110, &fonts::Font2);
    return;
  }

  // Multi-value grid  (max 8 cells, 2 columns)
  uint8_t cols = (cnt <= 3) ? 1 : 2;
  uint8_t rows = (cnt + cols - 1) / cols;
  int cW = _W / cols;
  int startY = 50;
  int cH = (_H - startY - 18) / max(static_cast<int>(rows), 1);

  for (uint8_t i = 0; i < cnt && i < 8; i++) {
    int col = i % cols;
    int row = i / cols;
    int bx = col * cW;
    int by = startY + row * cH;
    // subtle cell divider
    M5.Display.drawRect(bx, by, cW, cH, C_BAND);
    // key label
    M5.Display.setTextColor(C_DIM, C_BG);
    M5.Display.drawString(vals[i].key, bx+4, by+3, &fonts::Font2);
    // value
    char vbuf[20];
    snprintf(vbuf, sizeof(vbuf), "%.2f", vals[i].value);
    M5.Display.setTextColor(C_TEXT, C_BG);
    M5.Display.drawString(vbuf, bx+4, by+20, &fonts::FreeSans9pt7b);
    // unit
    M5.Display.setTextColor(C_ACNT, C_BG);
    M5.Display.drawString(vals[i].unit, bx + cW - 30, by+22, &fonts::Font2);
  }
}

// ── _buildTicker ──────────────────────────────────────────────
void DisplayManager::_buildTicker(Framework* fw) {
  _ticker = "   ";
  for (auto* p : fw->plugins()) {
    if (!p->active) continue;
    SensorVal vals[16]; uint8_t cnt = 0;
    p->getReadings(vals, cnt);
    for (uint8_t i = 0; i < cnt; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "[%s] %s=%.2f%s   ",
               p->slug(), vals[i].key, vals[i].value, vals[i].unit);
      _ticker += buf;
    }
  }
  if (_ticker.length() < 5) _ticker = "   No active sensors   ";
}

// ── _renderTicker ─────────────────────────────────────────────
//  We avoid full-screen flicker by drawing the static parts
//  (header, body, footer) only once per "tickerBuilt" interval,
//  and only re-painting the scrolling band each frame.
void DisplayManager::_renderTicker(Framework* fw) {
  bool rebuild = (millis() - _tickerBuilt > POLL_MS * 2);
  if (rebuild) {
    _buildTicker(fw);
    _tickerBuilt = millis();
  }

  M5.Display.startWrite();

  if (rebuild) {
    // Repaint static layout
    M5.Display.fillScreen(C_BG);
    _header(_frameworkTitle, C_HDR);

    uint8_t active = 0;
    for (auto* p : fw->plugins()) if (p->active) active++;
    M5.Display.setTextColor(C_DIM, C_BG);
    char cbuf[28];
    snprintf(cbuf, sizeof(cbuf), "%u sensor(s) online", active);
    M5.Display.drawCentreString(cbuf, _W/2, 60, &fonts::Font2);
  }

  // Footer (uptime) updates every frame so re-paint it
  _footer();

  // Scrolling band — clear and redraw only this strip
  int ty = _H - 38;
  M5.Display.fillRect(0, ty, _W, 20, C_BAND);

  if (millis() - _scrollMs > 25) {
    _scrollX -= DISPLAY_SCROLL_PX;
    int tw = static_cast<int>(_ticker.length()) * 8;  // ~8 px/char @ Font2
    if (_scrollX < -tw) _scrollX = _W;
    _scrollMs = millis();
  }
  M5.Display.setTextColor(C_ACNT, C_BAND);
  M5.Display.drawString(_ticker, _scrollX, ty+3, &fonts::Font2);

  M5.Display.endWrite();
}

// ── _renderFixed ──────────────────────────────────────────────
void DisplayManager::_renderFixed(Framework* fw) {
  M5.Display.startWrite();
  M5.Display.fillScreen(C_BG);
  _header(_frameworkTitle, C_HDR);
  _footer();

  int x = DISPLAY_FIX_X + 4;
  int y = DISPLAY_FIX_Y + 50;
  const int ROW = 16;

  for (auto* p : fw->plugins()) {
    if (!p->active) continue;
    SensorVal vals[16]; uint8_t cnt = 0;
    p->getReadings(vals, cnt);

    M5.Display.setTextColor(C_HDR, C_BG);
    M5.Display.drawString(String("[") + p->slug() + "]", x, y, &fonts::Font2);
    y += ROW;

    for (uint8_t i = 0; i < cnt; i++) {
      char buf[40];
      snprintf(buf, sizeof(buf), "  %-10s %8.2f %s",
               vals[i].key, vals[i].value, vals[i].unit);
      M5.Display.setTextColor(C_TEXT, C_BG);
      M5.Display.drawString(buf, x, y, &fonts::Font2);
      y += ROW;
      if (y > _H - 20) goto done;
    }
  }
  done: { /* goto target - drawing finished or out of vertical space */ }
  M5.Display.endWrite();
}

// ── update ────────────────────────────────────────────────────
void DisplayManager::update(Framework* fw) {
  if (!enabled || !_ready) return;

  if (DISPLAY_SCROLL) {
    _renderTicker(fw);
    return;
  }
  _renderFixed(fw);
}
#endif  // OUT_DISPLAY
