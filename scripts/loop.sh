#!/usr/bin/env bash
# scripts/robot.sh (loop hasta que falle)
set -euo pipefail

cd "$(dirname "$0")/.." || exit 1
mkdir -p tests/robot/output

ITER=1
while true; do
    echo "[robot.sh] Iteración ${ITER} - Ejecutando Robot..."
    # Ejecuta y captura rc sin salir del loop
    set +e
    robot -d "tests/robot/output" tests/robot/tests
    rc=$?
    set -e

    echo "[robot.sh] Iteración ${ITER} terminó con rc=${rc}"

    # Limpieza para no dejar procesos colgados (best-effort)
    bash scripts/kill.sh || true

    # Si falló, salimos y dejamos pruebas corriendo justo al fallo
    if [[ $rc -ne 0 ]]; then
        echo "[robot.sh] Fallo detectado (rc=${rc}). Saliendo sin más iteraciones."
        exit $rc
    fi

    ITER=$((ITER+1))
done