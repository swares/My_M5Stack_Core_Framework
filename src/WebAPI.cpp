// ============================================================
//  WebAPI.cpp  –  HTTPS dashboard + REST API
// ============================================================
#include "Config.h"        // OUT_WEB — must precede the #if
#if OUT_WEB
#include "WebAPI.h"
#include "Framework.h"
#include "BoardInfo.h"
#include "Settings.h"  // runtime WiFi + dashboard login (approach B)
#include <WiFiUdp.h>   // captive-portal DNS responder (lwIP sockets — safe)
#include <SD.h>        // sdcard_type_t enum (CARD_NONE/MMC/SD/SDHC) for /api SD status
#include <esp_log.h>   // esp_log_level_set() — quiets the esp-tls log tag
#include <cstdio>      // snprintf

// Embedded self-signed TLS certificate + private key (DER).  See
// https_cert.h for why the cert is baked in rather than generated
// on the device.
#include "https_cert.h"

// ============================================================
//  Embedded HTML dashboard – stored in Flash (PROGMEM)
//  The page title and topbar headline are filled in by JavaScript
//  using values from /api/all (board.long_name), so the same
//  binary works for any supported board without a recompile.
//
//  The dashboard is a single self-contained vanilla-JS page — no
//  external scripts, no internet needed.  It polls /api/all and
//  splits devices into two sections:
//    • Controllable Devices — cards with live interactive widgets
//      (toggle / slider / colour / text / button).  The widgets
//      are built generically from the "controls" array each
//      controllable device emits via IDevice::controlSchema();
//      changing a widget issues POST /api/<slug>/set (body
//      <id>=<value>); the old GET ?<id>=<value> form still works.
//    • Read-only Sensors — reading cards, as before.
//  An API request log panel records every /set call + its status.
// ============================================================
#include "WebAssets.h"


// ============================================================

// Forward declaration — the shared setup-submit handler is defined
// with the other setup helpers further below; begin() references it
// for the captive (plain-HTTP) server.
template <class Srv> static void serveSetupSubmit(Srv* srv);

// ── Plain-HTTP-AP response helpers (templated on server type) ─
//  Mirror _cors / _json / _requireAuth / _reqPath but operate on any
//  server, so the standalone-AP routes can reuse the JSON the HTTPS
//  routes build.  Same pattern as serveSetupSubmit<Srv>.
template <class Srv> static void apCors(Srv* s) {
  s->sendHeader("Access-Control-Allow-Origin",  WEB_CORS_ORIGIN);
  s->sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  s->sendHeader("Access-Control-Allow-Headers", "Content-Type");
  s->sendHeader("Cache-Control",                "no-cache");
}
template <class Srv> static void apSendJson(Srv* s, JsonDocument& doc, int code = 200) {
  apCors(s);
  String out;
  serializeJson(doc, out);
  s->send(code, "application/json", out);
}
template <class Srv> static bool apAuth(Srv* s) {
  String user = Settings::webUser();
  if (user.isEmpty()) return true;                 // auth disabled
  if (s->authenticate(user.c_str(), Settings::webPass().c_str())) return true;
  s->requestAuthentication();
  return false;
}
template <class Srv> static String apPath(Srv* s) {
  String u = s->uri();
  int q = u.indexOf('?');
  return (q >= 0) ? u.substring(0, q) : u;
}

