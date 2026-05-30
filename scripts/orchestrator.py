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
    python orchestrator.py
"""
from __future__ import annotations
import json
import re
import subprocess
import os
import sys
import time
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


def run_claude(brief: str, workdir=WORKDIR):
    """Yield (kind, text) events from Claude Code as it works.

    kind is 'progress' (a paraphrasable tool step) or 'final' (assistant
    text). Parses --output-format stream-json; falls back to raw lines.
    """
    proc = subprocess.Popen(
        [CLAUDE_BIN, "-p", brief, "--output-format", "stream-json", "--verbose"],
        cwd=workdir, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
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
        proc.wait(timeout=CLAUDE_TIMEOUT)
    except subprocess.TimeoutExpired:
        proc.kill()
        yield ("final", "[claude timed out]")
    finally:
        if proc.stderr:
            err = proc.stderr.read().strip()
            if err and proc.returncode not in (0, None):
                yield ("final", f"[claude error] {err}")


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
#  Terminal chat loop
# ----------------------------------------------------------------------
def main():
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
