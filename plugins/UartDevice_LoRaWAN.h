#pragma once
// ============================================================
//  UartDevice_LoRaWAN.h  –  M5Stack LoRaWAN Unit US915  [UART]
//                          (STM32WLE5 / RAK3172) — LoRa P2P + chat log
//
//  A LoRa transceiver for bench-testing a point-to-point link AND a
//  basic two-way text "chat" with another P2P node.  The M5 LoRaWAN
//  Unit US915 is a RAK3172 (STM32WLE5) module speaking the RAKwireless
//  RUI3 AT-command set over UART; it stacks onto the framework exactly
//  like the other UART devices (no framework changes) and lands on the
//  host board's Port-C.
//
//      fw.addPlugin(new UartDevice_LoRaWAN(Serial2, 115200));
//
//  ⚠ ONE UART DEVICE AT A TIME.  Port-C is shared with the Module
//  LLM / Barcode / Modem / GPS — register at most one.  On the test
//  unit, register THIS instead of UartDevice_ModuleLLM.
//
//  ── P2P, not LoRaWAN ────────────────────────────────────────
//  This drives the module in raw LoRa **P2P** mode (`AT+NWM=0`), for
//  device-to-device testing/messaging against a second radio (e.g. an
//  SX1262 on a Cardputer).  It does NOT join a LoRaWAN network — that
//  needs a gateway + network server, not a peer.  For the two radios to
//  hear each other EVERY parameter below must match on both ends:
//  frequency, spreading factor, bandwidth, coding rate, preamble, and
//  the public/private sync word.  A mismatch = total silence with no
//  error, so they are set explicitly here rather than left to default.
//
//  Frequency note: the Unit is US915 (902–928 MHz) and a typical
//  SX1262 cap covers 868–923 MHz, so keep the P2P frequency in the
//  ~902–923 MHz overlap.  915.0 MHz (the default) is safe; do NOT go
//  above 923 MHz or the SX1262 side can't tune it.
//
//  ── Chat / message log ──────────────────────────────────────
//  Beyond the raw last-packet fields, the plugin keeps a rolling LOG
//  of the last LOG_MAX messages (sent + received, each with direction,
//  sender, RSSI/SNR and a monotonic id).  toJson() publishes it as a
//  "log" array so a dashboard chat panel can append only the new
//  entries (track the highest "id" seen).  An optional LORA_NODE_NAME
//  is prepended to outgoing messages as "<name>|<text>" so a peer can
//  tell who sent what (handy with 3+ nodes); incoming "<name>|<text>"
//  is split back into sender + body.  Plain messages with no "|" still
//  work — sender shows blank.
//
//  ── AT firmware caveat ──────────────────────────────────────
//  Commands here are the RAK3172 **RUI3** set (AT+NWM / AT+PFREQ /
//  AT+PSF / AT+PBW / AT+PCR / AT+PPL / AT+PTP / AT+PSEND / AT+PRECV,
//  receive events as `+EVT:RXP2P:<rssi>:<snr>:<hex>`).  RAK AT syntax
//  shifted between firmware generations — if a link won't form, set
//  TRACE_RX = true, watch the raw module replies on the serial console,
//  and adjust the few command strings / the _consumeLine() parser to
//  match your unit's firmware (see docs.m5stack.com Unit LoRaWAN-US915).
//
//  ── How it works in this framework ──────────────────────────
//  CONTROLLABLE — driven via the Web API:
//      GET /api/lora/set?send=<text>     transmit text (UTF-8 → hex)
//      GET /api/lora/set?sendhex=<hex>   transmit raw hex bytes
//      GET /api/lora/set?clear=1         reset log / counters
//  Runs without blocking the main loop: beginUart() configures the
//  radio once (a few short blocking AT waits at boot, like the other
//  UART devices), then fastPoll() drains receive events every loop and
//  folds them into the log.  toJson()/getReadings() publish the log +
//  RSSI/SNR + TX/RX counts.  A send briefly stops RX, transmits, and
//  re-arms continuous receive.
// ============================================================
#include "../src/IUartDevice.h"

// ── P2P radio parameters — MUST match the peer radio ──────────
//  Overridable from Config.h; sensible US915 bench defaults here.
#ifndef LORA_P2P_FREQ_HZ
#define LORA_P2P_FREQ_HZ 915000000UL   // 915.0 MHz (in the 902–923 overlap)
#endif
#ifndef LORA_P2P_SF
#define LORA_P2P_SF 7                  // spreading factor 7..12
#endif
#ifndef LORA_P2P_BW
#define LORA_P2P_BW 125                // bandwidth kHz (125/250/500)
#endif
#ifndef LORA_P2P_CR
#define LORA_P2P_CR 0                  // coding rate: 0=4/5 1=4/6 2=4/7 3=4/8
#endif
#ifndef LORA_P2P_PREAMBLE
#define LORA_P2P_PREAMBLE 8            // preamble length (symbols)
#endif
#ifndef LORA_P2P_TX_POWER
#define LORA_P2P_TX_POWER 14           // TX power dBm (<=22; respect local limits)
#endif
// Optional short name for THIS node, prepended to outgoing messages as
// "<name>|<text>".  Leave "" to send plain text.
#ifndef LORA_NODE_NAME
#define LORA_NODE_NAME ""
#endif

