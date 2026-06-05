# Code Audit v2 — M5Stack Core Framework (post-remediation)

_Date: 2026-06-05 · Follow-up to `CODE_AUDIT.md`_

This is a fresh, comprehensive pass over the **current** state of the tree —
after the first audit's fixes landed (see `CHANGELOG.md` → *Unreleased*). It
re-verifies the remediation, critically reviews the code added during it, and
widens scope to the Pi-side **`scripts/orchestrator.py`**, which the first audit
(firmware-only) did not cover.

Method: full read of the changed/added files (`src/HttpSse.h`, `src/CmdParse.h`,
the templated WebAPI dispatch, the reworked `Plugin_ADS1115` / `Plugin_TVOC` /
`Plugin_ULTRASONIC`, `UartDevice_Modem`, `Security.h`, the `src/config/` split),
the orchestrator in full, plus whole-tree pattern scans. Compiles and flashes
cleanly per the maintainer.

## Overall assessment

The firmware was already strong; the remediation improved it without regressions
I can find. The previously-flagged real bug (AlertManager out-of-bounds enum
read) is fixed, the worst duplication (WebAPI route table, per-plugin parsers) is
collapsed to single sources of truth, the streaming-token-loss bug is fixed, and
no sensor blocks the cooperative loop during a poll anymore. Whole-tree scans
still find **no** `strcpy`/`strcat`/`sprintf`/`atoi`/`gets`, no `getReadings`
overruns the 16-slot contract, and FIFO buffers remain correctly sized.

The notable shift in risk is **off the device**: the orchestrator that fronts
Claude Code is where the highest-severity item now lives.

## A. Pi orchestrator — `scripts/orchestrator.py`

This service classifies a turn and, for "hard" turns, runs the **Claude Code
CLI as a subprocess with filesystem + shell access** to `WORKDIR`. When exposed
with `--serve`, it is the endpoint the device escalates to.

- **HIGH — `--serve` is open by default.** `CLIENT_ALLOWLIST`, `HOST_ALLOWLIST`,
  and `SERVE_BEARER` all default to empty/off, and an empty allowlist means
  *allow all* (`ip_allowed()` returns `True`). So a `--serve` instance with the
  shipped config accepts a prompt from **any** reachable client and runs Claude
  Code (shell, filesystem, the repo) on its behalf — effectively remote code
  execution against `WORKDIR`. It is well-documented ("lock down who can reach
  it"), but the *default posture is permissive*. Recommend: refuse to start
  `--serve` unless at least one of bearer/allowlist is set (fail-closed), or
  default `SERVE_BEARER`/`CLIENT_ALLOWLIST` to something non-empty.
- **MEDIUM — shared mutable state under `ThreadingHTTPServer`.** One
  `Orchestrator` instance is shared across handler threads, and every request
  mutates `orch.history` (`.append(...)`) with no lock — a data race that can
  corrupt the history list / interleave conversations under concurrent requests.
  Either make the server single-threaded, instantiate per-request, or guard
  `history` with a lock.
- **MEDIUM — unbounded concurrent agents.** `ThreadingHTTPServer` will spawn a
  Claude Code subprocess per concurrent escalation, with no cap — a handful of
  requests can exhaust CPU/RAM on the Pi. Add a concurrency semaphore.
- **MEDIUM — `CLAUDE_TIMEOUT` doesn't bound a stalled agent.** `run_claude()`
  iterates `proc.stdout` line-by-line and only calls `proc.wait(timeout=...)`
  *after* stdout reaches EOF. A Claude Code process that stalls while keeping
  stdout open blocks the read loop (and its thread) indefinitely; the timeout
  never fires. Enforce a wall-clock deadline around the read loop, or use a
  watchdog thread that kills `proc`.
- **LOW — POST body is read without a size cap.** `do_POST` reads
  `Content-Length` bytes into memory unbounded (memory DoS), and a non-numeric
  `Content-Length` raises an uncaught `ValueError` → 500. Clamp the length and
  guard the `int(...)`.
- **LOW — bearer compared non-constant-time.** Token check uses `!=`; a timing
  side-channel is negligible here but `hmac.compare_digest` is free to adopt.
- **GOOD.** Claude Code is launched via `subprocess.Popen([...])` with an
  argument list (no `shell=True`), so the prompt cannot inject *shell* commands;
  the API key is read from `ANTHROPIC_API_KEY` env (not hard-coded); the direct
  Anthropic call uses `requests`, which de-chunks SSE correctly server-side; and
  the allowlist/host/bearer guards, when enabled, are implemented correctly.

## B. Firmware — review of the remediation code

All verified correct against the current files; residual nits noted.

- **`HttpSseReader` (`src/HttpSse.h`) — correct, with two low residuals.** The
  header-skip + chunk de-framing is right, and it fixed the dropped-token bug.
  Residuals: (1) the internal `_raw`/`_body` `String`s have **no hard cap** (the
  old per-line parser capped at 2048 chars); a non-terminating or flooding chunk
  could grow heap until the plugin's inactivity timeout aborts the turn — fine
  for the trusted Anthropic/Pi endpoints plus the idle timeout, but worth a
  ceiling if ever pointed at an untrusted server. (2) A malformed/non-hex chunk
  size makes `strtol(...,16)` return 0, which is treated as the terminating
  chunk and ends the stream early; only reachable from a misbehaving server.
- **WebAPI templated dispatch — correct.** `_routeDynamic` / `_doControl` /
  `_doSettingsSave` / `_buildSettingsDoc` run only after auth in both servers;
  template instantiation ordering is sound (compiles). Minor: the dynamic JSON
  responses are now compact rather than pretty (harmless), and an orphaned
  "Embedded HTML dashboard … (PROGMEM)" comment block remains near the top of
  `WebAPI.cpp` describing content that now lives in `WebAssets.h` (cosmetic).
- **`Plugin_ADS1115` fastPoll state machine — correct.** Non-blocking
  per-channel conversion; the mux channel is reselected by the framework before
  each `fastPoll`, so start-conv and read-conv both run on the right channel.
  Bus errors skip a channel and keep the prior value. Full sweep every `POLL_MS`.
- **`Plugin_ULTRASONIC` / `Plugin_TVOC` trigger/read split — correct.** Reading
  the prior poll's result is valid (the ≥500 ms gap dwarfs the sensors' 50/12 ms
  conversion); one extra poll of startup latency, refresh rate unchanged.
