#!/usr/bin/env bash
set -euo pipefail

# Directorio raíz del repo (un nivel por encima de scripts/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "==[ Tests ]========================================"
echo "Root:   ${ROOT_DIR}"
echo "Source: ${ROOT_DIR}/tests"
echo "Build:  ${ROOT_DIR}/tests/build"
echo "==================================================="
echo

cmake -S "${ROOT_DIR}/tests" -B "${ROOT_DIR}/tests/build"
cmake --build "${ROOT_DIR}/tests/build"

echo
echo "==[ Running tests ]==============================="
ctest \
  --test-dir "${ROOT_DIR}/tests/build" \
  --output-on-failure \
  -VV
echo "==================================================="