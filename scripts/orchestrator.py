#!/usr/bin/env python3
"""
orchestrator.py  -  Local-first chat router for the M5Stack + Orange Pi rig.

The "router + voice" tier (escalation-ladder doc, SS00-05) as an EXTERNAL
service instead of an on-device plugin. It is the single chat entry point:

  - classifies each turn on the spot,
  - answers trivial ones from the CoreS3's on-board Module LLM
    (the /api/llm endpoint that UartDevice_ModuleLLM.h already exposes),
  - escalates hard ones to Claude Code, streaming the reply back.

The CoreS3 needs NO new firmware for this - flash the framework as-is and
the Module LLM is already reachable at https://<core>/api/llm.

Runs as a terminal chat loop (this file) or import `Orchestrator` into a
web/API front-end. Plain Python; only `requests` is third-party.

    pip install requests
    python orchestrator.py            # interactive terminal chat
    python orchestrator.py --serve    # inbound HTTP(S) service for the router

Optional inbound access control (all default OFF) lives in the config
block below: CLIENT_ALLOWLIST (source IP/CIDR), HOST_ALLOWLIST (Host
header), and SERVE_BEARER (shared token).  See orchestrator_README.md.
"""
from __future__ import annotations
import json
import re
import subprocess
import os
import sys
import time
import ipaddress
import threading
from dataclasses import dataclass, field

import requests
import urllib3

# ----------------------------------------------------------------------
#  Config  -  edit these (or lift them into env vars / a .toml)
# ----------------------------------------------------------------------
CORE_URL      = "https://cores3.local"   # the M5Stack framework's web API
CORE_AUTH     = None                     # ("admin", "pass") if WEB_AUTH_* set, else None
CORE_VERIFY   = False                    # self-signed cert on the LAN -> False
CORE_IDLE_S   = 60                       # give up on a Module LLM reply after this silence

CLAUDE_BIN    = "claude"                 # the Claude Code CLI
WORKDIR       = "/srv/acme-api"          # repo Claude Code operates in
CLAUDE_TIMEOUT = 1800                    # hard ceiling on one escalation (s)
MAX_CONCURRENT_AGENTS = 2                # cap simultaneous Claude Code subprocesses
AGENT_ACQUIRE_TIMEOUT = 5                # s to wait for a free slot before "busy"

# ── Direct Claude API: the "preferred" 3rd route ──────────────────────
#  In the key-stays-on-the-Pi architecture the ORCHESTRATOR owns all
#  cloud calls, so the direct-API middle route lives HERE, not on the
#  Core (the Core then holds no key — ROUTER_DIRECT_API stays false in
#  the firmware Config.h).  Set DIRECT_API_ENABLED true and drop the key
#  in ANTHROPIC_API_KEY (prefer an env var — see below) to turn it on.
#  Leave it false and "smart text" simply falls through to the local
#  Core model, exactly as before.
DIRECT_API_ENABLED = True
ANTHROPIC_API_KEY  = os.environ.get("ANTHROPIC_API_KEY", "")   # ⚠ keep out of source
ANTHROPIC_MODEL    = "claude-haiku-4-5"  # cheap + fast for no-repo text answers
ANTHROPIC_VERSION  = "2023-06-01"
ANTHROPIC_MAXTOK   = 1024
ANTHROPIC_URL      = "https://api.anthropic.com/v1/messages"

HISTORY_TURNS = 6                        # how much chat to fold into a brief
# A turn that needs the AGENT (filesystem / shell / repo) → Claude Code.
ESCALATE_RE   = re.compile(
    r"\b(refactor|debug|test|implement|rewrite|fix|build|run|compile|"
    r"grep|commit|deploy|stack ?trace|exception|migrate)\b", re.I)
CODE_EXT_RE   = re.compile(r"\.(py|ts|tsx|js|jsx|go|rs|java|c|cpp|h|sql|sh|ya?ml)\b", re.I)
# A non-coding turn too rich for the 0.5B but needing no repo → direct API.
# Mirrors ROUTER_DIRECT_KEYWORDS / ROUTER_DIRECT_MIN_WORDS in Config.h.
DIRECT_RE     = re.compile(
    r"\b(explain|summari[sz]e|draft|translate|rewrite|brainstorm|compare|"
    r"outline|reword|paraphrase|why|how|what if)\b", re.I)
DIRECT_MIN_WORDS = 12

if not CORE_VERIFY:
    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