class UartDevice_LoRaWAN : public IUartDevice {
 public:
  UartDevice_LoRaWAN(HardwareSerial& port, uint32_t baud = 115200,
                     int8_t rxPin = -1, int8_t txPin = -1)
      : IUartDevice(port, baud, rxPin, txPin) {}

  // ── Tunables ──────────────────────────────────────────────
  static constexpr uint32_t FREQ_HZ   = LORA_P2P_FREQ_HZ;
  static constexpr uint8_t  SF        = LORA_P2P_SF;
  static constexpr uint16_t BW_KHZ    = LORA_P2P_BW;
  static constexpr uint8_t  CR        = LORA_P2P_CR;
  static constexpr uint16_t PREAMBLE  = LORA_P2P_PREAMBLE;
  static constexpr uint8_t  TX_POWER  = LORA_P2P_TX_POWER;
  static constexpr const char* NODE_NAME = LORA_NODE_NAME;
  // Longest received/echoed payload kept in RAM (chars of text/hex).
  static constexpr uint16_t PAYLOAD_MAX = 200;
  // Rolling chat log depth (sent + received).  Kept small to bound RAM
  // and the JSON size folded into /api/all.
  static constexpr uint8_t  LOG_MAX = 14;
  // A transmit is considered done after this long if no TXP2P DONE
  // event arrives (so a missed event doesn't wedge `busy`).
  static constexpr uint32_t TX_IDLE_MS = 8000;
  // Dump every raw line from the module to the serial console.  The
  // first thing to flip on when a link won't form — it shows the exact
  // AT replies / event formats so the commands + parser can be matched
  // to your firmware.
  static constexpr bool TRACE_RX = false;
  static constexpr size_t RX_BUFFER = 1024;

  // ── Identity ──────────────────────────────────────────────
  const char* name() const override { return "LoRa P2P"; }
  const char* slug() const override { return "lora"; }
  bool controllable() const override { return true; }
  size_t rxBufferSize() const override { return RX_BUFFER; }

  // ── Activation: configure the radio for P2P ───────────────
  bool beginUart() override {
    bool up = false;
    for (uint8_t i = 0; i < 10 && !up; i++) {
      _drain();
      _send("AT");
      if (_waitFor("OK", 300)) up = true;
      else delay(150);
    }
    if (!up) {
      Serial.println(F("[LoRa] no response to AT on Port-C — unit absent?"));
      return false;
    }

    // Switch to P2P (network work mode 0).  On RUI3 this reboots the
    // module if the mode actually changes, so settle and re-probe.
    _send("AT+NWM=0");
    delay(2500);
    _drain();
    _send("AT");
    _waitFor("OK", 800);

    bool ok = true;
    ok &= _setParam("AT+PFREQ=", String(FREQ_HZ));
    ok &= _setParam("AT+PSF=",   String(SF));
    ok &= _setParam("AT+PBW=",   String(BW_KHZ));
    ok &= _setParam("AT+PCR=",   String(CR));
    ok &= _setParam("AT+PPL=",   String(PREAMBLE));
    ok &= _setParam("AT+PTP=",   String(TX_POWER));
    if (!ok)
      Serial.println(F("[LoRa] ⚠ one or more P2P params were rejected — check "
                       "TRACE_RX output against your firmware's AT syntax"));

    _send("AT+PRECV=65535");   // continuous receive
    _waitFor("OK", 800);

    _connected = true;
    Serial.printf("[LoRa] P2P ready — %.3f MHz SF%u BW%uk CR4/%u PL%u %ddBm  node='%s'\n",
                  FREQ_HZ / 1e6, SF, BW_KHZ, CR + 5, PREAMBLE, TX_POWER, NODE_NAME);
    return true;
  }

  // ── Async servicing ───────────────────────────────────────
  bool wantsFastPoll() const override { return true; }

  void fastPoll() override {
    if (!_connected) return;
    while (_port->available()) {
      char c = static_cast<char>(_port->read());
      if (c == '\r') continue;
      if (c == '\n') {
        _consumeLine(_line);
        _line = "";
      } else if (_line.length() < 320) {
        _line += c;
      }
    }
  }

  void update() override {
    if (_busy && (millis() - _lastTxMs) > TX_IDLE_MS) {
      _busy = false;
      Serial.println(F("[LoRa] tx assumed done (no DONE event before timeout)"));
    }
  }

