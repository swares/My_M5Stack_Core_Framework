"""
openai_adapter — OpenAI-compatible surface for the M5Stack Core Framework.

Exposes POST /v1/chat/completions and GET /v1/models and translates them to the framework's
native fire-and-poll protocol via the shared `protocol.AsyncDeviceClient` (so the wire format is
defined once, in protocol.py, shared with orchestrator.py). `route_taken` is passed back in
`system_fingerprint` (and an `x_route_taken` field) so local-vs-cloud accounting survives.

Config via env (all optional):
  M5_HOST            base URL of the device          (default https://cores3.local)
  M5_MODELS          JSON {model: {host, slug}}      (overrides defaults below)
  M5_USER / M5_PASS  HTTP Basic Auth (if WEB_AUTH on the device)
  M5_TLS_VERIFY      "true" to verify TLS            (default false; device is self-signed)
  M5_POLL_INTERVAL   seconds between polls           (default 0.4)
  M5_IDLE_TIMEOUT    abandon after N s with no token (default 90)
  M5_MAX_TOTAL       hard ceiling per turn, seconds  (default 1800)
  M5_CLEAR_BEFORE    "true" to send ?clear=1 first   (default false)
  M5_PROMPT_MAX      max rendered prompt chars       (default 6000)
"""
import os, json, time, uuid
from typing import Any
from fastapi import FastAPI, HTTPException
from fastapi.responses import JSONResponse, StreamingResponse
from pydantic import BaseModel

from protocol import AsyncDeviceClient   # shared with orchestrator.py


def _models() -> dict:
    raw = os.environ.get("M5_MODELS")
    if raw:
        return json.loads(raw)
    host = os.environ.get("M5_HOST", "https://cores3.local")
    return {
        "m5-llm":    {"host": host, "slug": "llm"},     # on-device NPU (Module LLM)
        "m5-route":  {"host": host, "slug": "route"},   # 3-tier escalation router
        "m5-claude": {"host": host, "slug": "claude"},  # direct Anthropic (model, not agent)
    }


MODELS        = _models()
AUTH          = (os.environ["M5_USER"], os.environ.get("M5_PASS", "")) if os.environ.get("M5_USER") else None
VERIFY        = os.environ.get("M5_TLS_VERIFY", "false").lower() == "true"
POLL_INTERVAL = float(os.environ.get("M5_POLL_INTERVAL", "0.4"))
IDLE_TIMEOUT  = float(os.environ.get("M5_IDLE_TIMEOUT", "90"))
MAX_TOTAL     = float(os.environ.get("M5_MAX_TOTAL", "1800"))
CLEAR_BEFORE  = os.environ.get("M5_CLEAR_BEFORE", "false").lower() == "true"
PROMPT_MAX    = int(os.environ.get("M5_PROMPT_MAX", "6000"))

app = FastAPI(title="m5stack-adapter", version="2.0")


class ChatRequest(BaseModel):
    model: str
    messages: list[dict[str, Any]]
    stream: bool = False
    class Config:
        extra = "allow"   # accept + ignore temperature/max_tokens/etc; device owns gen params


def render_prompt(messages: list[dict]) -> str:
    """Flatten an OpenAI message array into the single string the device's ask= wants.
    The OpenAI client owns history, so target the STATELESS plugins (llm/route/base claude),
    NOT NetDevice_ClaudeAPI_History — else context doubles."""
    def text_of(c):
        if isinstance(c, list):
            return "".join(p.get("text", "") for p in c if isinstance(p, dict))
        return c or ""
    system = "\n".join(text_of(m.get("content")) for m in messages if m.get("role") == "system")
    convo = []
    for m in messages:
        role = m.get("role")
        if role == "system":
            continue
        who = "User" if role == "user" else "Assistant"
        convo.append(f"{who}: {text_of(m.get('content'))}")
    prompt = (f"{system}\n\n" if system else "") + "\n".join(convo) + "\nAssistant:"
    return prompt[-PROMPT_MAX:]


def _client(host: str) -> AsyncDeviceClient:
    return AsyncDeviceClient(base=host, auth=AUTH, verify=VERIFY,
                             poll_interval=POLL_INTERVAL, idle=IDLE_TIMEOUT, total=MAX_TOTAL)


def _approx_tokens(s: str) -> int:
    return max(1, len(s) // 4)


@app.get("/healthz")
async def healthz():
    return {"ok": True, "models": list(MODELS)}


@app.get("/v1/models")
async def list_models():
    return {"object": "list",
            "data": [{"id": m, "object": "model", "owned_by": "m5stack"} for m in MODELS]}


@app.post("/v1/chat/completions")
async def chat(req: ChatRequest):
    spec = MODELS.get(req.model)
    if not spec:
        raise HTTPException(status_code=404, detail=f"unknown model '{req.model}'")
    host, slug = spec["host"], spec["slug"]
    prompt = render_prompt(req.messages)
    cid = "chatcmpl-" + uuid.uuid4().hex[:24]
    created = int(time.time())
    client = _client(host)

    if req.stream:
        async def sse():
            first = {"id": cid, "object": "chat.completion.chunk", "created": created,
                     "model": req.model, "choices": [{"index": 0,
                     "delta": {"role": "assistant"}, "finish_reason": None}]}
            yield f"data: {json.dumps(first)}\n\n"
            route = None
            async for kind, text, route in client.stream(slug, prompt, clear=CLEAR_BEFORE):
                if kind == "delta" and text:
                    chunk = {"id": cid, "object": "chat.completion.chunk", "created": created,
                             "model": req.model, "choices": [{"index": 0,
                             "delta": {"content": text}, "finish_reason": None}]}
                    yield f"data: {json.dumps(chunk)}\n\n"
            done = {"id": cid, "object": "chat.completion.chunk", "created": created,
                    "model": req.model, "system_fingerprint": route or "",
                    "choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}
            yield f"data: {json.dumps(done)}\n\n"
            yield "data: [DONE]\n\n"
        return StreamingResponse(sse(), media_type="text/event-stream")

    answer, route = "", None
    async for kind, text, route in client.stream(slug, prompt, clear=CLEAR_BEFORE):
        if kind == "delta":
            answer += text
    return JSONResponse({
        "id": cid, "object": "chat.completion", "created": created, "model": req.model,
        "system_fingerprint": route or "",
        "x_route_taken": route,
        "choices": [{"index": 0, "finish_reason": "stop",
                     "message": {"role": "assistant", "content": answer}}],
        "usage": {"prompt_tokens": _approx_tokens(prompt),
                  "completion_tokens": _approx_tokens(answer),
                  "total_tokens": _approx_tokens(prompt) + _approx_tokens(answer)},
    })
