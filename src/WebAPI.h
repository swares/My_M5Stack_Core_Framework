#pragma once
// ============================================================
//  WebAPI.h  –  HTTPS REST JSON server + embedded HTML dashboard
//
//  The dashboard and REST API are served over TLS by
//  ESPWebServerSecure — the esp32_idf5_https_server_compat
//  wrapper, which mirrors the standard Arduino WebServer API.  A
//  second, plain-HTTP ESPWebServer on port 80 serves no content;
//  it only 301-redirects callers to the HTTPS port.  The device's
//  self-signed TLS certificate is embedded at compile time — see
//  https_cert.h.
//
//  Requires TWO libraries (install both):
//    • esp32_idf5_https_server_compat  – the WebServer-style wrapper
//    • esp32_idf5_https_server         – the TLS server it builds on
// ============================================================
#include "Config.h"  // for the OUT_WEB build switch

#if OUT_WEB
#include <WiFi.h>
#include <ESPWebServer.hpp>
#include <ESPWebServerSecure.hpp>
#include <ArduinoJson.h>
#include "IDevice.h"

class Framework;
// WiFiUDP (captive-portal DNS responder, setup mode only) comes from
// <WiFi.h> above, where it is a typedef for NetworkUDP — so no forward
// declaration here (forward-declaring a typedef as `class` is illegal).

class WebAPI {
 public:
  bool enabled = OUT_WEB;

  void begin(Framework* fw);
  void update();

 private:
  ESPWebServerSecure* _srv = nullptr;     // HTTPS  :WEB_HTTPS_PORT
  ESPWebServer* _httpRedirect = nullptr;  // HTTP :WEB_HTTP_REDIRECT_PORT
  WiFiUDP* _dnsUdp = nullptr;             // captive DNS, setup mode only
  Framework* _fw = nullptr;
  // True when serving the dashboard over PLAIN HTTP as a provisioned
  // standalone AP (WEB_AP_PLAIN_HTTP && apOnly).  Set in begin(); in
  // this mode the HTTPS server (_srv) is never created.
  bool _plainAp = false;

  // Service one pending captive-DNS query (called from update() in
  // setup mode).  Answers any A query with the AP IP.  Uses WiFiUDP
  // (lwIP sockets layer) — thread-safe, unlike DNSServer / AsyncUDP
  // which call raw udp_new and assert on this ESP-IDF build.
  void _serviceCaptiveDns();

  // Handler for the plain-HTTP port-80 server: 301 every request to
  // the equivalent HTTPS URL.
  void _sendRedirect();

  // Path portion of the current request URI, with any "?query"
  // stripped.  ESPWebServerSecure::uri() returns the full request
  // target (path + query), unlike the standard Arduino WebServer —
  // so any routing logic that inspects the path must strip the
  // query first or "/api/pps/set?vset=5" never matches "/set".
  String _reqPath();

  void _route_root();
  void _route_setup();        // first-boot provisioning submit (approach B)
  void _route_settingsGet();  // GET  /api/settings — non-secret state
  void _route_settingsSave(); // POST /api/settings/save — edit + reboot
  // True when "/" should serve the setup portal instead of the
  // dashboard: the device is unprovisioned, OR it failed to join its
  // configured Wi-Fi and dropped to the recovery AP.  In this mode
  // "/" and "/api/setup" are served without auth so the credentials
  // can be (re)entered.
  bool _setupMode();
  void _route_all();
  void _route_plugin();
  void _route_control(const String& slug);
  void _route_scan();
  void _route_config();
  void _route_mqtt();
  void _route_alerts();   // GET /api/alerts — alarm engine state + ring
  void _route_sdcard();
  void _route_endpoints();
  void _route_404();

  // Registers the full dashboard + REST API on a PLAIN-HTTP server for
  // provisioned standalone-AP mode (WEB_AP_PLAIN_HTTP).  Templated on
  // the server type, following the serveSetupSubmit<Srv> pattern, so it
  // reuses the same _build* helpers the HTTPS routes use.  Defined in
  // WebAPI.cpp; only instantiated there (for ESPWebServer).
  template <class Srv> void _wirePlainAp(Srv* s);

  // Fills `doc` with the MQTT status snapshot used by both
  // /api/mqtt and /api/mqtt/publish.  Declared here so both routes
  // share one source of truth for the JSON shape.
  void _buildMqttStatus(JsonDocument& doc);

  // Fills `doc` with the SD logger status snapshot used by both
  // /api/sdcard and /api/sdcard/flush.  Same single-source-of-truth
  // pattern as _buildMqttStatus.
  void _buildSdStatus(JsonDocument& doc);

  // Returns true iff the current request should be served.  When
  // WEB_AUTH_USER is empty this is unconditionally true.  Otherwise
  // checks HTTP Basic Auth against the configured credentials and,
  // if they don't match, sends a 401 with WWW-Authenticate and
  // returns false.  Every route handler calls this first.  The
  // credentials now travel inside the TLS tunnel, so they are
  // encrypted on the wire.
  bool _requireAuth();

  // Optional Host-header allowlist (WEB_HOST_ALLOWLIST).  Returns false
  // only when the list is non-empty, we're in station mode, and the
  // request's Host header matches nothing on it.  Off (always true) when
  // the list is empty or in AP / setup mode.  Checked first by
  // _requireAuth(); a rejected request gets a 403.
  bool _hostAllowed();

  void _cors();
  void _json(JsonDocument& doc, int code = 200);
  void _buildSensorObj(JsonObject& obj, IDevice* p);
};

#else   // !OUT_WEB
// ── Stub — WebAPI compiled out via Config.h (OUT_WEB = false) ─
//  Mirrors the real class's public surface (begin / update /
//  enabled) so Framework keeps building; every method is an inert
//  no-op and the ESPWebServer / ESPWebServerSecure libraries are
//  not pulled into the build at all.
class Framework;
class WebAPI {
 public:
  bool enabled = false;
  void begin(Framework*) {}
  void update() {}
};
#endif  // OUT_WEB
