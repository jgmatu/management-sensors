#!/bin/bash
# tests/scripts/socat.sh
# Puente Telnet -> PTY -> bash (modo debug, consola lo más limpia posible)
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# Configuración
VTTY_LINK="/tmp/vtty_robot"
TCP_PORT=2000
LOG_FILE="/tmp/bridge.log"
# Debug: comando dentro de la PTY (bash sin perfiles para evitar ruido)
BASH_CMD="/bin/bash --noprofile --norc"
echo "[1/4] Limpiando recursos..."
pkill -f "PTY,link=${VTTY_LINK}" 2>/dev/null || true
pkill -f "TCP-LISTEN:${TCP_PORT}" 2>/dev/null || true
rm -f "$VTTY_LINK" "$LOG_FILE" || true
echo "[2/4] Creando PTY con socat y lanzando bash..."

socat -d -d PTY,link="${VTTY_LINK}",raw,echo=0 \
  EXEC:"${BASH_CMD}",pty,stderr \
  >>"$LOG_FILE" 2>&1 &

sleep 2

if [ -e "$VTTY_LINK" ]; then
    echo "PTY creada en $VTTY_LINK"
else
    echo "Error: No se pudo crear la PTY en $VTTY_LINK"
    exit 1
fi

echo "[3/4] Levantando listener TCP para telnet..."
echo "------------------------------------------------" | tee -a "$LOG_FILE"
echo "PUENTE LISTO:" | tee -a "$LOG_FILE"
echo "Telnet debe conectar a: 127.0.0.1:${TCP_PORT}" | tee -a "$LOG_FILE"
echo "Proceso destino dentro de la PTY: ${VTTY_LINK}" | tee -a "$LOG_FILE"
echo "Log: $LOG_FILE" | tee -a "$LOG_FILE"
echo "------------------------------------------------" | tee -a "$LOG_FILE"
socat TCP-LISTEN:${TCP_PORT},reuseaddr,fork FILE:${VTTY_LINK},raw,echo=0 \
  >>"$LOG_FILE" 2>&1