void WebAPI::begin(Framework* fw) {
  _fw = fw;
  if (!enabled) return;

  // ── Quiet the TLS handshake-abort log spam ───────────────────
  //  Browsers (Chrome especially) routinely open speculative /
  //  pre-connect TLS sockets and abandon the ones they end up not
  //  using.  Each abandoned socket makes ESP-IDF's esp-tls layer
  //  print a red "mbedtls_ssl_handshake returned -0x7780" line.
  //  -0x7780 is MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE — the peer
  //  closed the handshake — which is normal background noise on
  //  any LAN web server, not a device fault.  Silence just that
  //  one log tag so the serial console stays readable.
  //
  //  NOTE: this tag is shared with the MQTT-over-TLS client.  If
  //  you ever need to debug an MQTTS handshake, comment this line
  //  out temporarily to see the esp-tls diagnostics again.
  esp_log_level_set("esp-tls-mbedtls", ESP_LOG_NONE);

  // ── Standalone-AP plain-HTTP mode ─────────────────────────
  //  When WEB_AP_PLAIN_HTTP is set and we're a provisioned standalone
  //  access point, serve the dashboard + API over plain HTTP on port
  //  80 and DON'T start the TLS server at all (no self-signed-cert
  //  warning over our own WPA2 AP, and the TLS RAM stays free).  Setup
  //  mode (captive portal) and station mode are unaffected and fall
  //  through to the HTTPS path below.
  _plainAp = WEB_AP_PLAIN_HTTP && !_setupMode() && Settings::apOnlyMode();
  if (_plainAp) {
    _httpRedirect = new ESPWebServer(WEB_HTTP_REDIRECT_PORT);
    _wirePlainAp(_httpRedirect);
    _httpRedirect->begin();
    Serial.printf("[WebAPI] STANDALONE AP — dashboard over PLAIN HTTP on "
                  "port %d (HTTPS disabled via WEB_AP_PLAIN_HTTP)\n",
                  WEB_HTTP_REDIRECT_PORT);
    return;
  }

  // ── HTTPS server (TLS) ────────────────────────────────────
  //  ESPWebServerSecure mirrors the standard Arduino WebServer
  //  API, so the route registration below is unchanged from the
  //  old plain-HTTP server — only the transport is now TLS.  The
  //  embedded self-signed certificate must be set before begin().
  _srv = new ESPWebServerSecure(WEB_HTTPS_PORT);
  _srv->setServerKeyAndCert(HTTPS_KEY_DER,  HTTPS_KEY_DER_LEN,
                            HTTPS_CERT_DER, HTTPS_CERT_DER_LEN);

  // Every route handler first passes through _requireAuth().  When
  // WEB_AUTH_USER is empty this is a no-op; otherwise the request
  // is rejected with 401 unless it carries valid HTTP Basic Auth.
  _srv->on("/",           [this](){
    // In setup mode (unprovisioned, or recovery AP after a failed
    // join) "/" serves the setup portal with NO auth — there may be
    // no usable password to enter yet.  Otherwise it's the dashboard
    // behind the usual auth.
    if (!_setupMode() && !_requireAuth()) return;
    _route_root();
  });
  // Provisioning submit.  Open in setup mode; auth-gated otherwise so
  // a normally-running unit can't be re-provisioned by anyone who can
  // reach it.
  _srv->on("/api/setup",  [this](){
    if (!_setupMode() && !_requireAuth()) return;
    _route_setup();
  });
  // Auth-gated credential editor (provisioned device, over HTTPS).
  _srv->on("/settings", [this](){
    if (!_requireAuth()) return;
    _cors();
    _srv->send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
  });
  _srv->on("/api/settings", [this](){
    if (!_requireAuth()) return;
    _route_settingsGet();
  });
  _srv->on("/api/settings/save", [this](){
    if (!_requireAuth()) return;
    _route_settingsSave();
  });
  _srv->on("/api/all",    [this](){
    if (!_requireAuth()) return;
    _route_all();
  });
  _srv->on("/api/scan",   [this](){
    if (!_requireAuth()) return;
    _route_scan();
  });
  _srv->on("/api/config", [this](){
    if (!_requireAuth()) return;
    _route_config();
  });
  _srv->on("/api/rescan", [this](){
    if (!_requireAuth()) return;
    // GET /api/rescan  → re-run the boot-time scan-and-bind pass.
    // Useful after plugging in a sensor (the device must already
    // have been powered when the call is made — the framework does
    // not hot-plug, it just rebinds what's currently on the bus).
    _fw->rescanAll();
    _route_all();   // reply with the fresh sensor map
  });
  _srv->on("/api/mqtt", [this](){
    if (!_requireAuth()) return;
    _route_mqtt();
  });
  _srv->on("/api/alerts", [this](){
    if (!_requireAuth()) return;
    _route_alerts();
  });
  _srv->on("/api/endpoints", [this](){
    if (!_requireAuth()) return;
    _route_endpoints();
  });
  _srv->on("/api/sdcard", [this](){
    if (!_requireAuth()) return;
    _route_sdcard();
  });
  _srv->on("/api/sdcard/flush", [this](){
    if (!_requireAuth()) return;
    bool ok = _fw->sdlog.flush();
    JsonDocument doc;
    _buildSdStatus(doc);
    doc["flushed"] = ok;
    _json(doc);
  });
  _srv->on("/api/sdcard/eject", [this](){
    if (!_requireAuth()) return;
    // Cleanly close the log file + unmount.  After this the card
    // is safe to physically remove; logging resumes only on reboot.
    bool ok = _fw->sdlog.eject();
    JsonDocument doc;
    _buildSdStatus(doc);
    doc["ejected"] = ok;
    _json(doc);
  });
  _srv->on("/api/mqtt/publish", [this](){
    if (!_requireAuth()) return;
    // GET /api/mqtt/publish  → force one immediate publish cycle.
    // Returns the same status doc as /api/mqtt plus a "publish_now"
    // boolean indicating whether the publish actually fired.
    bool fired = _fw->mqtt.publishNow();
    JsonDocument doc;
    _buildMqttStatus(doc);
    doc["publish_now"] = fired;
    _json(doc);
  });
  _srv->onNotFound([this](){
    String uri = _reqPath();          // path only — query stripped
    // Catch /api/setup here too, in case the server's on() only
    // registered it for GET and the portal submits via POST.  Same
    // gating as the dedicated route: open in setup mode, else auth.
    if (uri == "/api/setup") {
      if (!_setupMode() && !_requireAuth()) return;
      _route_setup();
      return;
    }
    if (!_requireAuth()) return;
    // Everything else that lands here — the alerts-rules routes, the
    // settings save (a POST; the on() registration above only matched
    // GET), the control /set endpoint, plugin reads, and the JSON 404 —
    // is the SAME logic the standalone-AP server runs.  It lives once in
    // _routeDynamic(); both servers share it.
    _routeDynamic(_srv, uri);
  });
  // Capture the CSRF header so _doControl can read it (the server only
  // surfaces non-standard request headers that are registered here).
  {
    static const char* csrfHdr[] = {"X-Requested-With"};
    _srv->collectHeaders(csrfHdr, 1);
  }
  _srv->begin();

  // ── HTTP server (port 80) ─────────────────────────────────
  _httpRedirect = new ESPWebServer(WEB_HTTP_REDIRECT_PORT);
  if (_setupMode()) {
    // Captive-portal mode: serve the setup page (and accept the
    // submit) over plain HTTP so a phone's captive-portal probe opens
    // it automatically — and without a self-signed-cert warning.  The
    // link is already WPA2-encrypted on our own AP.  Paired with the
    // DNS hijack below, any URL the device requests lands here.
    _httpRedirect->onNotFound([this]() {
      // Handle the submit by path (covers POST even if on() is
      // GET-only); serve the portal for every other request so the
      // OS captive check opens it.
      if (_httpRedirect->uri() == "/api/setup") {
        serveSetupSubmit(_httpRedirect);
        return;
      }
      _httpRedirect->send_P(200, "text/html; charset=utf-8", SETUP_HTML);
    });
    _httpRedirect->begin();
#if CAPTIVE_DNS_ENABLED
    // Point every hostname at us (resolve *.* → our AP IP) so the OS
    // captive check is redirected to the portal.  WiFiUDP uses the lwIP
    // SOCKETS layer (socket/bind/recvfrom) — thread-safe, unlike the
    // raw udp_new path DNSServer / AsyncUDP take, which asserts here.
    // Serviced (polled) in update().
    _dnsUdp = new WiFiUDP();
    if (_dnsUdp->begin(53)) {
      Serial.printf("[WebAPI] Captive portal active — DNS → %s\n",
                    WiFi.softAPIP().toString().c_str());
    } else {
      Serial.println(F("[WebAPI] Captive DNS failed to bind :53 — portal "
                       "still at the AP IP."));
      delete _dnsUdp;
      _dnsUdp = nullptr;
    }
#else
    Serial.printf("[WebAPI] Setup portal at http://%s/  (captive DNS off — "
                  "browse there manually)\n",
                  WiFi.softAPIP().toString().c_str());
#endif
  } else {
    // Normal mode: serve no content; 301 every request to the HTTPS
    // URL so an old http:// bookmark still lands on the secure page.
    _httpRedirect->onNotFound([this]() { _sendRedirect(); });
    _httpRedirect->begin();
  }

  if (_setupMode()) {
    Serial.printf("[WebAPI] HTTPS server on port %d — SETUP MODE, "
                  "serving setup portal at /\n", WEB_HTTPS_PORT);
  } else if (Settings::webUser().length() > 0) {
    Serial.printf("[WebAPI] HTTPS server on port %d (auth: user=%s)\n",
                  WEB_HTTPS_PORT, Settings::webUser().c_str());
  } else {
    Serial.printf("[WebAPI] HTTPS server on port %d (no auth)\n",
                  WEB_HTTPS_PORT);
  }
  Serial.printf("[WebAPI] HTTP port %d redirects -> HTTPS\n",
                WEB_HTTP_REDIRECT_PORT);
}

void WebAPI::update() {
  if (!enabled) return;
  if (_dnsUdp)       _serviceCaptiveDns();         // captive-portal DNS
  if (_srv)          _srv->handleClient();
  if (_httpRedirect) _httpRedirect->handleClient();
}

