#!/bin/bash
# tests/scripts/socat.sh
# Telnet TCP -> (fork por conexión) -> socat EXEC con PTY -> bash

set -euo pipefail

TCP_PORT=2000
LOG_FILE="/tmp/bridge.log"

BASH_CMD="/bin/bash --noprofile --norc"

echo "[1/3] Limpieza previa..."
pkill -f "TCP-LISTEN:${TCP_PORT}" 2>/dev/null || true
rm -f "$LOG_FILE" || true

echo "[2/3] Levantando listener con fork y PTY por conexión..."
echo "Puerto: 127.0.0.1:${TCP_PORT}" | tee -a "$LOG_FILE"
echo "Comando en PTY por conexión: ${BASH_CMD}" | tee -a "$LOG_FILE"
echo "Log: $LOG_FILE" | tee -a "$LOG_FILE"

# Cada conexión:
# - socat acepta (TCP-LISTEN)
# - fork crea un proceso hijo por conexión
# - EXEC lanza bash
# - pty crea una PTY independiente por hijo/conexión
socat -d -d \
  TCP-LISTEN:${TCP_PORT},reuseaddr,fork,bind=127.0.0.1 \
  EXEC:"${BASH_CMD}",pty,raw,echo=0,stderr \
  >>"$LOG_FILE" 2>&1

echo "[3/3] Fin (cuando el proceso de socat termine)"