# ----------------------------------------------------------------------
#  Inbound access control  -  for when the orchestrator is EXPOSED as an
#  HTTP service (the endpoint the M5Stack router escalates to).
# ----------------------------------------------------------------------
#  This box owns the Anthropic key AND runs Claude Code with filesystem +
#  shell access, so lock down who can reach it.  Unlike the ESP32 side, a
#  real Linux server sees the client's source IP, so this is a true
#  source-IP allowlist.  Entries are single IPs or CIDR ranges; an EMPTY
#  list disables the check (allow all).
CLIENT_ALLOWLIST: list[str] = [
    # "192.168.1.50",       # the CoreS3
    # "192.168.1.0/24",     # the whole LAN
]
HOST_ALLOWLIST: list[str] = [
    # "pi.local",
    # "192.168.1.10",
]
# ^ Optional Host-header allowlist (parity with the firmware's
#   WEB_HOST_ALLOWLIST).  When non-empty, a request is served only if its
#   Host header (port stripped, case-insensitive) matches an entry.  Guards
#   DNS-rebinding / access via an unexpected hostname.  []/empty = off.
TRUST_FORWARDED = False     # True ONLY behind a trusted reverse proxy
                            # (then the client IP is read from X-Forwarded-For)
SERVE_HOST   = "0.0.0.0"    # bind address for `--serve`
SERVE_PORT   = 8080
MAX_POST_BYTES = 64 * 1024  # reject a `--serve` request body larger than this
# Optional shared secret: if set, a request must carry
#   Authorization: Bearer <token>   (match the firmware's ROUTER_BEARER).
SERVE_BEARER = os.environ.get("ROUTER_BEARER", "")
# Optional TLS for `--serve`, so the firmware's WiFiClientSecure connects
# directly (it skips cert validation when ROUTER_TLS_INSECURE=true).  Point
# these at a cert/key pair, or leave "" for plain HTTP behind nginx/Caddy.
#   openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
#     -keyout pi.key -out pi.crt -subj "/CN=pi.local"
SERVE_TLS_CERT = ""
SERVE_TLS_KEY  = ""
# Fail-closed guard: `--serve` exposes an endpoint that can run Claude Code
# with shell + filesystem access, so it REFUSES TO START unless at least one
# real access control is set — a bearer token (SERVE_BEARER) and/or a source-IP
# allowlist (CLIENT_ALLOWLIST).  (HOST_ALLOWLIST is only a DNS-rebinding guard
# and does NOT count.)  If you genuinely want an open endpoint — e.g. it is
# already firewalled to localhost or a trusted LAN — set this true to override.
SERVE_ALLOW_INSECURE = False


# ----------------------------------------------------------------------
#  Tier 1  -  the CoreS3 Module LLM, over its existing HTTP API
# ----------------------------------------------------------------------
class CoreClient:
    """Talks to UartDevice_ModuleLLM via /api/llm. Fire-and-poll, async."""

    def __init__(self, base=CORE_URL, auth=CORE_AUTH, verify=CORE_VERIFY):
        self.base, self.auth, self.verify = base, auth, verify

    def _get(self, path, **params):
        return requests.get(f"{self.base}{path}", params=params or None,
                            auth=self.auth, verify=self.verify, timeout=8).json()

    def ask(self, prompt: str, idle=CORE_IDLE_S, retries=2) -> str:
        """Submit a prompt and poll until the streamed reply is done."""
        for attempt in range(retries + 1):
            try:
                self._get("/api/llm/set", ask=prompt)       # returns immediately
                last_change, prev = time.time(), ""
                while time.time() - last_change < idle:
                    s = self._get("/api/llm")
                    if s.get("answer", "") != prev:          # tokens still arriving
                        prev, last_change = s["answer"], time.time()
                    if s.get("done") or s.get("timed_out"):
                        return s.get("answer", "").strip() or "[empty reply]"
                    time.sleep(0.3)
                return prev.strip() or "[core went silent]"
            except requests.RequestException as e:
                if attempt == retries:
                    return f"[core unreachable: {e}]"
                time.sleep(1.0 * (attempt + 1))


# ----------------------------------------------------------------------
#  Tier 3  -  Claude Code, streamed
# ----------------------------------------------------------------------
def build_brief(goal: str, summary: str, workdir: str) -> str:
    """Render the SS03 handoff brief as plain-English markdown."""
    parts = [f"# Task\n{goal}\n", f"# Working directory\n{workdir}\n"]
    if summary:
        parts.append(f"# Context from the chat so far\n{summary}\n")
    parts.append("# Output\nWhen done, print a one-paragraph summary "
                 "followed by the unified diff.")
    return "\n".join(parts)


# Cap how many Claude Code subprocesses can run at once.  ThreadingHTTPServer
# would otherwise spawn one agent per concurrent escalation and exhaust the Pi.
_AGENT_SEM = threading.BoundedSemaphore(MAX_CONCURRENT_AGENTS)