// ── _serviceCaptiveDns ───────────────────────────────────────
//  Answer one pending DNS query (if any) with an A record pointing at
//  the AP IP, so every hostname resolves to us and the OS opens the
//  setup portal.  Echoes the question, appends one answer, drops any
//  additional/EDNS records.  WiFiUDP = lwIP sockets = thread-safe.
void WebAPI::_serviceCaptiveDns() {
  int avail = _dnsUdp->parsePacket();
  if (avail <= 0) return;

  uint8_t q[320];
  int qlen = _dnsUdp->read(q, sizeof(q));
  if (qlen < 12) return;                  // too short to be a DNS query

  // Walk the first question's QNAME (length-prefixed labels, 0-term).
  int pos = 12;
  while (pos < qlen && q[pos] != 0) {
    pos += q[pos] + 1;
    if (pos >= qlen) return;              // malformed
  }
  pos += 1 + 4;                           // 0 terminator + QTYPE + QCLASS
  if (pos > qlen) return;
  const int qend = pos;                  // header + one question

  uint8_t resp[320];
  if (qend + 16 > (int)sizeof(resp)) return;
  memcpy(resp, q, qend);                  // copy header + question verbatim
  resp[2] = 0x81;                         // QR=1, AA=1
  resp[3] = 0x80;                         // RA=1, RCODE=0
  resp[4] = 0x00; resp[5] = 0x01;         // QDCOUNT = 1
  resp[6] = 0x00; resp[7] = 0x01;         // ANCOUNT = 1
  resp[8] = 0x00; resp[9] = 0x00;         // NSCOUNT = 0
  resp[10] = 0x00; resp[11] = 0x00;       // ARCOUNT = 0 (drop EDNS OPT)

  IPAddress apIP = WiFi.softAPIP();
  int i = qend;
  resp[i++] = 0xC0; resp[i++] = 0x0C;     // NAME → pointer to the question
  resp[i++] = 0x00; resp[i++] = 0x01;     // TYPE  A
  resp[i++] = 0x00; resp[i++] = 0x01;     // CLASS IN
  resp[i++] = 0x00; resp[i++] = 0x00;
  resp[i++] = 0x00; resp[i++] = 0x3C;     // TTL = 60 s
  resp[i++] = 0x00; resp[i++] = 0x04;     // RDLENGTH = 4
  resp[i++] = apIP[0]; resp[i++] = apIP[1];
  resp[i++] = apIP[2]; resp[i++] = apIP[3];

  _dnsUdp->beginPacket(_dnsUdp->remoteIP(), _dnsUdp->remotePort());
  _dnsUdp->write(resp, i);
  _dnsUdp->endPacket();
}

// ── _sendRedirect ─────────────────────────────────────────────
//  Handler for the plain-HTTP port-80 server: 301 every request to
//  the same path on HTTPS.  The target host comes from the request
//  Host header (port stripped); if absent, the device IP is used.
void WebAPI::_sendRedirect() {
  String host = _httpRedirect->hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  if (host.length() == 0) host = WiFi.localIP().toString();

  String loc = "https://" + host;
  if (WEB_HTTPS_PORT != 443) loc += ":" + String(WEB_HTTPS_PORT);
  loc += _httpRedirect->uri();

  _httpRedirect->sendHeader("Location", loc);
  _httpRedirect->send(301, "text/plain", "Redirecting to HTTPS");
}

// ── _reqPath ──────────────────────────────────────────────────
//  ESPWebServerSecure::uri() returns the FULL request target,
//  query string included (e.g. "/api/pps/set?vset=5") — unlike the
//  standard Arduino WebServer, whose uri() is path-only.  Routing
//  that inspects the path (does it end in "/set"? what's the slug?)
//  must therefore strip the "?query" first, or a controllable
//  device's /set endpoint never matches.  Query parameters are
//  still read normally via _srv->arg()/args(), which parse them
//  independently of uri().
String WebAPI::_reqPath() {
  String u = _srv->uri();
  int q = u.indexOf('?');
  return (q >= 0) ? u.substring(0, q) : u;
}

// ── helpers ───────────────────────────────────────────────────
bool WebAPI::_requireAuth() {
  // Optional Host-header allowlist — reject before any other handling.
  if (!_hostAllowed()) {
    _srv->send(403, "text/plain", "Forbidden: host not allowed");
    return false;
  }
  // Effective dashboard login: NVS override (set via the setup portal)
  // first, else the compiled Secrets.h value.
  String user = Settings::webUser();
  // Empty user disables authentication — every route serves freely.
  if (user.isEmpty()) return true;
  // authenticate() returns true if the Authorization header matches.
  // The credentials travel inside the TLS tunnel, encrypted on the wire.
  if (_srv->authenticate(user.c_str(), Settings::webPass().c_str())) return true;
  // Send 401 + WWW-Authenticate so the browser prompts.
  _srv->requestAuthentication();
  return false;
}

// ── _hostAllowed ──────────────────────────────────────────────
//  Optional Host-header allowlist (WEB_HOST_ALLOWLIST).  Compares the
//  request's Host (port stripped, case-insensitive) against the
//  comma-separated allowlist.  Disabled when the list is empty, and
//  never enforced in AP / setup mode — the AP is the device's own
//  network, so a bad allowlist can't strand recovery.
bool WebAPI::_hostAllowed() {
  if (WEB_HOST_ALLOWLIST[0] == '\0') return true;          // feature off
  if (!_fw || _fw->apMode() || _setupMode()) return true;  // AP / setup
  String host = _srv->hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);  // strip ":port"
  host.trim();
  if (host.isEmpty()) return false;  // a filtered server demands a Host
  String list = WEB_HOST_ALLOWLIST;
  int start = 0;
  while (start < static_cast<int>(list.length())) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    String item = list.substring(start, comma);
    item.trim();
    if (item.length() && item.equalsIgnoreCase(host)) return true;
    start = comma + 1;
  }
  return false;
}

void WebAPI::_cors() {
  _srv->sendHeader("Access-Control-Allow-Origin",  WEB_CORS_ORIGIN);
  _srv->sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  _srv->sendHeader("Access-Control-Allow-Headers", "Content-Type");
  _srv->sendHeader("Cache-Control",                "no-cache");
}

void WebAPI::_json(JsonDocument& doc, int code) {
  _cors();
  String out;
  serializeJsonPretty(doc, out);
  _srv->send(code, "application/json", out);
}

void WebAPI::_buildSensorObj(JsonObject& obj, IDevice* p) {
  obj["name"]   = p->name();
  obj["slug"]   = p->slug();
  obj["active"] = p->active;
  obj["addr"]   = p->addr;
  // How the device is physically attached:
  //   builtin   – soldered on the Core / power base
  //   stackable – an M-Bus stacking module (internal bus)
  //   pluggable – a cabled Grove / Port-A unit
  switch (p->mount()) {
    case MountType::Builtin:   obj["mount"] = "builtin";   break;
    case MountType::Stackable: obj["mount"] = "stackable"; break;
    default:                   obj["mount"] = "pluggable"; break;
  }
  // True for output devices that accept commands via
  // POST /api/<slug>/set (body <param>=<value>; GET ?query also works).
  obj["controllable"] = p->controllable();
  // Label "shared" on Core1 (single physical bus); otherwise route
  // through BoardInfo's bus pointers so the label matches reality
  // regardless of which TwoWire instance the board happens to use.
  // For plugins bound behind an I2C hub, emit a compact descriptor
  // including the hub address and channel.
  const auto& bi = BoardInfo::detect();
  if (p->muxAddr != 0) {
    char buf[24];
    snprintf(buf, sizeof(buf), "hub 0x%02X ch%u", p->muxAddr, p->muxChannel);
    obj["bus"]     = buf;
    obj["hub"]     = p->muxAddr;
    obj["channel"] = p->muxChannel;
  } else if (bi.sharedBus()) {
    obj["bus"] = "shared";
  } else if (p->bus == bi.intBus) {
    obj["bus"] = "internal";
  } else if (p->bus == bi.extBus) {
    obj["bus"] = "external";
  } else {
    obj["bus"] = "?";
  }
  if (p->active) {
    JsonObject rd = obj["readings"].to<JsonObject>();
    p->toJson(rd);
  }
  // Controllable devices additionally describe their widgets via
  // controlSchema() so the dashboard can render interactive
  // controls generically.  Only emitted for active devices — an
  // unbound device has no live state to seed the widget values.
  if (p->active && p->controllable()) {
    JsonArray ctl = obj["controls"].to<JsonArray>();
    p->controlSchema(ctl);
  }
}

