# orchestrator.py — usage

Local-first chat router for the M5Stack + Orange Pi rig, with optional
inbound access control. `orchestrator.py` is the live file; the previous
version (no access control) is kept as `orchestrator_original.py` for
reference.

The access-control additions are a strict superset of the original — with
`CLIENT_ALLOWLIST`, `HOST_ALLOWLIST`, and `SERVE_BEARER` all empty, behavior
is identical to `orchestrator_original.py`.

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

## Access control (all optional, all default OFF)

Edit the config block near the top of the file:

| Setting | Effect |
|---|---|
| `CLIENT_ALLOWLIST` | Serve only these source IPs / CIDRs. `[]` = any. **The real teeth.** |
| `HOST_ALLOWLIST` | Serve only requests whose `Host:` matches (port-stripped, case-insensitive). `[]` = any. DNS-rebinding guard. |
| `SERVE_BEARER` | Require `Authorization: Bearer <token>`. Reads `$ROUTER_BEARER`. |
| `TRUST_FORWARDED` | `True` **only** behind a trusted reverse proxy — then the client IP is read from `X-Forwarded-For`. |
| `SERVE_HOST` / `SERVE_PORT` | Bind address / port. |
| `SERVE_TLS_CERT` / `SERVE_TLS_KEY` | Serve HTTPS directly (else plain HTTP for behind nginx/Caddy). |

```python
CLIENT_ALLOWLIST = ["192.168.1.50", "192.168.1.0/24"]   # CoreS3 + LAN
HOST_ALLOWLIST   = ["pi.local"]                          # optional
```

A blocked request gets `403` (IP/Host) or `401` (bearer), with the reason
printed to the console. Empty lists / empty token = that layer is off.

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

## Security note

This box holds the Anthropic key **and** runs Claude Code with filesystem
/ shell access in `WORKDIR`. The IP allowlist + bearer are the meaningful
controls; the Host allowlist is a lightweight rebinding guard on top. For
strong isolation, also restrict at the router/firewall.
