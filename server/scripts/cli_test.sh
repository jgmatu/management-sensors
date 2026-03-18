#!/bin/bash
# server/scripts/cli_test.sh

PORT=50443
HOST="localhost"
POLICY="../policies/client_policies.txt"
CERTS="../certs/ca.pem"

cd "$(dirname "$0")"

# 1. Start the coprocess
# Note: redirection 2>&1 ensures we see errors in the same stream
coproc BOTAN { 
    botan tls_client "$HOST" --port="$PORT" --policy="$POLICY" --trusted-cas="$CERTS" 2>&1
}

send_and_wait() {
    local cmd="$1"
    local got_ok=0
    if [[ -z ${BOTAN_PID} ]]; then
        echo "Error: Botan process is not running."
        return 1
    fi
    echo ">>> Sending: $cmd"
    echo "$cmd" >&"${BOTAN[1]}"
    while IFS= read -u "${BOTAN[0]}" -r line; do
        echo "Server: $line"
        # Si vemos OK:, marcamos éxito
        if [[ "$line" == *"OK:"* ]]; then
            got_ok=1
        fi
        # Condiciones de fin de respuesta
        if [[ "$line" == *"OK:"* ]] || [[ "$line" == *"FAILED:"* ]] || [[ "$line" == *"ERROR:"* ]]; then
            break
        fi
    done
    # Si no se ha visto OK:, consideramos fallo
    if [[ $got_ok -ne 1 ]]; then
        echo "Command '$cmd' did not receive an OK: response."
        return 1
    fi
    return 0
}

# --- TEST COMMANDS ---
send_and_wait "CONFIG_IP 1 IP 7.7.7.7/22";
send_and_wait "CONFIG_IP 2 IP 0.0.0.0/24";

# Cleanly close
echo "quit" >&"${BOTAN[1]}"
