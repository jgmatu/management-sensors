#!/usr/bin/env bash
set -euo pipefail

HOST="$1"
PORT="$2"
CLIENTS="$3"
IP_CIDR="$4"

pids=()
for ((i=0; i<CLIENTS; i++)); do
  sensor_id=$(( (i % 2) + 1 ))
  bash tests/scripts/shot.sh "$HOST" "$PORT" "$sensor_id" "$IP_CIDR" &
  pids+=( "$!" )
done

fail=0
for pid in "${pids[@]}"; do
  if ! wait "$pid"; then
    fail=1
  fi
done

exit "$fail"