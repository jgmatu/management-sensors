#!/usr/bin/env bash
set -euo pipefail

HOST="$1"
PORT="$2"
SENSOR_ID="$3"
IP_CIDR="$4"

CMD="CONFIG_IP ${SENSOR_ID} IP ${IP_CIDR}"

# timeout global por seguridad
TIMEOUT_SEC="${TIMEOUT_SEC:-60}"

timeout "${TIMEOUT_SEC}s" bash -lc '
    exec 3<>/dev/tcp/"'"$HOST"'"'/'"'"$PORT"'" || exit 2

    # Esperar handshake
    while IFS= read -r -u 3 line; do
        if [[ "$line" == *"Handshake complete"* ]]; then
            break
        fi
    done

    # Enviar comando
    printf "%s\n" "'"$CMD"'" >&3

    # Leer respuesta hasta OK/ERROR/FAILED/TIMEOUT
    out=""
    while IFS= read -r -u 3 line; do
        out+="$line"$'\''\n'\''
        if [[ "$line" == *"OK:"* ]] || [[ "$line" == *"FAILED:"* ]] || [[ "$line" == *"ERROR:"* ]] || [[ "$line" == *"Request Timed Out"* ]]; then
            break
        fi
    done

    # Cerrar
    printf "quit\n" >&3 || true
    exec 3<&- 3>&- || true

    echo "$out" | tail -n 30 >&2

    if echo "$out" | grep -q "OK:"; then
        if echo "$out" | grep -qE "FAILED:|ERROR:|Request Timed Out"; then
            exit 1
        fi
        exit 0
    fi

    exit 1
'