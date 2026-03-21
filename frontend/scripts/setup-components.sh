#!/usr/bin/env bash
# Merge private/components → src/components; seed private/components/auth from auth-seed if missing.
set -euo pipefail

FRONTEND_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_ROOT="$(cd "$FRONTEND_DIR/.." && pwd)"
PRIV_COMPONENTS="${REPO_ROOT}/private/components"
DEST="${FRONTEND_DIR}/src/components"

mkdir -p "${PRIV_COMPONENTS}/auth"
# auth-seed es la fuente versionada del shell de login; sobrescribe private/components/auth en cada setup.
for src in "${FRONTEND_DIR}/auth-seed"/*.tsx; do
  [ -f "${src}" ] || continue
  cp -f "${src}" "${PRIV_COMPONENTS}/auth/"
done

mkdir -p "${DEST}"
# Sincronizar exactamente con private/components (como antes); auth se siembra arriba si faltaba.
rm -rf "${DEST:?}/"*
if [ -d "${PRIV_COMPONENTS}" ] && [ "$(find "${PRIV_COMPONENTS}" -mindepth 1 -maxdepth 1 2>/dev/null | wc -l)" -gt 0 ]; then
  cp -a "${PRIV_COMPONENTS}/." "${DEST}/"
fi