// ── routes ────────────────────────────────────────────────────
void WebAPI::_route_root() {
  _cors();
  // Setup mode → the setup portal instead of the dashboard (which
  // would have no network data to show anyway).
  if (_setupMode()) {
    _srv->send_P(200, "text/html; charset=utf-8", SETUP_HTML);
    return;
  }
  _srv->send_P(200, "text/html; charset=utf-8", DASH_HTML);
}

// ── _setupMode ───────────────────────────────────────────────
bool WebAPI::_setupMode() {
  return !Settings::isProvisioned() || (_fw && _fw->forceSetupPortal());
}

// ── URL-decode helpers (approach B setup form) ───────────────
//  We decode the submitted fields ourselves so any password
//  character round-trips exactly, independent of how the HTTPS
//  compat layer parses request bodies.
static String _urlDecode(const String& s) {
  auto hex = [](char h) -> int {
    if (h >= '0' && h <= '9') return h - '0';
    if (h >= 'a' && h <= 'f') return h - 'a' + 10;
    if (h >= 'A' && h <= 'F') return h - 'A' + 10;
    return -1;
  };
  String out;
  out.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < s.length()) {
      int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
      if (hi >= 0 && lo >= 0) { out += char((hi << 4) | lo); i += 2; }
      else out += c;
    } else {
      out += c;
    }
  }
  return out;
}

//  Extract one field from an application/x-www-form-urlencoded blob
//  ("a=1&b=2"), URL-decoded.  Returns "" when the key is absent.
//  Safe for values containing & = etc. because URLSearchParams
//  percent-encodes those inside values.
static String _formField(const String& body, const String& key) {
  int i = 0;
  const int n = body.length();
  while (i < n) {
    int amp = body.indexOf('&', i);
    if (amp < 0) amp = n;
    int eq = body.indexOf('=', i);
    if (eq >= 0 && eq < amp) {
      if (body.substring(i, eq) == key) return _urlDecode(body.substring(eq + 1, amp));
    }
    i = amp + 1;
  }
  return "";
}

//  Read one setup field: our own decode of the raw body/query first,
//  then the server's arg parser as a fallback.  Templated on the
//  server type so it works for both the HTTPS (_srv) and the plain
//  HTTP captive (_httpRedirect) servers.
template <class Srv>
static String _setupField(Srv* srv, const String& src, const char* key) {
  String v = _formField(src, key);
  if (v.isEmpty()) v = srv->arg(key);
  return v;
}

// ── serveSetupSubmit ─────────────────────────────────────────
//  Shared handler for POST /api/setup, used by both the HTTPS server
//  and the plain-HTTP captive server.  Saves Wi-Fi + the optional
//  dashboard / MQTT / Claude credentials to NVS, marks the device
//  provisioned, replies, then reboots.  POST keeps the secrets out of
//  the request-line log; we decode the fields ourselves so any
//  character round-trips exactly.
template <class Srv>
static void serveSetupSubmit(Srv* srv) {
  String src = srv->arg("plain");          // raw POST body, or ""
  if (src.isEmpty()) {                      // fall back to the query
    String full = srv->uri();
    int qm = full.indexOf('?');
    if (qm >= 0) src = full.substring(qm + 1);
  }

  String ssid   = _setupField(srv, src, "ssid");
  String wpass  = _setupField(srv, src, "wpass");
  String user   = _setupField(srv, src, "user");
  String upass  = _setupField(srv, src, "upass");
  String mqttH  = _setupField(srv, src, "mqtthost");
  String mqttPt = _setupField(srv, src, "mqttport");
  String mqttU  = _setupField(srv, src, "mqttuser");
  String mqttP  = _setupField(srv, src, "mqttpass");
  String claude = _setupField(srv, src, "claude");
  // Standalone-AP checkbox: present (any non-empty value) → run as our
  // own access point serving the dashboard, with no upstream Wi-Fi.
  bool apOnly   = _setupField(srv, src, "aponly").length() > 0;

  if (apOnly) {
    // No network to join — clear any stored STA creds so _connectWiFi()
    // takes the access-point path, and record the deliberate choice.
    Settings::setWifi("", "");
    Settings::setApOnly(true);
  } else {
    if (ssid.isEmpty()) {
      srv->send(400, "application/json", "{\"error\":\"wifi ssid required\"}");
      return;
    }
    Settings::setWifi(ssid, wpass);
    Settings::setApOnly(false);
  }
  // Optional sections: only overwrite when the user supplied a value,
  // otherwise keep whatever is already set (NVS or compiled).
  if (user.length())   Settings::setWebLogin(user, upass);
  if (mqttH.length()) {
    long p = mqttPt.toInt();                 // 0/invalid → keep current
    Settings::setMqttServer(mqttH,
        (p > 0 && p < 65536) ? (uint16_t)p : Settings::mqttPort());
  }
  if (mqttU.length())  Settings::setMqtt(mqttU, mqttP);
  if (claude.length()) Settings::setClaudeKey(claude);
  Settings::markProvisioned();

  // Static JSON (no field interpolation → no quoting hazards).
  if (apOnly) {
    srv->send(200, "application/json",
              "{\"ok\":true,\"msg\":\"Saved. Rebooting as a standalone access "
              "point — reconnect to the device's Wi-Fi and open its IP.\"}");
    Serial.println(F("[WebAPI] Provisioned via setup portal — standalone AP "
                     "(dashboard) mode. Rebooting."));
  } else {
    srv->send(200, "application/json",
              "{\"ok\":true,\"msg\":\"Saved. Rebooting to join your Wi-Fi...\"}");
    Serial.printf("[WebAPI] Provisioned via setup portal — SSID '%s'. "
                  "Rebooting.\n", ssid.c_str());
  }
  delay(1200);
  ESP.restart();
}

// ── _route_setup ─────────────────────────────────────────────
//  HTTPS entry point for the provisioning submit (see route table).
void WebAPI::_route_setup() { serveSetupSubmit(_srv); }

// ── _route_settingsGet ───────────────────────────────────────
//  GET /api/settings — current NON-SECRET state for the editor.
//  Returns SSID + usernames (so the form can pre-fill) and set/unset
//  booleans for the secrets.  Never returns a password or the key.
void WebAPI::_route_settingsGet() {
  JsonDocument doc;
  _buildSettingsDoc(doc);
  _json(doc);
}

// ── _route_settingsSave ──────────────────────────────────────
//  POST /api/settings/save — update any supplied credential, keeping
//  the existing value where a field is left blank, then reboot so the
//  changes take effect (re-inits WiFi / MQTT / Claude cleanly).  The
//  actual work is the templated _doSettingsSave(), shared verbatim with
//  the standalone-AP server.
void WebAPI::_route_settingsSave() {
  // Also reachable via the dedicated (GET-matched) route, so guard here
  // too — a forged GET must not be able to save settings + reboot.
  if (_csrfBlocked(_srv)) return;
  _doSettingsSave(_srv);
}

