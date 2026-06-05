# Code Audit — M5Stack Core Framework

_Date: 2026-06-05_

Scope: every core `src/` file plus the full plugin set (I2C sensors, stackable
output modules, GPIO/PWM pin devices, UART devices, and the network/LLM
clients), with a whole-tree grep scan for unsafe patterns. Focus areas:
architecture & design, code quality & bugs, and embedded/ESP32 concerns.
(Security was reviewed only incidentally — it was out of the requested scope.)

## Summary

This is an unusually well-engineered embedded codebase. Validation discipline is
excellent (no `strcpy`/`strcat`/`sprintf`/`atoi`/`gets` anywhere — consistent
`snprintf`/`strncpy`+size-1/ArduinoJson), no plugin overruns the 16-slot
`SensorVal` contract, FIFO read buffers are correctly sized and capped, and the
embedded fundamentals (I2C bus recovery, mux parking, IRAM ISRs with
per-instance args, FIFO stall watchdogs, SD self-test + close/reopen commit,
NTP-optional design) are handled thoughtfully. The findings below are
refinements, not alarms.

## Architecture & design

Strengths worth keeping: the `IDevice → IPinDevice → IUartDevice` hierarchy lets
new device *classes* (network clients, LLM) slot in with zero framework edits;
`BoardInfo` gives one binary across CoreS3/Core2/Core1/Tough; and the
Router/AlertManager reach other plugins purely through public
`command()`/`toJson()`/slug lookup — genuinely clean decoupling.

- **`WebAPI.cpp` is doing too much (~2,660 lines).** The dashboard HTML/CSS/JS is
  embedded as PROGMEM, and the route logic exists in three parallel copies: the
  HTTPS `begin()` route table, the `onNotFound()` fallthrough, and the
  `_wirePlainAp()` template mirror. `/api/settings/save`, `/set` control, and the
  alerts routes are each implemented 2–3 times — the code itself says "mirror it
  here." This is the single biggest drift risk in the project. Extract shared
  handlers parameterized on the server type (the `_setupField`/`serveSetupSubmit`
  templates already establish the pattern).
- **~10 controllable plugins each re-implement `_parseBool`/`_parseInt`/
  `_parseFloat`/`_allDigits`/`_parseNibble`/`_hexNibble`.** Identical copies.
  Promote them to shared helpers (a `CmdParse` util or `protected` statics on
  `IDevice`).
- **A few plugins ship self-flagged unverified register maps** — `Plugin_GOPLUS2`
  explicitly says "REGISTER MAP NOT YET HARDWARE-VERIFIED." Fine while that
  caveat stays loud; worth a tracking list.
- `Config.h` is an ~820-line god-header (build switches + tunables for every
  subsystem + secret indirection). It works and is heavily documented, but it's a
  lot of surface in one file.

## Code quality & bugs

- **AlertManager: enum fields from JSON rules aren't range-checked (medium).**
  `_ruleFromJson()` casts `sev`, `op`, `kind`, `gop` straight from JSON with no
  clamp. A rule with `sev > 2` (via authenticated `POST /api/alerts/rules/save`,
  or a corrupt NVS blob) causes out-of-bounds reads on the 3-element
  `SV[]`/`TYT[]`/`SVT[]` arrays in `_emit`/`_route`/`toJson`. Clamp
  severity/op/kind to their valid ranges in `_ruleFromJson`.
- **SSE readers don't de-frame HTTP chunked transfer-encoding (low–medium) —
  FIXED.** `NetDevice_ClaudeAPI`, `NetDevice_ClaudeAPI_History`, and
  `NetDevice_Router` used to parse the raw socket line-by-line. Anthropic's API
  (and many servers) reply `Transfer-Encoding: chunked`; a chunk boundary could
  split a `data:` line mid-JSON → `deserializeJson` fails and that token is
  silently dropped. A shared `src/HttpSse.h` (`HttpSseReader`) now skips the
  response headers and de-frames chunked encoding, handing each plugin clean,
  complete body lines; it also exposes `complete()` (terminal-chunk seen) as a
  clean end-of-stream signal. One copy of the transport logic, shared by all
  three clients.
- **Modem SMS is built by string injection (low).** `command("sms", ...)` puts
  the number/text into `AT+CMGS="<num>"` with only `trim()`+length checks — a CR
  or `"` in the number could inject AT commands. Restrict the number to `[+0-9]`.
- **`Security::effectiveApPassword` sets `generated=true` on every boot**, even
  when reusing the stored random password — so `_startAccessPoint()` re-displays
  the AP password on the LCD/serial for 8 s at every power-up. Intentional per
  the comment, but a persisted secret shown on each boot is a minor exposure/UX
  wart.
- **CORS `Access-Control-Allow-Origin: *` on all endpoints, including `/set`
  (low).** A form-urlencoded POST is a CORS "simple request" (no preflight), so a
  malicious page could fire control commands if auth is disabled or creds are
  cached — there's no CSRF token. Limited on a Basic-Auth LAN device, and outside
  the requested focus, but noted.
- **Positives:** no unsafe C string functions anywhere; consistent `snprintf`/
  `strncpy` with size-1 and zero-init; no `getReadings` exceeds the 16-slot
  `SensorVal` contract; FIFO read buffers (MAX30100 64 B / MAX30102 96 B) are
  correctly sized and capped.

## Embedded / ESP32 concerns

- **Blocking `delay()` inside `update()` stalls the cooperative loop
  (low–medium).** Everything (web server, MQTT, display, SD, alerts) runs in
  `loop()`, so a sensor that blocks during its poll freezes all of it:
  `Plugin_ULTRASONIC` `delay(50)`, `Plugin_TVOC` `delay(12)`, `Plugin_ADS1115`
  `delay(9)`. Bounded to once per `POLL_MS` (500 ms), so it's a periodic
  9–50 ms hiccup, not a hang — but a trigger-now / read-next-poll split would
  remove it. (All other `delay()` calls are in `begin()`/config, which is fine.)
- **DisplayManager overview (ticker) repaints the scroll band + footer every
  `loop()` iteration** with no `POLL_MS` throttle (the detail view *is*
  throttled). Continuous SPI traffic on the bus shared with the SD card. Minor,
  but the asymmetry looks unintentional.
- **Heavy Arduino `String` use**, including concatenation in hot-ish paths
  (`toJson` building `String("ch")+i`, request handling, JSON-to-`String`). The
  code is careful (`reserve()`, `measureJson`, bounded `ANSWER_MAX` char
  buffers), so it's not a defect — but it's the most likely source of slow heap
  fragmentation over very long uptimes. Worth a free-heap watch on multi-week
  deployments.
- `AlertManager::_httpPost` blocks up to 8 s, but it's serialized one-per-loop
  and infrequent, and the trade-off is documented — acceptable.

## Remediation status

Every finding that was acted on is recorded in **`CHANGELOG.md`** (under
*Unreleased*) — that's the change history; this file is the findings report.
In short: the AlertManager OOB clamp, the WebAPI route-table de-duplication,
the shared command-parse helpers, the blocking-`delay()` sensor fixes, the SSE
chunked de-framing, the Modem SMS hardening, the optional AP-password and CORS
flags, the display-repaint throttle, the dashboard-HTML extraction, and the
Config.h split are all done.

Still open (also tracked at the bottom of `CHANGELOG.md`):

- Verify the unverified register maps on hardware (e.g. `Plugin_GOPLUS2`).
- A CSRF token for `/set` (CORS origin is now configurable, but no token).
- Heap/`String` watch on multi-week uptimes (monitoring, not a code change).
