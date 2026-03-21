#!/bin/bash
# scripts/gen_certs.sh
# Genera la PKI completa (CA + servidor + cliente) usando botan CLI.
# Los certificados quedan en server/certs/ listos para todos los componentes.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CERTS_DIR="${REPO_ROOT}/server/certs"
DAYS_CA=3650
DAYS_CERT=365
HASH="SHA-384"
ALGO="ECDSA"
PARAMS="secp384r1"

CA_CN="Management-Sensors CA"
SERVER_CN="localhost"

echo "=== Generación de certificados PQC/TLS ==="
echo "    Directorio: ${CERTS_DIR}"
echo ""

mkdir -p "${CERTS_DIR}"

# ── 1. CA (Certificate Authority) ────────────────────────────────────────────
echo "[1/7] Generando clave privada de la CA (${ALGO} ${PARAMS})..."
botan keygen \
    --algo="${ALGO}" \
    --params="${PARAMS}" \
    --output="${CERTS_DIR}/ca.key"

echo "[2/7] Generando certificado autofirmado de la CA (${DAYS_CA} días)..."
botan gen_self_signed \
    --ca \
    --country=ES \
    --organization="Management-Sensors" \
    --dns="${SERVER_CN}" \
    --hash="${HASH}" \
    --days="${DAYS_CA}" \
    --output="${CERTS_DIR}/ca.pem" \
    "${CERTS_DIR}/ca.key" \
    "${CA_CN}"

echo "    CA subject:"
botan cert_info "${CERTS_DIR}/ca.pem" | head -8

# ── 2. Servidor ──────────────────────────────────────────────────────────────
echo ""
echo "[3/7] Generando clave privada del servidor (${ALGO} ${PARAMS})..."
botan keygen \
    --algo="${ALGO}" \
    --params="${PARAMS}" \
    --output="${CERTS_DIR}/server.key"

echo "[4/7] Generando CSR del servidor (CN=${SERVER_CN}, DNS=${SERVER_CN})..."
botan gen_pkcs10 \
    --dns="${SERVER_CN}" \
    --hash="${HASH}" \
    --output="${CERTS_DIR}/server.req" \
    "${CERTS_DIR}/server.key" \
    "${SERVER_CN}"

echo "[5/7] Firmando certificado del servidor con la CA (${DAYS_CERT} días)..."
botan sign_cert \
    --duration="${DAYS_CERT}" \
    --hash="${HASH}" \
    --output="${CERTS_DIR}/server.pem" \
    "${CERTS_DIR}/ca.pem" \
    "${CERTS_DIR}/ca.key" \
    "${CERTS_DIR}/server.req"

echo "    Server subject:"
botan cert_info "${CERTS_DIR}/server.pem" | head -8

# ── 3. Cliente (para mTLS del proxy) ────────────────────────────────────────
echo ""
echo "[6/7] Generando clave privada del cliente (${ALGO} ${PARAMS})..."
botan keygen \
    --algo="${ALGO}" \
    --params="${PARAMS}" \
    --output="${CERTS_DIR}/client.key"

echo "    Generando CSR del cliente..."
botan gen_pkcs10 \
    --dns="${SERVER_CN}" \
    --hash="${HASH}" \
    --output="${CERTS_DIR}/client.req" \
    "${CERTS_DIR}/client.key" \
    "proxy-client"

echo "    Firmando certificado del cliente con la CA (${DAYS_CERT} días)..."
botan sign_cert \
    --duration="${DAYS_CERT}" \
    --hash="${HASH}" \
    --output="${CERTS_DIR}/client.pem" \
    "${CERTS_DIR}/ca.pem" \
    "${CERTS_DIR}/ca.key" \
    "${CERTS_DIR}/client.req"

echo "    Client subject:"
botan cert_info "${CERTS_DIR}/client.pem" | head -8

# ── 4. Verificación ─────────────────────────────────────────────────────────
echo ""
echo "[7/7] Verificando cadena de certificados..."
echo -n "    server.pem -> ca.pem: "
botan cert_verify "${CERTS_DIR}/server.pem" "${CERTS_DIR}/ca.pem" || true
echo -n "    client.pem -> ca.pem: "
botan cert_verify "${CERTS_DIR}/client.pem" "${CERTS_DIR}/ca.pem" || true

echo ""
echo "=== Ficheros generados ==="
ls -la "${CERTS_DIR}/"
echo ""
echo "Listo. Recuerda añadir require_cert_revocation_info = false"
echo "a las políticas TLS para entorno de desarrollo (sin CRL/OCSP)."
