#!/usr/bin/env bash
set -euo pipefail

# Directorio raíz del repo
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==[ Tests ]========================================"
echo "Root:   ${ROOT_DIR}"
echo "Build:  ${ROOT_DIR}/build"
echo "==================================================="
echo

# 1) Configurar y compilar TODO el proyecto (incluye tests)
cmake -S "${ROOT_DIR}" -B "${ROOT_DIR}/build"
cmake --build "${ROOT_DIR}/build"

echo
echo "==[ Running all CTest suites ]====================="
ctest \
  --test-dir "${ROOT_DIR}/build" \
  --output-on-failure
echo "==================================================="