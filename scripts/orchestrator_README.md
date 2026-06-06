# orchestrator.py — usage

Local-first chat router for the M5Stack + Orange Pi rig, with optional
inbound access control. `orchestrator.py` is the live file; the previous
version (no access control) is kept as `orchestrator_original.py` for
reference.

The access-control additions are a strict superset of the original — with
`CLIENT_ALLOWLIST`, `HOST_ALLOWLIST`, and `SERVE_BEARER` all empty, the
*routing* behavior is identical to `orchestrator_original.py`. On top of that,
a few **resilience limits** now apply regardless of access control: a cap on
concurrent Claude Code agents, a hard wall-clock deadline on a stalled agent,
and a maximum `--serve` request size (see *Operational limits* below).

> Only `requests` is third-party (`pip install requests`); everything
> the allowlist/server uses is the Python stdlib.

## Running

```bash
# interactive terminal chat (unchanged)
python orchestrator.py

# as the inbound service the M5Stack router escalates to
python orchestrator.py --serve
```

`--serve` accepts `POST <prompt>` (raw text, or JSON `{"prompt":"..."}`)
and streams a `text/event-stream` back — `data: {"delta":"..."}` …
`data: {"finish":true}` … `data: [DONE]` — the exact shape
`NetDevice_Router` on the CoreS3 parses.

## Access control (fail-closed)

`--serve` exposes an endpoint that can run Claude Code with shell + filesystem
access, so it **refuses to start** unless at least one real access control is
set: a bearer token (`SERVE_BEARER`) and/or a source-IP allowlist
(`CLIENT_ALLOWLIST`). The Host allowlist is only a rebinding guard and does
**not** satisfy this. To run genuinely open anyway — e.g. it's already
firewalled to localhost or a trusted LAN — set `SERVE_ALLOW_INSECURE = True`.

Edit the config block near the top of the file:

| Setting | Effect |
|---|---|
| `CLIENT_ALLOWLIST` | Serve only these source IPs / CIDRs. `[]` = any. **The real teeth.** |
| `SERVE_BEARER` | Require `Authorization: Bearer <token>`. Reads `$ROUTER_BEARER`. |
| `SERVE_ALLOW_INSECURE` | `True` to allow starting with no bearer **and** no IP allowlist (you accept the risk / it's firewalled). Default `False`. |
| `HOST_ALLOWLIST` | Serve only requests whose `Host:` matches (port-stripped, case-insensitive). `[]` = any. DNS-rebinding guard (does not count as access control). |
| `TRUST_FORWARDED` | `True` **only** behind a trusted reverse proxy — then the client IP is read from `X-Forwarded-For`. |
| `SERVE_HOST` / `SERVE_PORT` | Bind address / port. |
| `SERVE_TLS_CERT` / `SERVE_TLS_KEY` | Serve HTTPS directly (else plain HTTP for behind nginx/Caddy). |

```python
CLIENT_ALLOWLIST = ["192.168.1.50", "192.168.1.0/24"]   # CoreS3 + LAN
HOST_ALLOWLIST   = ["pi.local"]                          # optional
```

A blocked request gets `403` (IP/Host) or `401` (bearer), with the reason
printed to the console. Empty lists / empty token = that layer is off. A body
larger than `MAX_POST_BYTES` is rejected with `413`, and a malformed
`Content-Length` with `400`.

## Operational limits

These bound resource use and apply on every `--serve` request regardless of
the access-control settings above:

| Setting | Default | Effect |
|---|---|---|
| `MAX_CONCURRENT_AGENTS` | `2` | Max Claude Code subprocesses running at once. A request past the cap waits up to `AGENT_ACQUIRE_TIMEOUT` for a slot, then streams a `[orchestrator busy …]` reply instead of piling on. |
| `AGENT_ACQUIRE_TIMEOUT` | `5` s | How long an escalation waits for a free agent slot before returning "busy". |
| `CLAUDE_TIMEOUT` | `1800` s | Hard wall-clock deadline for one escalation. A watchdog kills the agent at this point even if it has stalled with stdout open, and the reply ends with `[claude timed out]`. |
| `MAX_POST_BYTES` | `65536` | Largest accepted `--serve` request body; larger bodies get `413`. |

Each `--serve` request also runs with its **own** `Orchestrator` instance, so
the conversation `history` is per-request — no shared state races across the
threaded server, and no context bleed between unrelated clients.

## Matching the firmware

In `Config.h` on the CoreS3 these line up:

| Firmware | Orchestrator |
|---|---|
| `ROUTER_PI_HOST` | must be in `HOST_ALLOWLIST` (and resolves to / is the bind IP) |
| `ROUTER_PI_PORT` | `SERVE_PORT` |
| `ROUTER_PI_PATH` | any path — the server serves all paths via POST |
| `ROUTER_BEARER` | `SERVE_BEARER` (or `$ROUTER_BEARER`) |
| `ROUTER_TLS_INSECURE = true` | set `SERVE_TLS_CERT`/`KEY` to a self-signed pair |

The router connects with `WiFiClientSecure`, so for a direct connection
the service must be **HTTPS** — either set the TLS cert/key here, or put
nginx/Caddy in front (then leave TLS off and use `TRUST_FORWARDED=True`).

Self-signed cert for direct TLS:

```bash
openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
  -keyout pi.key -out pi.crt -subj "/CN=pi.local"
```

## Reuse without `--serve`

If you already have a Flask/aiohttp front-end, skip `--serve` and call the
two helpers in your handler — reject the request when either returns
`False`:

```python
from orchestrator import ip_allowed, host_allowed, Orchestrator
if not ip_allowed(request.remote_addr): abort(403)
if not host_allowed(request.host):      abort(403)
```

Construct a fresh `Orchestrator()` per request (its `history` isn't
thread-safe to share). The concurrent-agent cap lives in `run_claude` via a
module-level semaphore, so it applies even through your own front-end.

## Security note

This box holds the Anthropic key **and** runs Claude Code with filesystem
/ shell access in `WORKDIR`. The IP allowlist + bearer are the meaningful
controls; the Host allowlist is a lightweight rebinding guard on top. For
strong isolation, also restrict at the router/firewall.
