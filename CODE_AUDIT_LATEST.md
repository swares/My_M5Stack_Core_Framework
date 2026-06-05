# Code Audit — M5Stack Core Framework (latest, standalone)

_Date: 2026-06-05_

A complete, self-contained audit of the project as it currently stands. This
report does not assume any prior audit; every finding and observation below is
stated in full here. Severity reflects this codebase's context: a hobby/maker
embedded device plus an optional Linux-side helper, typically on a home LAN.

---

## 1. Scope & methodology

**In scope (read / scanned):**

- Firmware entrypoint `M5Stack_Core_Framework.ino`.
- Core `src/`: `Framework`, `WebAPI` (+ `WebAssets.h`), `MQTTOut`, `SDLogger`,
  `DisplayManager`, `AlertManager`, `Settings`, `Security`, `BoardInfo`,
  `SerialOut`, the device interfaces (`IDevice` / `IPinDevice` / `IUartDevice`),
  `PpgBeatDetector`, `CmdParse`, `HttpSse`, and the `config/` section headers
  (Config.h is now an ordered include list).
- All 78 `plugins/*.h` device drivers (I2C sensors, stackable modules, GPIO/PWM
  pin devices, UART devices, and the network/LLM clients).
- The Pi-side `scripts/orchestrator.py` (the escalation target that fronts
  Claude Code).

**Method:** full static reading of the core, the network/streaming plugins, the
controllable (command-accepting) plugins, and the orchestrator; representative
reading of the sensor plugins; and whole-tree pattern scans (unsafe string
APIs, blocking calls, buffer sizing, reading-count contracts). No compiler or
hardware was available in this environment, so findings are from static review;
the maintainer reports the firmware builds and flashes cleanly.

**Focus areas:** architecture & maintainability, correctness/robustness,
embedded/ESP32 concerns, and security.

---

## 2. Executive summary

This is a well-engineered codebase. The firmware shows consistent discipline:
strong per-command input validation, careful I2C handling (bus recovery, mux
parking, FIFO watchdogs), a clean board-abstraction that runs one binary across
four boards, and a tidy plugin model that new device classes extend without
touching the core. Whole-tree scans find **no** `strcpy`/`strcat`/`sprintf`/
`atoi`/`gets`; string handling is `snprintf`/bounded-`strncpy`/`ArduinoJson`/
Arduino `String` throughout. No plugin exceeds the 16-slot `SensorVal` reading
contract, FIFO and streaming buffers are bounded, and the cooperative
single-threaded loop avoids data races on device state.

The residual risk is concentrated in two places:

1. **The Pi orchestrator's network service**, which can run Claude Code with
   shell + filesystem access and ships with its access controls defaulting to
   "off / allow-all." This is the highest-severity item.
2. **Secrets compiled into firmware** on a physically accessible device — an
   inherent property of the design, well-documented, but worth stating plainly.

Nothing found is a crash/overflow bug in the firmware's hot paths.

### Severity summary

