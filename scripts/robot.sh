#!/bin/bash
# scripts/robot.sh

# Ir a la raíz del repo desde scripts/
cd "$(dirname "$0")/.." || exit 1

# Crear carpeta de salida para Robot
mkdir -p tests/robot/output

# Ejecutar todas las suites Robot
robot -d tests/robot/output tests/robot/tests

sleep 1;

bash scripts/kill.sh