  // ── Data access ───────────────────────────────────────────
  void toJson(JsonObject& o) const override {
    o["connected"]  = _connected ? 1 : 0;
    o["busy"]       = _busy ? 1 : 0;
    o["mode"]       = "p2p";
    o["node"]       = NODE_NAME;
    o["freq_hz"]    = FREQ_HZ;
    o["sf"]         = SF;
    o["bw_khz"]     = BW_KHZ;
    o["cr"]         = CR;
    o["tx_count"]   = _txCount;
    o["rx_count"]   = _rxCount;
    o["last_rssi"]  = _lastRssi;
    o["last_snr"]   = _lastSnr;
    o["last_id"]    = _msgSeq;        // highest message id (for incremental UI)
    // Rolling chat log, oldest → newest.
    JsonArray log = o["log"].to<JsonArray>();
    for (uint8_t i = 0; i < _logCount; i++) {
      uint8_t idx = (uint8_t)((_logHead + LOG_MAX - _logCount + i) % LOG_MAX);
      const Msg& m = _log[idx];
      JsonObject e = log.add<JsonObject>();
      e["id"]   = m.id;
      e["dir"]  = (m.dir == 't') ? "tx" : "rx";
      e["from"] = m.from;
      e["text"] = m.text;
      if (m.dir == 'r') {
        e["rssi"] = m.rssi;
        e["snr"]  = m.snr;
      }
    }
  }

  void getReadings(SensorVal* b, uint8_t& n) const override {
    b[0] = {"rx_count",  static_cast<float>(_rxCount), ""};
    b[1] = {"tx_count",  static_cast<float>(_txCount), ""};
    b[2] = {"last_rssi", static_cast<float>(_lastRssi), "dBm"};
    b[3] = {"last_snr",  static_cast<float>(_lastSnr),  "dB"};
    n = 4;
  }

  // ── Control ───────────────────────────────────────────────
  bool command(const String& param, const String& value) override {
    if (param == "send") {
      if (!_connected) { Serial.println(F("[LoRa] send rejected — not ready")); return false; }
      if (value.length() == 0) return false;
      // Prepend this node's name (if set) so the peer sees the sender.
      String wire = (NODE_NAME[0] != '\0') ? (String(NODE_NAME) + "|" + value) : value;
      bool ok = _txHex(_toHex(wire));
      _appendLog('t', (NODE_NAME[0] != '\0') ? NODE_NAME : "me", value, 0, 0);
      return ok;
    }
    if (param == "sendhex") {
      if (!_connected) return false;
      String h = value; h.trim();
      if (h.length() == 0 || (h.length() % 2) != 0) {
        Serial.println(F("[LoRa] sendhex rejected — need an even-length hex string"));
        return false;
      }
      bool ok = _txHex(h);
      _appendLog('t', "me", _fromHex(h), 0, 0);
      return ok;
    }
    if (param == "clear") {
      _txCount = _rxCount = 0;
      _lastRssi = _lastSnr = 0;
      _lastText = _lastHex = "";
      _logCount = _logHead = 0;
      // Note: _msgSeq is NOT reset, so a watching panel still sees ids
      // advance monotonically and won't replay a stale cache.
      return true;
    }
    return false;  // unknown param
  }

  void controlSchema(JsonArray& out) const override {
    JsonObject a = out.add<JsonObject>();
    a["id"] = "send";
    a["label"] = "Send";
    a["type"] = "text";
    a["placeholder"] = "Message to transmit (P2P) — peer must match radio params";
    JsonObject h = out.add<JsonObject>();
    h["id"] = "sendhex";
    h["label"] = "Send hex";
    h["type"] = "text";
    h["placeholder"] = "Raw hex bytes, e.g. 48656C6C6F";
    JsonObject c = out.add<JsonObject>();
    c["id"] = "clear";
    c["label"] = "Clear log";
    c["type"] = "button";
    c["query"] = "clear=1";
  }

 private:
  struct Msg {
    uint16_t id = 0;
    char     dir = 'r';        // 't' = sent, 'r' = received
    String   from;             // sender name ("me"/node name/parsed peer/"")
    String   text;             // message body
    int      rssi = 0, snr = 0;
  };

  String   _line;                 // RX line accumulator
  String   _lastText, _lastHex;   // last received packet
  int      _lastRssi = 0, _lastSnr = 0;
  uint32_t _txCount = 0, _rxCount = 0, _lastTxMs = 0;
  bool     _connected = false, _busy = false;

  Msg      _log[LOG_MAX];
  uint8_t  _logHead = 0, _logCount = 0;   // ring buffer
  uint16_t _msgSeq = 0;                    // monotonic message id

