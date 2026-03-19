#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TELNET_HOST="127.0.0.1"
TELNET_PORT="2000"

# DB
START_DB="${START_DB:-1}"
STOP_DB_ON_EXIT="${STOP_DB_ON_EXIT:-0}"
PG_CTL="/usr/local/pgsql/bin/pg_ctl"
PG_DATA="/usr/local/pgsql/data"
PG_LOGFILE="${REPO_ROOT}/logs/postgresql.log"
PSQL_BIN="psql"
DB_HOST="localhost"
DB_PORT="5432"
DB_NAME="javi"
DB_USER="javi"

cleanup() {
    # Limpieza de procesos (best-effort)
    "${REPO_ROOT}/scripts/kill.sh" >/dev/null 2>&1 || true

    if [[ "${STOP_DB_ON_EXIT}" == "1" ]]; then
        "${PG_CTL}" -D "${PG_DATA}" stop >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT INT TERM

wait_for_db() {
    local retries=30
    local i
    for ((i=1; i<=retries; i++)); do
        if "${PSQL_BIN}" -h "${DB_HOST}" -p "${DB_PORT}" -d "${DB_NAME}" -U "${DB_USER}" \
            -c "SELECT 1;" >/dev/null 2>&1; then
            return 0
        fi
        sleep 1
    done
    echo "[run.sh][ERROR] No pude conectar a PostgreSQL en ${DB_HOST}:${DB_PORT} (${DB_NAME})."
    return 1
}

echo "[run.sh] Arrancando (opcional) PostgreSQL..."
if [[ "${START_DB}" == "1" ]]; then
    # Limpiar log previo de PostgreSQL antes de un nuevo arranque.
    mkdir -p "$(dirname "${PG_LOGFILE}")"
    rm -f "${PG_LOGFILE}"

    # Si ya está arriba, pg_ctl start normalmente no falla “duro”, pero igual no queremos romper el flujo.
    "${PG_CTL}" -D "${PG_DATA}" -l "${PG_LOGFILE}" start >/dev/null 2>&1 || true
    wait_for_db
else
    echo "[run.sh] START_DB=0, no se arranca PostgreSQL."
fi

echo "[run.sh] Limpieza previa..."
"${REPO_ROOT}/scripts/kill.sh" >/dev/null 2>&1 || true

echo "[run.sh] Arrancando server..."
bash "${REPO_ROOT}/scripts/server.sh" >/tmp/server_run.log 2>&1 &
SERVER_PID=$!

echo "[run.sh] Arrancando botan bridge (TCP 2000 -> botan)..."
bash "${REPO_ROOT}/tests/scripts/botan.sh" >/tmp/botan_bridge_run.log 2>&1 &
BOTAN_BRIDGE_PID=$!

echo "[run.sh] Esperando telnet bridge ${TELNET_HOST}:${TELNET_PORT}..."
tries=150
i=0
until bash -lc "cat < /dev/null > /dev/tcp/${TELNET_HOST}/${TELNET_PORT}" >/dev/null 2>&1; do
    sleep 0.2
    i=$((i+1))
    if [[ $i -ge $tries ]]; then
        echo "[run.sh][ERROR] Timeout esperando ${TELNET_HOST}:${TELNET_PORT}"
        exit 1
    fi
done

echo "[run.sh] Arrancando controller..."
bash "${REPO_ROOT}/scripts/controller.sh" >/tmp/controller_run.log 2>&1 &
CONTROLLER_PID=$!

echo "[run.sh] Arrancando sensor..."
bash "${REPO_ROOT}/scripts/sensor.sh" >/tmp/sensor_run.log 2>&1 &
SENSOR_PID=$!

echo "[run.sh] OK: server=${SERVER_PID}, botan_bridge=${BOTAN_BRIDGE_PID}, controller=${CONTROLLER_PID}, sensor=${SENSOR_PID}"

# Mantener vivo: si muere algo, salimos y el trap limpia.
wait -n "$SERVER_PID" "$BOTAN_BRIDGE_PID" "$CONTROLLER_PID" "$SENSOR_PID"