void WebAPI::_route_all() {
  JsonDocument doc;
  doc["uptime_s"]  = millis() / 1000;
  doc["ip"]        = _fw->apMode() ? WiFi.softAPIP().toString()
                                   : WiFi.localIP().toString();
  doc["port"]      = WEB_HTTPS_PORT;
  doc["scheme"]    = "https";
  doc["free_heap"] = ESP.getFreeHeap();
  doc["cpu_mhz"]   = ESP.getCpuFreqMHz();
  doc["flash_mb"]  = ESP.getFlashChipSize() / (1024 * 1024);

  // Wall-clock from the ESP32 RTC.  time_synced=true means the
  // value came from NTP; false means it's a fallback from the
  // RTC's post-reset epoch (i.e. seconds since boot mapped onto
  // 1970-01-01) and shouldn't be trusted as real time.
  char ts[24];
  bool synced = _fw->nowIso8601(ts, sizeof(ts));
  doc["datetime"]     = ts;
  doc["time_synced"]  = synced;
  doc["time_source"]  = _fw->timeSource();   // "ntp" | "rtc" | "none"

  // Board identity
  const auto& bi = _fw->board();
  JsonObject bobj = doc["board"].to<JsonObject>();
  bobj["short_name"] = bi.shortName;
  bobj["long_name"]  = bi.longName;
  bobj["i2c_int_sda"]= bi.i2cIntSda;
  bobj["i2c_int_scl"]= bi.i2cIntScl;
  bobj["i2c_ext_sda"]= bi.i2cExtSda;
  bobj["i2c_ext_scl"]= bi.i2cExtScl;

  JsonArray arr = doc["sensors"].to<JsonArray>();
  for (auto* p : _fw->plugins()) {
    JsonObject obj = arr.add<JsonObject>();
    _buildSensorObj(obj, p);
  }
  _json(doc);
}

// NOTE: the former _route_plugin() (one plugin's readings) and
// _route_control() (POST/GET /api/<slug>/set) lived here.  Their bodies
// now live in the templated _doControl() / _routeDynamic() further down,
// so the HTTPS server and the standalone-AP server share ONE copy
// instead of keeping two hand-synced duplicates.

void WebAPI::_route_scan() {
  // Delegate to Framework::scanReport() — it knows the board's bus
  // topology (shared vs separate) AND walks every registered hub's
  // 8 channels.  Doing the scan in WebAPI directly meant the old
  // implementation hardcoded Wire/Wire1 and ignored the muxes,
  // which on a Core1+PaHUB setup made everything behind the hub
  // invisible to the API.
  JsonDocument doc;
  _fw->scanReport(doc);
  _json(doc);
}

void WebAPI::_route_config() {
  JsonDocument doc;
  const auto& bi = _fw->board();
  doc["board_short_name"] = bi.shortName;
  doc["board_long_name"]  = bi.longName;
  doc["i2c_int_sda"]      = bi.i2cIntSda;
  doc["i2c_int_scl"]      = bi.i2cIntScl;
  doc["i2c_ext_sda"]      = bi.i2cExtSda;
  doc["i2c_ext_scl"]      = bi.i2cExtScl;
  doc["wifi_ssid"]        = Settings::wifiSsid();   // effective (NVS or compiled)
  doc["wifi_mode"]        = _fw->apMode() ? "ap" : "station";
  doc["ap_only"]          = Settings::apOnlyMode();
  doc["provisioned"]      = Settings::isProvisioned();
  doc["poll_ms"]          = POLL_MS;
  doc["i2c_int_freq"]     = I2C_INT_FREQ;
  doc["i2c_ext_freq"]     = I2C_EXT_FREQ;
  doc["out_web"]          = OUT_WEB;
  doc["out_serial"]       = OUT_SERIAL;
  doc["out_display"]      = OUT_DISPLAY;
  doc["display_scroll"]   = DISPLAY_SCROLL;
  doc["display_cycle_ms"] = DISPLAY_CYCLE_MS;
  _json(doc);
}

void WebAPI::_route_mqtt() {
  JsonDocument doc;
  _buildMqttStatus(doc);
  _json(doc);
}

// GET /api/alerts — alarm-engine state + the recent-event ring,
// straight from AlertManager::toJson().  POST /api/alerts/ack
// (handled in onNotFound) releases latched rules.
void WebAPI::_route_alerts() {
  JsonDocument doc;
  JsonObject o = doc.to<JsonObject>();
  _fw->alerts.toJson(o);
  _json(doc);
}

void WebAPI::_route_sdcard() {
  JsonDocument doc;
  _buildSdStatus(doc);
  _json(doc);
}

// Shared between /api/sdcard and /api/sdcard/flush so the JSON
// shape stays stable for dashboards polling either URL.
void WebAPI::_buildSdStatus(JsonDocument& doc) {
  const auto& st = _fw->sdlog.stats();
  const auto& bi = _fw->board();

  doc["enabled"]      = _fw->sdlog.enabled;
  doc["supported"]    = bi.hasSdCard;
  doc["present"]      = st.present;
  doc["active"]       = st.active;
  doc["log_interval_ms"] = SD_LOG_INTERVAL_MS;
  doc["spi_hz"]       = static_cast<uint32_t>(SD_SPI_HZ);
  // self_test: "pass", "fail", or "not-run".  A "fail" here is the
  // smoking gun for a card whose writes don't persist — see the
  // boot serial log for the suggested fix (lower SD_SPI_HZ).
  const char* selfTestStr = "not-run";
  if (st.selfTest == 1)
    selfTestStr = "pass";
  else if (st.selfTest == 0)
    selfTestStr = "fail";
  doc["self_test"]    = selfTestStr;

  const char* tname = "?";
  switch (st.cardType) {
    case CARD_NONE: tname = "none";   break;
    case CARD_MMC:  tname = "MMC";    break;
    case CARD_SD:   tname = "SDSC";   break;
    case CARD_SDHC: tname = "SDHC";   break;
  }
  doc["card_type"]      = tname;
  doc["card_size_mb"]   =
      static_cast<uint32_t>(st.cardSizeBytes / (1024ULL * 1024ULL));
  doc["boot_number"]    = st.bootNumber;
  doc["filename"]       = st.filename;
  doc["rows_written"]   = st.rowsWritten;
  doc["bytes_written"]  = st.bytesWritten;
  doc["write_failures"] = st.writeFailures;
  if (st.lastWriteMs) {
    doc["since_last_write_s"] = (millis() - st.lastWriteMs) / 1000;
  }
}

