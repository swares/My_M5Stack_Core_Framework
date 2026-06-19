# openai_adapter

An OpenAI-compatible HTTP surface for the **M5Stack Core Framework**. It accepts standard
OpenAI Chat Completions requests and translates them to the framework's native fire-and-poll
protocol via the shared [`../protocol.py`](../protocol.py) (`AsyncDeviceClient`), so the wire
format is defined once and shared with `orchestrator.py` — the two consumers cannot drift.

- **Code lives here** (in the framework repo) because it's coupled to the firmware *protocol*.
- **Deployment lives in the homelab GitOps repo** (`gitops/workloads/ai-gateway/m5stack-adapter/`):
  k8s manifests that reference a pinned image built from this directory.

Any OpenAI-style client (the `openai` SDK, LangChain, LiteLLM, `curl`, etc.) can talk to a
CoreS3 by pointing its base URL at this adapter and treating the device plugins as "models".

## Endpoints

| Method | Path | Purpose |
|---|---|---|
| `POST` | `/v1/chat/completions` | Chat completion. Honours `stream: true` (SSE) and `stream: false` (single JSON body). |
| `GET`  | `/v1/models` | Lists the configured models (the device plugins). |
| `GET`  | `/healthz` | Liveness check — returns `{"ok": true, "models": [...]}`. |

Generation params the device owns (`temperature`, `max_tokens`, …) are **accepted and ignored** —
the request model is validated but extra fields pass through harmlessly.

## Models

Each "model" maps to a device host + protocol slug. The defaults (overridable, see `M5_MODELS`):

| Model id | Slug | What it is |
|---|---|---|
| `m5-llm` | `llm` | On-device NPU (Module LLM) — the local 0.5B. |
| `m5-route` | `route` | The firmware's 3-tier escalation router. |
| `m5-claude` | `claude` | Direct Anthropic (the model, not the Claude Code agent). |

### Route accounting
The device reports which tier actually served a turn via `route_taken`. The adapter passes it
back so local-vs-cloud accounting survives the OpenAI envelope:
- non-streaming: in both `system_fingerprint` and a top-level `x_route_taken` field;
- streaming: in `system_fingerprint` on the final chunk (the one with `finish_reason: "stop"`).

### History is client-owned
The OpenAI client sends the full message array each turn, so the adapter flattens it into a
single prompt and targets the **stateless** device plugins (`llm` / `route` / base `claude`) —
not the device's history-keeping plugin — to avoid doubling context. The flattened prompt is
clamped to the last `M5_PROMPT_MAX` characters.

## Configuration (environment variables)

All optional:

| Var | Default | Effect |
|---|---|---|
| `M5_HOST` | `https://cores3.local` | Base URL of the device. |
| `M5_MODELS` | _(unset)_ | JSON `{model: {host, slug}}` overriding the default model map. |
| `M5_USER` / `M5_PASS` | _(unset)_ | HTTP Basic Auth, if `WEB_AUTH_*` is set on the device. |
| `M5_TLS_VERIFY` | `false` | `true` to verify TLS (the device is self-signed by default). |
| `M5_POLL_INTERVAL` | `0.4` | Seconds between polls of the device. |
| `M5_IDLE_TIMEOUT` | `90` | Abandon a turn after N seconds with no new token. |
| `M5_MAX_TOTAL` | `1800` | Hard ceiling per turn, in seconds. |
| `M5_CLEAR_BEFORE` | `false` | `true` to send `?clear=1` before each prompt. |
| `M5_PROMPT_MAX` | `6000` | Max rendered prompt length, in characters. |

## Run locally

From the framework repo's `scripts/` dir (so `protocol.py` is importable alongside the package):

```bash
pip install -r openai_adapter/requirements.txt
uvicorn openai_adapter.main:app --host 0.0.0.0 --port 8080
```

Smoke test:

```bash
curl localhost:8080/v1/models

curl localhost:8080/v1/chat/completions \
  -H 'content-type: application/json' \
  -d '{"model":"m5-llm","messages":[{"role":"user","content":"hello"}]}'
```

With the OpenAI Python SDK:

```python
from openai import OpenAI
client = OpenAI(base_url="http://localhost:8080/v1", api_key="unused")
r = client.chat.completions.create(
    model="m5-route",
    messages=[{"role": "user", "content": "summarise the SS03 handoff brief"}],
)
print(r.choices[0].message.content, "| route:", r.system_fingerprint)
```

(The adapter does not check `api_key`; gate access at your ingress / network layer.)

## Build & deploy

Build from the framework repo's `scripts/` dir so `protocol.py` (one level up) is in the
Docker build context — see the `Dockerfile` header for why:

```bash
cd scripts
docker build -f openai_adapter/Dockerfile -t <registry>/m5stack-adapter:<ver> .
docker push <registry>/m5stack-adapter:<ver>
```

The image runs `uvicorn openai_adapter.main:app` on port **8080** as a non-root user. Then bump
the image tag in the homelab `deployment.yaml`; no app code lives in the homelab repo.

## See also
- `../protocol.py` — the shared device-protocol client (sync `DeviceClient` + async `AsyncDeviceClient`).
- `../orchestrator_README.md` — the sibling consumer (the local-first chat router) of the same protocol.