| # | Severity | Area | Finding |
|---|----------|------|---------|
| H1 | High | Orchestrator | ~~`--serve` defaults to open (no allowlist/bearer ⇒ allow-all)~~ — **fixed** (fail-closed: refuses to start without bearer/allowlist unless `SERVE_ALLOW_INSECURE`) |
| M1 | Medium | Firmware / secrets | Real credentials + TLS private key compiled into a flash image that can be dumped from a desk device |
| M2 | Medium | Web API | ~~No CSRF protection on state-changing `/api/<slug>/set`~~ — **addressed** (POST-only + `X-Requested-With`, `WEB_CSRF_PROTECT`) |
| M3 | Medium | Embedded | Concurrent `WiFiClientSecure` users (HTTPS dashboard + MQTTS + router + Claude API + alert webhooks) can exhaust/fragment heap — **mitigated** (`MIN_TLS_HEAP` pre-connect guard); full fix optional |
| L1 | Low | Streaming | `HttpSseReader`: a non-hex chunk size decodes as 0 → premature end-of-stream (bounded; trusted endpoints) |
| L2 | Low | Reliability | `UartDevice_ModuleLLM::beginUart()` blocks up to ~tens of seconds at boot — **addressed** (optional `WDT_ENABLE` Task Watchdog, registered *after* boot so it can't false-trip) |
| L3 | Low | Correctness | Several plugins carry self-flagged, hardware-unverified register maps (e.g. `Plugin_GOPLUS2`) |
| L4 | Low | Robustness | `AlertManager` rule strings rely on `Rule{}` zero-init for NUL-termination (safe today, fragile to refactor) |
| L5 | Low | Cosmetic | Orphan dashboard comment left in `WebAPI.cpp`; long auto-generated `config/*.h` filenames |

---

## 3. Detailed findings

### H1 — Orchestrator `--serve` is open by default (High)

`scripts/orchestrator.py --serve` exposes an HTTP(S) endpoint that classifies a
prompt and, for "hard" turns, launches the **Claude Code CLI as a subprocess
with shell + filesystem access** to `WORKDIR`. Access is gated by
`CLIENT_ALLOWLIST`, `HOST_ALLOWLIST`, and `SERVE_BEARER` — all of which default
empty, and an empty allowlist means *allow all* (`ip_allowed()` returns `True`).

So a stock `--serve` instance will accept a prompt from any reachable client and
run an agent with real tool access on its behalf — effectively remote code
execution against `WORKDIR` for anyone who can reach the port. It is well
documented ("lock down who can reach it"), and concurrency/stall limits now
bound resource abuse, but the *default posture is permissive*.

**Recommendation:** make `--serve` fail-closed — refuse to start unless at least
one of `SERVE_BEARER` / `CLIENT_ALLOWLIST` is set (or bind to loopback by
default). Good supporting controls already present: the agent is launched via
`subprocess.Popen([...])` (argv list, **no** `shell=True`), so the prompt cannot
inject shell commands; the Anthropic key is read from an env var; and the
allowlist/host/bearer checks, when enabled, are implemented correctly.

**Update — fixed.** `serve_http()` now refuses to start unless `SERVE_BEARER`
or `CLIENT_ALLOWLIST` is set (the Host allowlist doesn't count), printing a
clear reason and exiting. An explicit `SERVE_ALLOW_INSECURE = True` override is
provided for deployments already firewalled to localhost/LAN.

### M1 — Secrets compiled into a dumpable flash image (Medium)

`src/Secrets.h` (git-ignored, good) bakes the dashboard password, optional MQTT
credentials, the optional `CLAUDE_API_KEY`, and a router bearer directly into
the firmware; the HTTPS server's private key is embedded in `src/https_cert.h`
(and per its own comments can also live on the SD card). On a desk device whose
flash can be read back, all of these are recoverable. This is inherent to the
architecture and is clearly called out in the comments, but for the record it is
a real exposure.

**Recommendation:** prefer the "key stays on the Pi" routing (keep
`ROUTER_DIRECT_API` off so no Anthropic key is on the device); scope/rotate any
key that must live in flash; and consider ESP32 flash encryption + secure boot
for deployed units. No code change required — this is a deployment posture note.

### M2 — No CSRF token on control endpoints (Medium)

`GET/POST /api/<slug>/set` changes physical state (relays, servos, power
supply…). Auth is HTTP Basic over TLS, and the CORS origin is now configurable
(`WEB_CORS_ORIGIN`), but there is no anti-CSRF token. A form-encoded POST is a
CORS "simple request" (no preflight), so a malicious page open in a browser that
has cached the device's Basic-Auth credentials could issue control commands. On
a LAN device the blast radius is small, but the endpoints are state-changing.

**Recommendation:** add a CSRF token (e.g. a per-session value the dashboard
echoes in a header that `/set` requires), or require a non-simple content type /
custom header on `/set` so the browser forces a preflight that CORS can reject.

**Update — addressed.** `/api/<slug>/set` is now POST-only and requires the
custom `X-Requested-With` header (registered via `collectHeaders` on both
servers; checked in the shared `_doControl`), gated by `WEB_CSRF_PROTECT`
(default true). A cross-site page can't add that header without a CORS preflight
the server doesn't allow, so forged commands are blocked. The dashboard sends
it automatically; the only behaviour change is that GET bookmarks to `/set` no
longer work.

### M3 — Concurrent TLS clients can pressure the heap (Medium, embedded)

Several `WiFiClientSecure`/TLS users can be live simultaneously: the HTTPS
dashboard server, MQTTS (`MQTTOut`), the escalation router (`NetDevice_Router`),
the direct Claude API client, and the `AlertManager` webhook/email POSTs. Each
carries large mbedTLS buffers. Under a browser pre-connect burst the device can
transiently fail to allocate a new TLS context (the router already retries its
connect with backoff, which is a symptom of this). On the ESP32's limited heap
this is a real pressure point when many TLS paths are enabled at once.

**Recommendation:** document/encourage enabling only the TLS paths a deployment
needs; watch free heap when combining dashboard-HTTPS + MQTTS + router + direct
API; consider serialising outbound TLS (the alert queue already drains one POST
at a time) and reusing a single client where feasible.

**Update — mitigated.** A `MIN_TLS_HEAP` (default 50 KB) pre-connect guard was
added to every outbound TLS opener (`MQTTOut::_tryConnect`,
`NetDevice_Router::_startEscalated`, both Claude API `_start`s, and
`AlertManager::_pumpHttp`): when free heap is below the watermark they
defer/skip gracefully (the alert queue keeps the job; MQTTS retries next cycle)
instead of attempting a handshake that fragments memory and can starve the
dashboard. This makes the failure mode graceful; it does not increase total
capacity. The capacity fix — shrinking mbedTLS record buffers
(`MBEDTLS_SSL_IN/OUT_CONTENT_LEN`) — is a build-config change with cloud-TLS
compatibility risk and is left optional. Capping the HTTPS server's max
connections depends on whether the `ESPWebServerSecure` compat wrapper exposes
the underlying server's `maxConnections` parameter (see note below).

> **Note — can the HTTPS connection cap be set easily? (mitigation #2)**
> _Verdict: unverified, and leans "not easy"; treat as optional._
>
> - The capability exists one layer down: the underlying
>   `esp32_idf5_https_server` `HTTPSServer` constructor takes a
>   `maxConnections` argument (default 4).
> - But this firmware only ever constructs `new ESPWebServerSecure(WEB_HTTPS_PORT)`
>   — **port only, no cap** — so whether it's tunable depends entirely on the
>   `ESPWebServerSecure` **compat wrapper**, which is a third-party library in the
>   Arduino `libraries/` folder, **not in this repo** (so it could not be
>   inspected during this audit).
> - If the wrapper forwards `maxConnections` (a constructor arg or setter), it's a
>   genuine one-line change: `new ESPWebServerSecure(port, 2)` → **Easy**. If it
>   hardcodes the value internally, capping requires editing/forking or
>   subclassing the library → **Medium**, and touches third-party code.
> - Every call site uses only the `(port)` form, which *suggests* the cap isn't
>   surfaced — hence the "leans not-easy" verdict. **To confirm:** open
>   `ESPWebServerSecure.hpp` in your libraries folder and check the constructor /
>   for a `setMaxConnections`-style method.
> - **Recommendation:** with the `MIN_TLS_HEAP` guard already handling the
>   practical failure mode gracefully, the marginal value of a connection cap is
>   low. Pursue it only if the wrapper exposes the setting cheaply; otherwise
>   leave it.

### L1 — SSE chunk-size robustness (Low)

`src/HttpSse.h` de-frames `Transfer-Encoding: chunked` correctly for the
streaming clients. A malformed, non-hex chunk-size line makes
`strtol(..., 16)` return 0, which is treated as the terminating chunk and ends
the stream early. Only reachable from a misbehaving server, and the reader now
caps both internal buffers at 8 KB (`kMaxBuffer`) and abandons a runaway stream,
so there is no unbounded growth. Cosmetic robustness only.

**Recommendation:** optionally distinguish "0" from "garbage" (treat a
non-hex-digit size as an error rather than a clean end). Low priority.

### L2 — Blocking boot path in the Module LLM (Low)

`UartDevice_ModuleLLM::beginPins()/beginUart()` loads the on-board model with
retries and settle waits, blocking for up to tens of seconds at boot. This runs
during `setup()`, not the loop, so it doesn't stall live operation — but on a
build with the task watchdog enabled the long synchronous waits could trip it.

**Recommendation:** if the watchdog is enabled, feed it during the model-load
waits, or cap total boot-time spent here.

**Update — addressed.** An optional Task Watchdog (`WDT_ENABLE` / `WDT_TIMEOUT_S`,
Config.h, default off) was added. It is registered in `setup()` *after*
`fw.begin()` completes and fed once per `loop()` pass, so the long boot-time
model load runs before the watchdog exists and can't trip it; once running, a
genuinely hung `loop()` reboots the device.

### L3 — Unverified hardware register maps (Low, correctness)

At least `Plugin_GOPLUS2` self-flags its register map as "NOT yet hardware
verified," and a few module drivers (e.g. GoPlus2 motor/servo/RGB/encoder
offsets) follow M5Stack conventions without on-silicon confirmation. A wrong map
is a silent failure — the device binds and shows a card but the values/commands
are meaningless.

**Recommendation:** verify each flagged map against real hardware, then drop the
caveat; keep a short list of "verified vs. assumed" drivers.

### L4 — `AlertManager` string NUL-termination depends on zero-init (Low)

In `AlertManager.cpp`, seed rules and `_ruleFromJson()` populate fixed `char`
buffers with `strncpy(dst, src, sizeof(dst) - 1)` and rely on the surrounding
`Rule r{};` / `Rule rad{}` zero-initialisation to guarantee the trailing NUL
(the event-ring copies at lines 204–205 *do* terminate explicitly). This is
correct today, but a future refactor that constructs a `Rule` without
zero-init would leave an unterminated string feeding `snprintf`/JSON.

**Recommendation:** add an explicit `dst[sizeof(dst)-1] = '\0';` after each
`strncpy` (cheap insurance), matching the pattern already used in `_emit()`.

### L5 — Cosmetic (Low)

- `WebAPI.cpp` still carries an orphaned "Embedded HTML dashboard … (PROGMEM)"
  comment block where the markup used to be (it now lives in `WebAssets.h`).
- The `config/*.h` split produced long auto-generated names
  (e.g. `02_standalone_access_point_used_only_when_wifi_ssid_is.h`). Functional,
  but short slugs would read better.

---

## 4. Strengths (verified, worth preserving)

- **Memory-safety hygiene.** No unsafe C string functions anywhere; consistent
  `snprintf`/bounded-`strncpy`/`ArduinoJson`. No `getReadings()` writes past the
  16-element `SensorVal` contract. FIFO reads (MAX30100 64 B, MAX30102 96 B) are
  sized and capped; the SSE reader is now bounded.
- **Input validation.** Every controllable plugin validates each `command()`
  parameter before touching hardware, via the shared `cmd::` validators
  (`CmdParse.h`) — out-of-range / malformed values are rejected, not applied.
- **Concurrency.** The firmware runs a cooperative single-threaded loop
  (`handleClient()` is polled), so there are no data races on plugin/device
  state. Sensor polling is non-blocking — no `delay()` remains in any
  `update()`/`fastPoll()` hot path (trigger/read-next-poll for ULTRASONIC/TVOC,
  a `fastPoll` state machine for ADS1115).
- **I2C robustness.** Boot-time bus recovery (SCL pulsing), PCA9548A mux parking
  before/after scans, per-instance `IRAM_ATTR` ISRs (Geiger, AS3935), FIFO-stall
  watchdogs with bounded recovery (heart sensors), and a deliberate no-periodic-
  rescan policy.
- **Robust I/O channels.** SD logger self-test + cross-boot persistence check +
  close/reopen commit; NTP-optional design with uptime fallback; MQTT with LWT,
  TLS/mutual-TLS options, and HA discovery.
- **Security baseline.** HTTPS + HTTP→HTTPS redirect, optional Basic Auth, a
  default-credential boot guard (`SECURITY_STRICT`), a random per-device AP
  password (now shown once), a hardware factory-reset escape hatch, an optional
  Host-header allowlist, and configurable CORS origin.
- **Architecture.** Runtime board detection yields one binary for
  CoreS3/Core2/Core1/Tough; the `IDevice → IPinDevice → IUartDevice` hierarchy
  lets new device *classes* drop in with no core edits; cross-feature coupling is
  by public `command()`/`toJson()`/slug lookup (e.g. AlertManager → LoRa/Modem,
  Router → LLM/ClaudeAPI). Streaming SSE transport is factored into one
  `HttpSseReader` shared by all three network clients.
- **Dashboard XSS surface is low.** Dynamic text (LLM/LoRa chat) is rendered via
  `textContent`; reading rows go through an `esc()` HTML-escaper; the values that
  are interpolated are device-originated, not attacker-controlled.
- **Orchestrator.** Agent launched with an argv list (no shell injection); API
  key from env; per-request state (no shared-history race); concurrency cap and a
  watchdog deadline on a stalled agent; POST size/`Content-Length` guards.

---

## 5. Prioritised recommendations

1. **(High) — DONE.** Orchestrator `--serve` is now fail-closed (requires a
   bearer or allowlist; `SERVE_ALLOW_INSECURE` to override). [H1]
2. **(Medium)** Treat secrets-in-flash as a deployment decision: keep the
   Anthropic key on the Pi where possible; enable flash encryption / secure boot
   on fielded units. [M1]
3. **(Medium) — DONE.** CSRF protection added to `/api/<slug>/set` (POST-only +
   `X-Requested-With`, `WEB_CSRF_PROTECT`). [M2]
4. **(Medium) — mitigated.** A `MIN_TLS_HEAP` pre-connect guard now defers/skips
   outbound TLS opens when free heap is low, so failures are graceful instead of
   fragmenting/starving the dashboard. The fuller fix (shrink mbedTLS record
   buffers via build config) remains optional and carries cloud-TLS
   compatibility risk. [M3]
5. **(Low)** Verify the flagged register maps on hardware [L3]; add explicit
   NUL-termination after the `AlertManager` `strncpy`s [L4]; treat a non-hex SSE
   chunk size as an error [L1]; tidy the cosmetic items [L5]. _(L2 watchdog: done
   — optional `WDT_ENABLE`.)_

---

## 6. Notes & limitations

- This was a static review; no build, flash, or runtime exercise was performed
  in the audit environment. Dynamic issues (timing, real heap behaviour under
  load, hardware register correctness) are best confirmed on-device.
- The sensor plugins were reviewed representatively plus pattern-scanned across
  all 78; the controllable, network, UART, and core files were read in full.
- Third-party libraries (M5Unified, ArduinoJson, PubSubClient, the ESP-IDF HTTPS
  server, the M5 Module-LLM/PPS libraries) were treated as trusted dependencies
  and not audited.
