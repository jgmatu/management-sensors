#!/bin/bash
# tests/scripts/robot.sh

# Ir al directorio raíz del repo (por si se llama desde otro sitio)
cd "$(dirname "$0")/../.." || exit 1

# Crear carpeta de salida para Robot
mkdir -p tests/robot/output

# Ejecutar la suite de base de datos (hola mundo + conexión)
robot -d tests/robot/output tests/robot/tests
