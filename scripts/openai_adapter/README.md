# openai_adapter

OpenAI-compatible surface (`/v1/chat/completions`, `/v1/models`) for the M5Stack Core Framework.
Translates to the framework's native fire-and-poll protocol via the shared `../protocol.py`, so
the wire format is defined once and shared with `orchestrator.py` — they cannot drift.

- **Code lives here** (in the framework repo) because it's coupled to the firmware *protocol*.
- **Deployment lives in the homelab GitOps repo** (`gitops/workloads/ai-gateway/m5stack-adapter/`):
  k8s manifests that reference a pinned image built from this directory.

Build (from the framework repo's `scripts/` dir, so `protocol.py` is in context):
```bash
docker build -f openai_adapter/Dockerfile -t <registry>/m5stack-adapter:<ver> .
docker push <registry>/m5stack-adapter:<ver>
```
Then bump the image tag in the homelab `deployment.yaml`.
