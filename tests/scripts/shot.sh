#!/usr/bin/env bash
set -euo pipefail

HOST="$1"
PORT="$2"
SENSOR_ID="$3"
IP_CIDR="$4"
REQUESTS_PER_CLIENT="$5"

CMD="CONFIG_IP ${SENSOR_ID} IP ${IP_CIDR}"

# timeout global por seguridad
TIMEOUT_SEC="${TIMEOUT_SEC:-$((60 + REQUESTS_PER_CLIENT * 5))}"

timeout "${TIMEOUT_SEC}s" bash -lc '
    exec 3<>/dev/tcp/"'"$HOST"'"'/'"'"$PORT"'" || exit 2

    # Esperar handshake
    while IFS= read -r -u 3 line; do
        if [[ "$line" == *"Handshake complete"* ]]; then
            break
        fi
    done

    # Repetir N peticiones antes de cerrar la conexión.
    out=""
    any_ok=0
    any_fail=0
    for ((req=0; req<'"$REQUESTS_PER_CLIENT"'; req++)); do
        # Enviar comando
        printf "%s\n" "'"$CMD"'" >&3

        # Leer respuesta hasta OK/ERROR/FAILED/TIMEOUT
        while IFS= read -r -u 3 line; do
            out+="$line"$'\''\n'\''
            if [[ "$line" == *"OK:"* ]]; then
                any_ok=1
                break
            fi
            if [[ "$line" == *"FAILED:"* ]] || [[ "$line" == *"ERROR:"* ]] || [[ "$line" == *"Request Timed Out"* ]]; then
                any_fail=1
                break
            fi
        done
    done

    # Cerrar
    printf "quit\n" >&3 || true
    exec 3<&- 3>&- || true

    echo "$out" | tail -n 30 >&2

    if [[ "$any_fail" -eq 0 && "$any_ok" -eq 1 ]]; then
        exit 0
    fi

    exit 1
'