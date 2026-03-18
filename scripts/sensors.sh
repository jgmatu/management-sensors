#!/bin/bash
# Ir a la raíz del proyecto
cd "$(dirname "$0")/.." || exit 1
# Ejecutar el binario del sensor desde el build
# Pasa todos los argumentos que des al script directamente al binario
./build/sensor/sensor "$@"