- **`UartDevice_Modem` SMS — correct.** Number restricted to `+`/digits, body
  stripped of CR/LF/Ctrl-Z, empty-after-clean rejected; AT-command injection
  closed.
- **`CmdParse.h` — correct; one behavioral note.** `parseInt`/`parseFloat` now
  accept a leading sign where a couple of the old per-plugin copies did not.
  Because every caller passes a non-negative `lo`, signed input is rejected by
  the range check anyway, so behavior is unchanged in practice — just be aware
  the helpers are intentionally more permissive than some of the originals.
- **`Config.h` split — equivalent.** The 26 `src/config/*.h` includes
  concatenate in original order; `24_credentials.h` correctly uses
  `#include "../Secrets.h"`, and the `#if MQTT_TLS` cert block stays self-balanced
  and after its macro defs. Nit: the auto-generated section filenames are long
  (e.g. `02_standalone_access_point_used_only_when_wifi_ssid_is.h`); renaming to
  short slugs would be friendlier but is cosmetic.

## C. Still-open / pre-existing (carried forward)

- **Unverified hardware register maps.** `Plugin_GOPLUS2` self-flags its map as
  not hardware-verified; spot-check it (and any siblings) on real silicon, then
  drop the caveat. (Design risk: a wrong map = silent no-op / wrong output.)
- **No CSRF token on `/set`.** CORS origin is now configurable
  (`WEB_CORS_ORIGIN`), but a control endpoint still has no anti-CSRF token; a
  same-origin or credential-cached browser context could be abused. Low on a
  Basic-Auth LAN device.
- **TLS heap pressure on the ESP32.** Several `WiFiClientSecure` users can be
  live at once (dashboard HTTPS server + MQTTS + Router + ClaudeAPI + the
  AlertManager webhook/email POSTs). Each carries large mbedTLS buffers; under a
  browser pre-connect burst, `connect()` can transiently fail (the framework
  already retries the router connect). Worth a free-heap watch when enabling many
  TLS paths together.
- **Boot-time blocking in `UartDevice_ModuleLLM::beginUart()`** (model load /
  retries, up to ~tens of seconds). Pre-existing and only at boot, but on a build
  with the task watchdog enabled it could be worth feeding the WDT during the
  long waits.
- **Heap/`String` churn** over multi-week uptimes — monitoring item, not a defect.

## Prioritized actions

1. **(High) — DONE.** Orchestrator `--serve` is now fail-closed: it refuses to
   start with no bearer *and* no allowlist (`SERVE_ALLOW_INSECURE` overrides).
2. **(Med) — DONE.** Orchestrator: each request now gets its own `Orchestrator`
   (no shared-`history` race under `ThreadingHTTPServer`); a
   `BoundedSemaphore(MAX_CONCURRENT_AGENTS)` caps concurrent Claude Code
   subprocesses (returns a "busy" note past the cap); and a `threading.Timer`
   watchdog kills a stalled agent at `CLAUDE_TIMEOUT` (the old `proc.wait()`
   timeout only applied after stdout EOF, which a hung child never reaches).
3. **(Med) — DONE.** Orchestrator `do_POST` guards the `Content-Length` parse
   against non-numeric values and rejects bodies over `MAX_POST_BYTES` (413).
4. **(Low) — DONE.** `HttpSseReader` now abandons the stream (marks it complete,
   frees `_raw`) if either buffer exceeds `kMaxBuffer` (8 KB) — a safety valve
   against a malformed/non-terminating chunk, line, or header block.
5. **(Low)** Verify the GoPlus2 register map on hardware. _(open)_
6. **(Low)** CSRF for `/set` — **DONE** (POST-only + `X-Requested-With`,
   `WEB_CSRF_PROTECT`; a custom-header guard rather than a token, equally
   effective here). Still open: tidy the orphan WebAPI comment and the long
   config filenames.

_Actions 2–4 implemented in this pass (see `CHANGELOG.md`). No compiler/runtime
was available in the audit environment; the Python generator/watchdog patterns
were syntax-checked separately, and the firmware builds per the maintainer — but
exercise `--serve` and a streamed reply to confirm end-to-end._
