#!/bin/bash
# tests/scripts/https_proxy.sh
# Proxy C++ HTTP plano -> TLS PQC (Botan)
#
# curl/Robot (HTTP plano) -> proxy :8443 -> servidor :50443 (TLS PQC)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

PROXY_PORT="${PROXY_PORT:-8443}"
PROXY_BIN="${REPO_ROOT}/build/proxy/proxy"

BOTAN_HOST="localhost"
BOTAN_PORT="${BACKEND_PORT:-50444}"

CA_CERT="${REPO_ROOT}/server/certs/ca.pem"
POLICY="${REPO_ROOT}/server/policies/pqc_basic.txt"

echo "[1/2] Limpieza previa del proxy HTTPS..."

pkill -f "proxy.*--listen-port.*${PROXY_PORT}" 2>/dev/null || true
fuser -k "${PROXY_PORT}/tcp" 2>/dev/null || true

sleep 0.5

echo "[2/2] Lanzando proxy C++ en 127.0.0.1:${PROXY_PORT} -> ${BOTAN_HOST}:${BOTAN_PORT} (TLS PQC)"

exec "${PROXY_BIN}" \
  --listen-port "${PROXY_PORT}" \
  --backend-host "${BOTAN_HOST}" \
  --backend-port "${BOTAN_PORT}" \
  --ca-cert "${CA_CERT}" \
  --cert "${REPO_ROOT}/server/certs/client.pem" \
  --key "${REPO_ROOT}/server/certs/client.key" \
  --policy "${POLICY}"