def _kill(proc, flag):
    """Watchdog target: flag the timeout and kill the process."""
    flag.set()
    try:
        proc.kill()
    except Exception:
        pass


def run_claude(brief: str, workdir=WORKDIR):
    """Yield (kind, text) events from Claude Code as it works.

    kind is 'progress' (a paraphrasable tool step) or 'final' (assistant
    text). Parses --output-format stream-json; falls back to raw lines.

    Concurrency is capped by _AGENT_SEM (returns a 'busy' note rather than
    pile up agents), and a watchdog kills the process at CLAUDE_TIMEOUT so a
    stalled agent — one that keeps stdout open but stops emitting — can't pin
    the reading thread forever (the old proc.wait() timeout only applied
    AFTER stdout reached EOF, which a hung child never does).
    """
    if not _AGENT_SEM.acquire(timeout=AGENT_ACQUIRE_TIMEOUT):
        yield ("final", "[orchestrator busy — too many concurrent agents, retry shortly]")
        return
    proc = None
    timed_out = threading.Event()
    watchdog = None
    try:
        proc = subprocess.Popen(
            [CLAUDE_BIN, "-p", brief, "--output-format", "stream-json", "--verbose"],
            cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        watchdog = threading.Timer(CLAUDE_TIMEOUT, _kill, args=(proc, timed_out))
        watchdog.daemon = True
        watchdog.start()
        for line in proc.stdout:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                yield ("final", line)            # not JSON -> pass through
                continue
            t = ev.get("type", "")
            if t == "tool_use" or ev.get("name"):
                name = ev.get("name", "tool")
                tgt = ev.get("path") or ev.get("pattern") or ev.get("cmd") or ""
                yield ("progress", f"{name} {tgt}".strip())
            elif t in ("assistant", "assistant_text", "text"):
                txt = ev.get("text") or ev.get("content") or ""
                if txt:
                    yield ("final", txt)
        if timed_out.is_set():
            yield ("final", "[claude timed out]")
    finally:
        if watchdog:
            watchdog.cancel()
        if proc:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                try:
                    proc.kill()
                except Exception:
                    pass
            if proc.stderr:
                err = proc.stderr.read().strip()
                if err and proc.returncode not in (0, None) and not timed_out.is_set():
                    yield ("final", f"[claude error] {err}")
        _AGENT_SEM.release()


# ----------------------------------------------------------------------
#  Tier 2 (middle)  -  the direct Anthropic Messages API, streamed
# ----------------------------------------------------------------------
def call_claude_api(prompt: str, summary: str = ""):
    """Yield ('final', text) deltas from the Claude *model* (no tools).

    The middle route: a strong worded answer for a turn that needs no
    repo, without booting the full Claude Code agent.
    """
    if not ANTHROPIC_API_KEY:
        yield ("final", "[direct API key not set on the Pi]")
        return
    msgs = []
    if summary:
        msgs.append({"role": "user",
                     "content": f"Context from the chat so far:\n{summary}"})
        msgs.append({"role": "assistant", "content": "Understood."})
    msgs.append({"role": "user", "content": prompt})
    body = {"model": ANTHROPIC_MODEL, "max_tokens": ANTHROPIC_MAXTOK,
            "stream": True, "messages": msgs}
    headers = {"x-api-key": ANTHROPIC_API_KEY,
               "anthropic-version": ANTHROPIC_VERSION,
               "content-type": "application/json"}
    try:
        with requests.post(ANTHROPIC_URL, json=body, headers=headers,
                           stream=True, timeout=60) as r:
            if r.status_code != 200:
                yield ("final", f"[api error {r.status_code}] {r.text[:200]}")
                return
            for line in r.iter_lines(decode_unicode=True):
                if not line or not line.startswith("data:"):
                    continue
                payload = line[5:].strip()
                if payload in ("", "[DONE]"):
                    continue
                try:
                    ev = json.loads(payload)
                except json.JSONDecodeError:
                    continue
                if ev.get("type") == "content_block_delta":
                    t = ev.get("delta", {}).get("text", "")
                    if t:
                        yield ("final", t)
                elif ev.get("type") == "error":
                    yield ("final", f"[api error] {ev['error'].get('message','')}")
    except requests.RequestException as e:
        yield ("final", f"[api unreachable: {e}]")


# ----------------------------------------------------------------------
#  The router
# ----------------------------------------------------------------------
@dataclass
class Orchestrator:
    core: CoreClient = field(default_factory=CoreClient)
    workdir: str = WORKDIR
    history: list[tuple[str, str]] = field(default_factory=list)  # (role, text)

    def needs_claude(self, msg: str) -> bool:
        """Agent route: needs filesystem / shell / repo → Claude Code."""
        return bool(ESCALATE_RE.search(msg) or CODE_EXT_RE.search(msg)
                    or "/" in msg)

    def is_smart_text(self, msg: str) -> bool:
        """Direct-API route: non-coding but too rich for the 0.5B."""
        if not (DIRECT_API_ENABLED and ANTHROPIC_API_KEY):
            return False
        return bool(DIRECT_RE.search(msg)
                    or len(msg.split()) >= DIRECT_MIN_WORDS)

    def route_of(self, msg: str) -> str:
        """Three-way classify. Coding wins first, then smart-text, else local."""
        if self.needs_claude(msg):
            return "escalated"
        if self.is_smart_text(msg):
            return "direct_api"
        return "local"

    def _summary(self) -> str:
        recent = self.history[-HISTORY_TURNS:]
        return "\n".join(f"{r}: {t}" for r, t in recent)

    def handle(self, msg: str):
        """Route one turn. Yields ('route', name) then text/progress events."""
        self.history.append(("user", msg))
        route = self.route_of(msg)
        yield ("route", route)

        if route == "escalated":
            brief = build_brief(msg, self._summary(), self.workdir)
            final = []
            for kind, text in run_claude(brief, self.workdir):
                if kind == "final":
                    final.append(text)
                yield (kind, text)
            self.history.append(("assistant", " ".join(final)[:2000]))

        elif route == "direct_api":
            final = []
            for kind, text in call_claude_api(msg, self._summary()):
                final.append(text)
                yield (kind, text)
            self.history.append(("assistant", "".join(final)[:2000]))

        else:  # local
            answer = self.core.ask(msg)
            self.history.append(("assistant", answer))
            yield ("final", answer)


# ----------------------------------------------------------------------
#  Inbound HTTP(S) service  -  the endpoint the M5Stack router hits,
#  gated by the IP allowlist (+ optional bearer).  Run with `--serve`.
# ----------------------------------------------------------------------
def ip_allowed(addr: str) -> bool:
    """True if `addr` passes CLIENT_ALLOWLIST (empty list = allow all).
    Lift this into your own Flask/aiohttp front-end if you have one and
    reject the request when it returns False."""
    if not CLIENT_ALLOWLIST:
        return True
    try:
        ip = ipaddress.ip_address(addr)
    except ValueError:
        return False
    for entry in CLIENT_ALLOWLIST:
        try:
            if ip in ipaddress.ip_network(entry, strict=False):
                return True
        except ValueError:
            continue   # skip a malformed allowlist entry
    return False


def host_allowed(host: str) -> bool:
    """True if the request Host (port stripped, case-insensitive) passes
    HOST_ALLOWLIST.  Empty list = allow any Host.  Parity with the
    firmware's _hostAllowed()."""
    if not HOST_ALLOWLIST:
        return True
    host = (host or "").split(":")[0].strip().lower()
    if not host:
        return False
    return any(host == h.strip().lower() for h in HOST_ALLOWLIST if h.strip())


def serve_http(host: str = SERVE_HOST, port: int = SERVE_PORT) -> int:
    """Expose the orchestrator over HTTP(S) for the M5Stack router.

    POST a prompt (raw text, or JSON {"prompt": "..."}) and receive a
    text/event-stream:  data: {"delta": "..."} ... data: {"finish": true}
    then  data: [DONE]  - the exact shape NetDevice_Router parses.  Every
    request is gated by the IP allowlist (+ optional bearer token)."""
    import ssl
    from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

    # Fail-closed: this endpoint can drive Claude Code (shell + filesystem),
    # so refuse to start wide open.  A bearer token or a source-IP allowlist
    # counts as access control; the Host allowlist (rebinding guard) does not.
    if not (SERVE_BEARER or CLIENT_ALLOWLIST or SERVE_ALLOW_INSECURE):
        print(
            "[serve] REFUSING TO START: no access control configured.\n"
            "        This endpoint can run Claude Code with shell + filesystem\n"
            "        access. Set SERVE_BEARER (or $ROUTER_BEARER) and/or\n"
            "        CLIENT_ALLOWLIST. To override (e.g. already firewalled to\n"
            "        localhost/LAN), set SERVE_ALLOW_INSECURE = True.")
        return 2

    # NOTE: each request gets its OWN Orchestrator (created in do_POST).
    # ThreadingHTTPServer dispatches requests on separate threads, so a
    # single shared instance would race on its `history` list and bleed
    # context between unrelated clients.  Per-request state is correct here
    # — the device's escalation brief already carries its own context.

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _client(self) -> str:
            if TRUST_FORWARDED:
                xff = self.headers.get("X-Forwarded-For", "")
                if xff:
                    return xff.split(",")[0].strip()
            return self.client_address[0]

        def _json(self, code: int, obj: dict):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def _sse(self, obj: dict):
            self.wfile.write(f"data: {json.dumps(obj)}\n\n".encode())
            self.wfile.flush()

        def _guard(self) -> bool:
            client = self._client()
            if not ip_allowed(client):
                print(f"[serve] DENY {client} - ip not in allowlist")
                self._json(403, {"error": "forbidden: ip not allowed"})
                return False
            if not host_allowed(self.headers.get("Host", "")):
                print(f"[serve] DENY {client} - host not in allowlist")
                self._json(403, {"error": "forbidden: host not allowed"})
                return False
            if SERVE_BEARER and self.headers.get("Authorization", "") != f"Bearer {SERVE_BEARER}":
                print(f"[serve] DENY {client} - bad bearer")
                self._json(401, {"error": "unauthorized"})
                return False
            return True

        def do_GET(self):
            if not self._guard():
                return
            self._json(200, {"ok": True, "service": "orchestrator"})

        def do_POST(self):
            if not self._guard():
                return
            # Guard the length header (a non-numeric value would otherwise
            # raise) and clamp the body so a huge/absent-EOF request can't
            # exhaust memory.
            try:
                n = int(self.headers.get("Content-Length", 0) or 0)
            except ValueError:
                return self._json(400, {"error": "bad content-length"})
            if n < 0:
                return self._json(400, {"error": "bad content-length"})
            if n > MAX_POST_BYTES:
                return self._json(413, {"error": "payload too large"})
            raw = self.rfile.read(n).decode("utf-8", "replace") if n else ""
            prompt = raw
            try:
                obj = json.loads(raw)
                prompt = obj.get("prompt") or obj.get("ask") or raw
            except (json.JSONDecodeError, AttributeError):
                pass
            prompt = (prompt or "").strip()
            if not prompt:
                return self._json(400, {"error": "empty prompt"})

            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.send_header("Connection", "close")
            self.end_headers()
            print(f"[serve] {self._client()} -> {prompt[:60]!r}")
            # Per-request Orchestrator — no shared mutable state across threads.
            orch = Orchestrator()
            try:
                for kind, text in orch.handle(prompt):
                    if kind == "route":
                        self._sse({"route": text})
                    elif kind == "final":
                        self._sse({"delta": text})
                    # 'progress' steps stay server-side (firmware ignores them)
                self._sse({"finish": True})
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass   # client hung up mid-stream

        def log_message(self, *a):
            pass   # quiet default logging; we print our own lines

    httpd = ThreadingHTTPServer((host, port), Handler)
    tls = bool(SERVE_TLS_CERT and SERVE_TLS_KEY)
    if tls:
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(SERVE_TLS_CERT, SERVE_TLS_KEY)
        httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
    print(f"orchestrator service on {'https' if tls else 'http'}://{host}:{port}  "
          f"(allowlist: {CLIENT_ALLOWLIST or 'OFF - all IPs'}; "
          f"bearer: {'on' if SERVE_BEARER else 'off'})")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
        httpd.shutdown()
    return 0


# ----------------------------------------------------------------------
#  Terminal chat loop
# ----------------------------------------------------------------------
def main():
    if "--serve" in sys.argv:
        return serve_http()
    orch = Orchestrator()
    print(f"orchestrator -> core {CORE_URL} | claude workdir {WORKDIR}")
    api_state = "on" if (DIRECT_API_ENABLED and ANTHROPIC_API_KEY) else "off"
    print(f"routes: local (CoreS3) | direct API ({api_state}) | Claude Code")
    print("type a message (Ctrl-C to quit)\n")
    _tags = {"local": "  ~ local (CoreS3)",
             "direct_api": "  ~ direct Claude API (no repo)",
             "escalated": "  ~ escalating to Claude Code"}
    try:
        while True:
            msg = input("you  > ").strip()
            if not msg:
                continue
            for kind, text in orch.handle(msg):
                if kind == "route":
                    print(_tags.get(text, f"  ~ {text}"), flush=True)
                elif kind == "progress":
                    print(f"      ... {text}", flush=True)
                elif kind == "final":
                    print(f"\nbot  > {text}\n", flush=True)
    except (KeyboardInterrupt, EOFError):
        print("\nbye")


if __name__ == "__main__":
    sys.exit(main())