  void _appendLog(char dir, const String& from, const String& text,
                  int rssi, int snr) {
    Msg& m = _log[_logHead];
    m.id   = ++_msgSeq;
    m.dir  = dir;
    m.from = from;
    m.text = text.substring(0, PAYLOAD_MAX);
    m.rssi = rssi;
    m.snr  = snr;
    _logHead = (uint8_t)((_logHead + 1) % LOG_MAX);
    if (_logCount < LOG_MAX) _logCount++;
  }

  // ── UART helpers ──────────────────────────────────────────
  void _send(const String& cmd) {
    if (TRACE_RX) Serial.printf("[LoRa] tx: %s\n", cmd.c_str());
    _port->print(cmd);
    _port->print("\r\n");
  }

  void _drain() {
    while (_port->available()) _port->read();
  }

  bool _waitFor(const char* token, uint32_t timeoutMs) {
    String buf;
    uint32_t t0 = millis();
    while (millis() - t0 < timeoutMs) {
      while (_port->available()) {
        char c = static_cast<char>(_port->read());
        buf += c;
        if (buf.length() > 256) buf.remove(0, buf.length() - 256);
        if (buf.indexOf(token) >= 0) return true;
      }
      delay(2);
    }
    if (TRACE_RX) Serial.printf("[LoRa] waitFor('%s') timed out; saw: %s\n",
                                token, buf.c_str());
    return false;
  }

  bool _setParam(const char* cmd, const String& val) {
    _send(String(cmd) + val);
    return _waitFor("OK", 600);
  }

  bool _txHex(const String& hex) {
    _send("AT+PRECV=0");
    _waitFor("OK", 400);
    _send(String("AT+PSEND=") + hex);
    bool ok = _waitFor("OK", 2000);
    _txCount++;
    _busy = true;
    _lastTxMs = millis();
    _send("AT+PRECV=65535");
    _waitFor("OK", 400);
    if (!ok)
      Serial.println(F("[LoRa] PSEND not acked — check payload / firmware"));
    return ok;
  }

  // Parse one line from the module.
  //   +EVT:RXP2P:<rssi>:<snr>:<hex>   a received P2P packet (RUI3)
  //   +EVT:TXP2P DONE                 transmit finished
  void _consumeLine(String line) {
    line.trim();
    if (line.length() == 0) return;
    if (TRACE_RX) Serial.printf("[LoRa] rx: %s\n", line.c_str());

    if (line.indexOf("TXP2P") >= 0) {     // TX done (or timeout)
      _busy = false;
      return;
    }
    int rx = line.indexOf("RXP2P");
    if (rx >= 0) {
      int colon = line.indexOf(':', rx);
      if (colon < 0) return;
      String rest = line.substring(colon + 1);   // rssi:snr:hex
      int c1 = rest.indexOf(':');
      int c2 = (c1 >= 0) ? rest.indexOf(':', c1 + 1) : -1;
      if (c1 < 0 || c2 < 0) return;
      _lastRssi = rest.substring(0, c1).toInt();
      _lastSnr  = rest.substring(c1 + 1, c2).toInt();
      String hex = rest.substring(c2 + 1);
      hex.trim();
      _lastHex  = hex.substring(0, PAYLOAD_MAX);
      String body = _fromHex(_lastHex);

      // Split an optional "<sender>|<body>" prefix.
      String from = "";
      int bar = body.indexOf('|');
      if (bar > 0 && bar <= 16) {           // short sender name only
        from = body.substring(0, bar);
        body = body.substring(bar + 1);
      }
      _lastText = body;
      _rxCount++;
      _appendLog('r', from, body, _lastRssi, _lastSnr);
      if (TRACE_RX)
        Serial.printf("[LoRa] packet #%u  RSSI=%d SNR=%d  from='%s'  \"%s\"\n",
                      (unsigned)_rxCount, _lastRssi, _lastSnr, from.c_str(),
                      body.c_str());
    }
  }

  // ── hex <-> text ──────────────────────────────────────────
  static String _toHex(const String& s) {
    static const char* H = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 2);
    for (size_t i = 0; i < s.length(); ++i) {
      uint8_t c = (uint8_t)s[i];
      out += H[c >> 4];
      out += H[c & 0x0F];
    }
    return out;
  }

  static String _fromHex(const String& hex) {
    auto nib = [](char h) -> int {
      if (h >= '0' && h <= '9') return h - '0';
      if (h >= 'a' && h <= 'f') return h - 'a' + 10;
      if (h >= 'A' && h <= 'F') return h - 'A' + 10;
      return -1;
    };
    String out;
    for (int i = 0; i + 1 < (int)hex.length(); i += 2) {
      int hi = nib(hex[i]), lo = nib(hex[i + 1]);
      if (hi < 0 || lo < 0) break;
      char c = char((hi << 4) | lo);
      out += (c >= 32 && c < 127) ? c : '.';
    }
    return out;
  }
};