// ── _route_endpoints ─────────────────────────────────────────
//  Self-describing endpoint list.  Hand-maintained — the HTTPS
//  server library doesn't expose its registered-handler table.
//  Keep this in sync when adding new routes; one place to update
//  beats hunting through README and dashboard separately.
void WebAPI::_route_endpoints() {
  struct Ep { const char* method; const char* url; const char* desc; };
  static const Ep eps[] = {
    { "GET", "/",                 "Live HTML dashboard (auto-refresh ~5s)" },
    { "GET", "/api/all",
      "All plugins + readings + board + system info" },
    { "GET", "/api/{slug}",
      "One plugin's readings (e.g. /api/env4, /api/heart)" },
    { "POST", "/api/{slug}/set",
      "Control an output device (POST body relay1=1; header "
      "X-Requested-With required when WEB_CSRF_PROTECT)" },
    { "GET", "/api/scan",
      "Live I2C scan: root bus(es) + every hub channel" },
    { "GET", "/api/config",       "Framework configuration + board info" },
    { "GET", "/api/rescan",
      "Re-run boot-time scan-and-bind; returns fresh /api/all" },
    { "GET", "/api/mqtt",
      "MQTT runtime status (connected/state/counts/timings)" },
    { "GET", "/api/mqtt/publish",
      "Force one immediate publish cycle; returns status" },
    { "GET", "/api/sdcard",
      "SD card + log file status (boot/filename/rows/bytes)" },
    { "GET", "/api/sdcard/flush",
      "Commit (close+reopen) buffered log writes to the card now" },
    { "GET", "/api/sdcard/eject",
      "Cleanly close the file + unmount; card safe to remove" },
    { "GET", "/api/endpoints",    "This list" },
  };

  JsonDocument doc;
  doc["uptime_s"] = millis() / 1000;
  doc["ip"]       = _fw->apMode() ? WiFi.softAPIP().toString()
                                  : WiFi.localIP().toString();
  doc["port"]     = WEB_HTTPS_PORT;
  doc["scheme"]   = "https";
  doc["auth"]     = Settings::webUser().length() > 0 ? "basic" : "none";

  JsonArray arr = doc["endpoints"].to<JsonArray>();
  for (const auto& e : eps) {
    JsonObject o = arr.add<JsonObject>();
    o["method"]      = e.method;
    o["url"]         = e.url;
    o["description"] = e.desc;
  }
  _json(doc);
}

// Shared between /api/mqtt and /api/mqtt/publish so both endpoints
// emit the same status fields.  Keeps the JSON shape stable for
// dashboards that poll either URL.
void WebAPI::_buildMqttStatus(JsonDocument& doc) {
  const auto& st  = _fw->mqtt.stats();
  bool        wifiUp    = (WiFi.status() == WL_CONNECTED);
  bool        connected = _fw->mqtt.connected();
  uint32_t    now       = millis();

  doc["enabled"]        = _fw->mqtt.enabled;
  doc["configured"]     = Settings::mqttHost().length() > 0;
  doc["host"]           = Settings::mqttHost();
  doc["port"]           = Settings::mqttPort();
  doc["transport"]      = MQTTOut::transport();   // "plain" or "tls"
  doc["tls"]            = MQTTOut::tls();
  doc["tls_mutual"]     = MQTTOut::tlsMutual();    // X.509 client-cert auth
  doc["tls_verified"]   = MQTTOut::tlsVerified();  // broker checked vs CA cert
  doc["client_id"]      = MQTT_CLIENT_ID;
  doc["base_topic"]     = MQTT_BASE_TOPIC;
  // HA discovery is suppressed at runtime only for a mutual-TLS
  // broker (e.g. AWS IoT) that rejects the homeassistant/# tree, so
  // report the effective value.
  doc["ha_discovery"]   = (MQTT_HA_DISCOVERY && !MQTTOut::tlsMutual());
  doc["wifi_connected"] = wifiUp;
  doc["connected"]      = connected;
  doc["last_state"]     = st.lastState;
  doc["last_state_text"] = MQTTOut::stateText(st.lastState);
  doc["connect_attempts"]  = st.connectAttempts;
  doc["connect_successes"] = st.connectSuccesses;
  doc["publish_count"]     = st.publishCount;
  doc["publish_failures"]  = st.publishFailures;
  doc["publish_interval_ms"] = MQTT_PUBLISH_MS;
  // "seconds since" fields default to null when never set, otherwise
  // an integer.  Lets dashboards distinguish "never happened" from
  // "happened just now".
  if (st.lastConnectMs) {
    doc["since_last_connect_s"] = (now - st.lastConnectMs) / 1000;
  }
  if (st.lastDisconnectMs) {
    doc["since_last_disconnect_s"] = (now - st.lastDisconnectMs) / 1000;
  }
  if (st.lastPublishMs) {
    doc["since_last_publish_s"] = (now - st.lastPublishMs) / 1000;
  }
}

// ── _buildSettingsDoc ────────────────────────────────────────
//  Non-secret settings snapshot for the editor: SSID + usernames (so
//  the form pre-fills) and set/unset booleans for the secrets.  Never
//  emits a password or the API key.  Single source of truth for
//  /api/settings on both the HTTPS and the standalone-AP servers.
void WebAPI::_buildSettingsDoc(JsonDocument& doc) {
  doc["provisioned"] = Settings::isProvisioned();
  doc["ap_only"]     = Settings::apOnlyMode();
  doc["wifi_ssid"]   = Settings::wifiSsid();
  doc["web_user"]    = Settings::webUser();
  doc["mqtt_host"]   = Settings::mqttHost();
  doc["mqtt_port"]   = Settings::mqttPort();
  doc["mqtt_user"]   = Settings::mqttUser();
  doc["mqtt_set"]    = Settings::mqttConfigured();
  doc["claude_set"]  = Settings::claudeConfigured();
}

// ── _doSettingsSave ──────────────────────────────────────────
//  POST /api/settings/save body: update any supplied credential, keep
//  the current value where a field is blank, reply, then reboot so the
//  changes take effect (re-inits WiFi / MQTT / Claude cleanly).  Shared
//  verbatim by the HTTPS server (_route_settingsSave) and the
//  standalone-AP server.  Templated on the server type.
template <class Srv>
void WebAPI::_doSettingsSave(Srv* s) {
  String src = s->arg("plain");
  if (src.isEmpty()) {
    String full = s->uri();
    int qm = full.indexOf('?');
    if (qm >= 0) src = full.substring(qm + 1);
  }
  String ssid   = _setupField(s, src, "ssid");
  String wpass  = _setupField(s, src, "wpass");
  String user   = _setupField(s, src, "user");
  String upass  = _setupField(s, src, "upass");
  String mqttH  = _setupField(s, src, "mqtthost");
  String mqttPt = _setupField(s, src, "mqttport");
  String mqttU  = _setupField(s, src, "mqttuser");
  String mqttP  = _setupField(s, src, "mqttpass");
  String claude = _setupField(s, src, "claude");
  bool   apOnly = _setupField(s, src, "aponly").length() > 0;

  // Standalone-AP toggle takes precedence: when on, clear the stored STA
  // credentials so the next boot comes up as an access point serving the
  // dashboard.  When off, fall through to the normal SSID handling (and
  // make sure any earlier AP-only choice is cleared).
  if (apOnly) {
    Settings::setWifi("", "");
    Settings::setApOnly(true);
  } else {
    Settings::setApOnly(false);
    // A blank password/key means "keep current", so merge against the
    // present effective value before saving the pair.
    if (ssid.length())
      Settings::setWifi(ssid, wpass.length() ? wpass : Settings::wifiPass());
  }
  if (user.length())
    Settings::setWebLogin(user, upass.length() ? upass : Settings::webPass());
  if (mqttH.length()) {
    long p = mqttPt.toInt();
    Settings::setMqttServer(mqttH,
        (p > 0 && p < 65536) ? (uint16_t)p : Settings::mqttPort());
  }
  if (mqttU.length())
    Settings::setMqtt(mqttU, mqttP.length() ? mqttP : Settings::mqttPass());
  if (claude.length()) Settings::setClaudeKey(claude);

  s->send(200, "application/json",
          "{\"ok\":true,\"msg\":\"Saved. Rebooting...\"}");
  Serial.println(F("[WebAPI] Settings updated — rebooting."));
  delay(1200);
  ESP.restart();
}

