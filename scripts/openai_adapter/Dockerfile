# Build from the scripts/ directory so protocol.py (one level up) is in the context:
#   docker build -f openai_adapter/Dockerfile -t <registry>/m5stack-adapter:<ver> .
# (run from the framework repo's scripts/ dir). Then push and pin the tag in the homelab manifests.
FROM python:3.12-slim
WORKDIR /app
COPY openai_adapter/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt
COPY protocol.py ./
COPY openai_adapter/ ./openai_adapter/
EXPOSE 8080
RUN useradd -u 10001 -m svc
USER 10001
# protocol.py is importable from /app; the app is the openai_adapter package
CMD ["uvicorn", "openai_adapter.main:app", "--host", "0.0.0.0", "--port", "8080"]
