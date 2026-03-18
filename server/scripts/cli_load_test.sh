#!/bin/bash
# server/scripts/cli_load_test.sh
# Lanza múltiples instancias de cli_test.sh en paralelo.

cd "$(dirname "$0")" || exit 1
PARALLEL_CLIENTS=${1:-50}   # número de clientes en paralelo, por defecto 50
LOG_DIR="log"
mkdir -p "$LOG_DIR"
pids=()
for i in $(seq 1 "$PARALLEL_CLIENTS"); do
    ./cli_test.sh >"${LOG_DIR}/cli_client_${i}.log" 2>&1 &
    pids+=("$!")
done
fail=0
for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
        fail=1
    fi
done
if [[ $fail -ne 0 ]]; then
    echo "At least one CLI client failed."
    exit 1
else
    echo "All CLI clients finished successfully."
    exit 0
fi