// ── _csrfBlocked ─────────────────────────────────────────────
//  Safe-method + CSRF guard for state-changing routes.  Returns true
//  (and sends the rejection) unless the request is a POST carrying the
//  custom X-Requested-With header.  Rationale: a control/settings call
//  changes state, so a GET (forgeable with <img>/prefetch, and not even
//  subject to CORS) is refused, and a cross-site page cannot attach a
//  custom header without a CORS preflight this server doesn't permit —
//  so a forged request from another origin is blocked even when the
//  browser still has cached Basic-Auth credentials.  The built-in
//  dashboard sends both.  Disabled by WEB_CSRF_PROTECT = false.
template <class Srv>
bool WebAPI::_csrfBlocked(Srv* s) {
  if (!WEB_CSRF_PROTECT) return false;
  if (s->method() != HTTP_POST) {
    apCors(s);
    s->send(405, "application/json",
            "{\"error\":\"use POST (CSRF guard)\"}");
    return true;
  }
  if (s->header("X-Requested-With").length() == 0) {
    apCors(s);
    s->send(403, "application/json",
            "{\"error\":\"missing X-Requested-With header (CSRF guard)\"}");
    return true;
  }
  return false;
}

// ── _doControl ───────────────────────────────────────────────
//  POST /api/<slug>/set (body <param>=<value>[&...]) — preferred — or
//  GET /api/<slug>/set?<param>=<value>[&...].  Drives a controllable
//  output device.  Each param is handed to the plugin's command(),
//  which validates it; applied params land in "applied", rejected ones
//  in "rejected" with the hardware left untouched.  Params are read
//  from the query string AND a url-encoded POST body, merged (the
//  secure server doesn't fold a POST body into arg()/args() reliably,
//  so the body is decoded here).  Shared by both servers; templated.
template <class Srv>
void WebAPI::_doControl(Srv* s, const String& slug) {
  // A control call changes physical state — gate it behind the CSRF /
  // safe-method guard (POST + X-Requested-With).  See _csrfBlocked().
  if (_csrfBlocked(s)) return;

  IDevice* target = nullptr;
  for (auto* p : _fw->plugins()) {
    if (String(p->slug()) == slug) { target = p; break; }
  }

  JsonDocument doc;
  doc["slug"] = slug;

  if (!target) {
    doc["error"] = "plugin not found";
    apSendJson(s, doc, 404);
    return;
  }
  if (!target->active) {
    doc["error"] = "plugin not active";
    apSendJson(s, doc, 409);
    return;
  }
  if (!target->controllable()) {
    doc["error"] = "plugin is not controllable";
    apSendJson(s, doc, 400);
    return;
  }

  JsonArray applied  = doc["applied"].to<JsonArray>();
  JsonArray rejected = doc["rejected"].to<JsonArray>();

  // Apply one param→value pair through the plugin, recording the result.
  auto apply = [&](const String& k, const String& v) {
    if (k.isEmpty()) return;
    bool ok = target->command(k, v);
    JsonObject e = (ok ? applied : rejected).add<JsonObject>();
    e["param"] = k;
    e["value"] = v;
  };

  // 1) Query-string params (the GET path, and any ?query on a POST).
  //    Skip "plain" — the server's name for the raw POST body, decoded
  //    below, not a real command parameter.
  int nArgs = s->args();
  for (int i = 0; i < nArgs; i++) {
    String k = s->argName(i);
    if (k == "plain") continue;
    apply(k, s->arg(i));
  }

  // 2) url-encoded POST body ("a=1&b=2"), decoded ourselves.
  String body = s->arg("plain");
  int start = 0;
  const int n = body.length();
  while (start < n) {
    int amp = body.indexOf('&', start);
    if (amp < 0) amp = n;
    int eq = body.indexOf('=', start);
    if (eq >= 0 && eq < amp) {
      String k = body.substring(start, eq);
      String v = _urlDecode(body.substring(eq + 1, amp));
      k.trim();
      apply(k, v);
    }
    start = amp + 1;
  }
  // ok = at least one command applied and nothing rejected.
  doc["ok"] = (rejected.size() == 0 && applied.size() > 0);
  // Echo the device's resulting state so a client sees the effect.
  JsonObject st = doc["state"].to<JsonObject>();
  target->toJson(st);
  apSendJson(s, doc, rejected.size() ? 400 : 200);
}

