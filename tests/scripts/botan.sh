#!/bin/bash
# tests/scripts/botan.sh
# Multiconexión real: telnet TCP -> socat fork -> botan tls_client (1 botan por conexión)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# Listener telnet
TCP_PORT=2000
LOG_FILE="/tmp/botan_cli_bridge.log"

# Botan TLS client target (tu server TLS)
BOTAN_HOST="localhost"
BOTAN_PORT=50443

BOTAN_POLICY="${REPO_ROOT}/server/policies/client_policies.txt"
BOTAN_CERTS="${REPO_ROOT}/server/certs/ca.pem"
BOTAN_BIN="botan"

echo "[1/2] Limpieza previa..."

# 1) matar socat que esté levantando el listener del puerto
pkill -f "socat TCP-LISTEN:${TCP_PORT}" 2>/dev/null || true
pkill -f "TCP-LISTEN:${TCP_PORT}" 2>/dev/null || true

# 2) matar por puerto (por si quedó un proceso distinto)
fuser -k "${TCP_PORT}/tcp" 2>/dev/null || true

# 3) esperar a que desaparezca el socket (evita carrera)
sleep 0.5
rm -f "$LOG_FILE" || true

BOTAN_CMD="${BOTAN_BIN} tls_client ${BOTAN_HOST} --port=${BOTAN_PORT} --policy=${BOTAN_POLICY} --trusted-cas=${BOTAN_CERTS}"

echo "[2/2] Socat listener (fork) listo en 127.0.0.1:${TCP_PORT}"
echo "Proceso ejecutado por conexión: ${BOTAN_CMD}"
echo "Log: ${LOG_FILE}"

# Cada conexión TCP => socat fork => arranca un botan tls_client independiente
# raw/echo=0: minimiza transformaciones del transporte.
socat -d -d \
  TCP-LISTEN:${TCP_PORT},reuseaddr,fork,bind=127.0.0.1 \
  EXEC:"${BOTAN_CMD}" \
  >>"$LOG_FILE" 2>&1