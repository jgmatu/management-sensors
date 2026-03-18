#!/bin/bash
# server/scripts/cli_load_test.sh
# Lanza múltiples instancias de cli_test.sh en PARALELO
# para probar el servidor de comunicaciones.
cd "$(dirname "$0")" || exit 1
PARALLEL_CLIENTS=${1:-50}   # número de clientes en paralelo, por defecto 50
LOG_DIR="log"
mkdir -p "$LOG_DIR"
echo "Launching $PARALLEL_CLIENTS parallel CLI clients..."
pids=()
for i in $(seq 1 "$PARALLEL_CLIENTS"); do
    echo "[CLI-LOAD] Launching client $i..."
    ./cli_test.sh >"${LOG_DIR}/cli_client_${i}.log" 2>&1 &
    pid=$!
    echo "[CLI-LOAD]   client $i PID=$pid (log: ${LOG_DIR}/cli_client_${i}.log)"
    pids+=("$pid")
done
# Esperar a que todos terminen
fail=0
idx=1
for pid in "${pids[@]}"; do
    echo "[CLI-LOAD] Waiting for client $idx (PID=$pid)..."
    if wait "$pid"; then
        rc=$?
        echo "[CLI-LOAD] client $idx (PID=$pid) finished OK (rc=$rc)"
    else
        rc=$?
        echo "[CLI-LOAD][ERROR] client $idx (PID=$pid) exited with code $rc"
        fail=1
    fi
    idx=$((idx + 1))
done
if [[ $fail -ne 0 ]]; then
    echo "At least one CLI client failed."
    exit 1
else
    echo "All CLI clients finished successfully."
    exit 0
fi