// ── _routeDynamic ────────────────────────────────────────────
//  The shared "everything that isn't a fixed route" dispatcher.  Both
//  the HTTPS onNotFound and the standalone-AP onNotFound call this
//  AFTER authenticating, so there is one copy of: the alerts-rules
//  routes, POST /api/settings/save, GET /api/settings, the control
//  /set endpoint, plugin reads, and the JSON 404.  Templated on the
//  server type.  `path` is the request path with any query stripped.
template <class Srv>
void WebAPI::_routeDynamic(Srv* s, const String& path) {
  // ── Alarm-engine rule CRUD ────────────────────────────────
  //  The mutating routes (ack / save / delete / reset) change persisted
  //  state, so each passes the CSRF / safe-method guard first; the rule
  //  LIST (/api/alerts/rules) is a read and is left open.
  if (path == "/api/alerts/ack") {
    if (_csrfBlocked(s)) return;
    int rid = s->hasArg("rule") ? s->arg("rule").toInt() : -1;
    _fw->alerts.ack(rid);
    apCors(s);
    s->send(200, "application/json", "{\"ok\":true}");
    return;
  }
  if (path == "/api/alerts/rules") {
    JsonDocument doc;
    JsonArray arr = doc["rules"].to<JsonArray>();
    _fw->alerts.rulesToJson(arr);
    apSendJson(s, doc);
    return;
  }
  if (path == "/api/alerts/rules/save") {
    if (_csrfBlocked(s)) return;
    JsonDocument d;
    bool ok = !deserializeJson(d, s->arg("plain")) &&
              _fw->alerts.upsertRule(d.as<JsonObjectConst>());
    apCors(s);
    s->send(ok ? 200 : 400, "application/json",
            ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return;
  }
  if (path == "/api/alerts/rules/delete") {
    if (_csrfBlocked(s)) return;
    long id = s->hasArg("id") ? s->arg("id").toInt() : -1;
    bool ok = id > 0 && _fw->alerts.deleteRule((uint8_t)id);
    apCors(s);
    s->send(ok ? 200 : 400, "application/json",
            ok ? "{\"ok\":true}" : "{\"ok\":false}");
    return;
  }
  if (path == "/api/alerts/rules/reset") {
    if (_csrfBlocked(s)) return;
    _fw->alerts.resetRules();
    apCors(s);
    s->send(200, "application/json", "{\"ok\":true}");
    return;
  }

  // ── Settings ──────────────────────────────────────────────
  //  The on() registrations only match GET, so the settings POST save
  //  lands here.  /api/settings (GET) is handled by a dedicated route
  //  on both servers, but it is covered here too for safety.  The save
  //  reboots the device, so it is CSRF-guarded; the GET read is open.
  if (path == "/api/settings/save") {
    if (_csrfBlocked(s)) return;
    _doSettingsSave(s);
    return;
  }
  if (path == "/api/settings") {
    JsonDocument doc;
    _buildSettingsDoc(doc);
    apSendJson(s, doc);
    return;
  }

  // ── 404 for anything that isn't an /api/ path ─────────────
  if (!path.startsWith("/api/")) {
    JsonDocument doc;
    doc["error"] = "not found";
    doc["uri"]   = s->uri();
    apSendJson(s, doc, 404);
    return;
  }

  // ── /api/<slug>/set → control;  /api/<slug> → readings ────
  if (path.endsWith("/set")) {
    _doControl(s, path.substring(5, path.length() - 4));
    return;
  }
  String slug = path.substring(5);  // strip "/api/"
  for (auto* p : _fw->plugins()) {
    if (String(p->slug()) == slug) {
      JsonDocument doc;
      JsonObject obj = doc.to<JsonObject>();
      _buildSensorObj(obj, p);
      apSendJson(s, doc);
      return;
    }
  }
  JsonDocument doc;
  doc["error"] = "plugin not found: " + slug;
  apSendJson(s, doc, 404);
}

// ── _wirePlainAp ─────────────────────────────────────────────
//  Standalone-AP dashboard over plain HTTP.  Intentionally mirrors the
//  HTTPS route table in begin() — if you add or rename a route there,
//  mirror it here.  Reuses the same server-independent builders
//  (_buildSensorObj / _buildMqttStatus / _buildSdStatus /
//  Framework::scanReport) so the JSON shapes stay identical; only the
//  transport and the /api/all scheme+port differ.  (/api/setup and
//  /api/endpoints are intentionally omitted: this path only runs when
//  already provisioned, and the dashboard degrades gracefully without
//  the endpoint chips.)
template <class Srv>
void WebAPI::_wirePlainAp(Srv* s) {
  // Capture the CSRF header so _doControl can read it on the plain-AP
  // server too (see the matching call in begin()).
  {
    static const char* csrfHdr[] = {"X-Requested-With"};
    s->collectHeaders(csrfHdr, 1);
  }
  // ── HTML pages ────────────────────────────────────────────
  s->on("/", [this, s]() {
    if (!apAuth(s)) return;
    apCors(s);
    s->send_P(200, "text/html; charset=utf-8", DASH_HTML);
  });
  s->on("/settings", [this, s]() {
    if (!apAuth(s)) return;
    apCors(s);
    s->send_P(200, "text/html; charset=utf-8", SETTINGS_HTML);
  });

  // /api/all — same doc as _route_all(), but reports the plain-HTTP
  // scheme/port and always the AP IP.
  auto buildAll = [this](JsonDocument& doc) {
    doc["uptime_s"]  = millis() / 1000;
    doc["ip"]        = WiFi.softAPIP().toString();
    doc["port"]      = WEB_HTTP_REDIRECT_PORT;
    doc["scheme"]    = "http";
    doc["free_heap"] = ESP.getFreeHeap();
    doc["cpu_mhz"]   = ESP.getCpuFreqMHz();
    doc["flash_mb"]  = ESP.getFlashChipSize() / (1024 * 1024);
    char ts[24];
    bool synced = _fw->nowIso8601(ts, sizeof(ts));
    doc["datetime"]    = ts;
    doc["time_synced"] = synced;
    doc["time_source"] = _fw->timeSource();   // "ntp" | "rtc" | "none"
    const auto& bi = _fw->board();
    JsonObject bobj = doc["board"].to<JsonObject>();
    bobj["short_name"] = bi.shortName;
    bobj["long_name"]  = bi.longName;
    bobj["i2c_int_sda"]= bi.i2cIntSda;
    bobj["i2c_int_scl"]= bi.i2cIntScl;
    bobj["i2c_ext_sda"]= bi.i2cExtSda;
    bobj["i2c_ext_scl"]= bi.i2cExtScl;
    JsonArray arr = doc["sensors"].to<JsonArray>();
    for (auto* p : _fw->plugins()) {
      JsonObject obj = arr.add<JsonObject>();
      _buildSensorObj(obj, p);
    }
  };
  s->on("/api/all", [this, s, buildAll]() {
    if (!apAuth(s)) return;
    JsonDocument doc; buildAll(doc); apSendJson(s, doc);
  });
  s->on("/api/rescan", [this, s, buildAll]() {
    if (!apAuth(s)) return;
    _fw->rescanAll();
    JsonDocument doc; buildAll(doc); apSendJson(s, doc);
  });
  s->on("/api/scan", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _fw->scanReport(doc); apSendJson(s, doc);
  });
  s->on("/api/config", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc;
    const auto& bi = _fw->board();
    doc["board_short_name"] = bi.shortName;
    doc["board_long_name"]  = bi.longName;
    doc["i2c_int_sda"]      = bi.i2cIntSda;
    doc["i2c_int_scl"]      = bi.i2cIntScl;
    doc["i2c_ext_sda"]      = bi.i2cExtSda;
    doc["i2c_ext_scl"]      = bi.i2cExtScl;
    doc["wifi_ssid"]        = Settings::wifiSsid();
    doc["wifi_mode"]        = _fw->apMode() ? "ap" : "station";
    doc["ap_only"]          = Settings::apOnlyMode();
    doc["provisioned"]      = Settings::isProvisioned();
    doc["poll_ms"]          = POLL_MS;
    doc["i2c_int_freq"]     = I2C_INT_FREQ;
    doc["i2c_ext_freq"]     = I2C_EXT_FREQ;
    doc["out_web"]          = OUT_WEB;
    doc["out_serial"]       = OUT_SERIAL;
    doc["out_display"]      = OUT_DISPLAY;
    doc["display_scroll"]   = DISPLAY_SCROLL;
    doc["display_cycle_ms"] = DISPLAY_CYCLE_MS;
    doc["scheme"]           = "http";
    apSendJson(s, doc);
  });
  s->on("/api/mqtt", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _buildMqttStatus(doc); apSendJson(s, doc);
  });
  s->on("/api/alerts", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; JsonObject o = doc.to<JsonObject>();
    _fw->alerts.toJson(o); apSendJson(s, doc);
  });
  s->on("/api/mqtt/publish", [this, s]() {
    if (!apAuth(s)) return;
    bool fired = _fw->mqtt.publishNow();
    JsonDocument doc; _buildMqttStatus(doc); doc["publish_now"] = fired;
    apSendJson(s, doc);
  });
  s->on("/api/sdcard", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc; _buildSdStatus(doc); apSendJson(s, doc);
  });
  s->on("/api/sdcard/flush", [this, s]() {
    if (!apAuth(s)) return;
    bool ok = _fw->sdlog.flush();
    JsonDocument doc; _buildSdStatus(doc); doc["flushed"] = ok;
    apSendJson(s, doc);
  });
  s->on("/api/sdcard/eject", [this, s]() {
    if (!apAuth(s)) return;
    bool ok = _fw->sdlog.eject();
    JsonDocument doc; _buildSdStatus(doc); doc["ejected"] = ok;
    apSendJson(s, doc);
  });
  s->on("/api/settings", [this, s]() {
    if (!apAuth(s)) return;
    JsonDocument doc;
    _buildSettingsDoc(doc);
    apSendJson(s, doc);
  });

  // Slug reads, control /set, the alerts-rules routes, and POST
  // /api/settings/save all share ONE implementation with the HTTPS
  // server — see _routeDynamic().  (Auth happens here first; the shared
  // dispatcher assumes the caller has already authorised the request.)
  s->onNotFound([this, s]() {
    if (!apAuth(s)) return;
    _routeDynamic(s, apPath(s));
  });
}
#endif  // OUT_WEB

