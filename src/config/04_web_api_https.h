#pragma once
// Section of the framework Config.h, split out by
// apply_refactors.py.  Edit values here; the includes in
// Config.h pull every section in the original order.

// ── Web API (HTTPS) ───────────────────────────────────────────
//  The dashboard and REST API are served over HTTPS/TLS on
//  WEB_HTTPS_PORT.  A second, plain-HTTP server on
//  WEB_HTTP_REDIRECT_PORT serves no content — it only issues a 301
//  redirect to the HTTPS URL, so an old "http://<ip>" bookmark
//  still lands the user on the secure page.
//
//  Required libraries: HTTPS uses ESPWebServerSecure from
//  "esp32_idf5_https_server_compat", which itself depends on
//  "esp32_idf5_https_server".  BOTH must be installed — the compat
//  wrapper alone won't compile.  See the README for install notes.
//
//  TLS certificate: a self-signed certificate is embedded in the
//  firmware (see src/https_cert.h) and used as-is — there is no
//  on-device key generation (the cert library's generator is
//  unstable on this ESP-IDF, and a fixed cert keeps boot instant).
//  Because it is self-signed, a browser shows a one-time "not
//  secure" warning the first time you connect — accept it once.
//  To use your own certificate, replace the DER byte arrays in
//  src/https_cert.h (regeneration commands are in that file).
[[maybe_unused]] constexpr unsigned WEB_HTTPS_PORT = 443;
[[maybe_unused]] constexpr unsigned WEB_HTTP_REDIRECT_PORT = 80;

// Plain-HTTP access-point dashboard.  When true AND the device is
// running as a STANDALONE access point (apOnlyMode — provisioned with
// no upstream Wi-Fi), the full dashboard + REST API are served over
// plain HTTP on WEB_HTTP_REDIRECT_PORT (80) and the HTTPS server is
// NOT started.  Rationale: on the device's own WPA2 AP the link is
// already encrypted, so a self-signed cert only adds a browser
// warning with no security benefit; plain HTTP also frees the TLS
// RAM.  No effect in station (joined-Wi-Fi) mode — that always uses
// HTTPS + the port-80 redirect.  Default false: HTTPS everywhere,
// behavior unchanged.  (Setup-mode captive portal is already plain
// HTTP regardless of this flag.)
[[maybe_unused]] constexpr bool WEB_AP_PLAIN_HTTP = false;

// Optional Host-header allowlist (STATION mode only).  When non-empty,
// the HTTPS dashboard/API answers a request ONLY if its Host header
// matches one of these comma-separated values — your device IP and/or a
// hostname you reach it by (e.g. "192.168.1.50,m5stack.local").  "" =
// off (answer any Host).  This guards against DNS-rebinding and access
// via unexpected hostnames; it does NOT restrict client IPs — do that at
// your router/firewall.  Never enforced in AP / setup mode (the AP is
// the device's own network), so a wrong value can't lock you out of
// recovery.  HTTP Basic Auth (WEB_AUTH_USER) remains the real access
// control; this is a lightweight extra.
[[maybe_unused]] constexpr char WEB_HOST_ALLOWLIST[] = "";

// CORS Access-Control-Allow-Origin sent on every API response.  Default
// "*" answers cross-origin reads from any page (the framework's original
// behaviour).  A control /set is a CORS "simple request" (no preflight),
// so on a shared network a malicious page could fire commands at the
// device if it has cached credentials.  Lock this to the one origin you
// serve the dashboard from (e.g. "https://192.168.1.50") to have the
// browser reject other origins.  HTTP Basic Auth remains the real access
// control; this is a lightweight extra and is NOT a CSRF token.
[[maybe_unused]] constexpr char WEB_CORS_ORIGIN[] = "*";

// CSRF protection for the state-changing control endpoint.  When true,
// POST /api/<slug>/set is required AND must carry the custom header
//   X-Requested-With: <anything>
// A cross-site page cannot attach a custom header to a request without
// triggering a CORS preflight, which this server does not allow for that
// header — so a forged control command from another origin is blocked,
// even though Basic-Auth credentials would otherwise ride along.  The
// built-in dashboard sends the header automatically; only hand-written
// GET bookmarks to /set stop working (state changes are POST-only now).
//   ⚠ Requires the web server to surface request headers via
//   collectHeaders().  If your HTTPS library build does not (controls
//   start returning 403 "missing X-Requested-With"), set this false to
//   restore the old behaviour and report it.
[[maybe_unused]] constexpr bool WEB_CSRF_PROTECT = true;

// Minimum free heap (bytes) required before the framework will OPEN a new
// outbound TLS socket (MQTTS, the escalation router, the direct Claude
// API, and the AlertManager webhook/email POSTs).  Each mbedTLS session
// needs tens of KB; if the heap is already low, attempting another
// connection just fragments memory and fails — and can starve the HTTPS
// dashboard server mid-handshake.  Below this watermark those callers
// defer/skip gracefully (and retry later) instead.  Tune to taste; 0
// disables the guard.
[[maybe_unused]] constexpr unsigned long MIN_TLS_HEAP = 50000UL;

// Back-compat alias: older code/docs that say WEB_SERVER_PORT mean
// "the port the dashboard is on", which is now the HTTPS port.
#define WEB_SERVER_PORT WEB_HTTPS_PORT

// Optional HTTP Basic Auth for the web API and dashboard.
//   - Leave WEB_AUTH_USER empty ("") to disable authentication
//     entirely.  All endpoints will be open, same as before.
//   - Set BOTH to enable: every route (including "/" and every
//     "/api/..." endpoint) will return 401 unless the request
//     provides matching credentials.  Browsers prompt automatically.
//
// Because the API is now HTTPS, these credentials travel inside the
// TLS tunnel and are encrypted on the wire — a big improvement over
// the old plain-HTTP server.  Still pick a dedicated password: the
// certificate is self-signed, and anyone with physical access to
// the SD card can read the server's private key (/https_pk.der).
//  ⚠ WEB_AUTH_USER / WEB_AUTH_PASS are defined in Secrets.h
//  (git-ignored).  The shipped default user / password pair is
//  REFUSED at boot when SECURITY_STRICT (below) is true — change it
//  in Secrets.h before deploying.
