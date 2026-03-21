#!/bin/bash
# Arranca el servidor en modo HTTP sobre TLS (PQC) con JWT para la API REST.
# Certificados TLS: server/certs/ (gen_certs.sh)
# JWT: server/certs/jwt.key + jwt.pem (mismo script)

set -euo pipefail

cd "$(dirname "$0")/.."

# Defaults; override with env JWT_KEY / JWT_CERT or --jwt-key / --jwt-cert
JWT_KEY="${JWT_KEY:-./server/certs/jwt.key}"
JWT_CERT="${JWT_CERT:-./server/certs/jwt.pem}"

usage() {
    cat <<'EOF'
Usage: https.sh [OPTIONS]

Starts ./build/server/server in --mode http with TLS and JWT for /api/*.

Options:
  --jwt-key PATH   Path to JWT ES384 private key (default: ./server/certs/jwt.key)
  --jwt-cert PATH  Path to JWT ES384 certificate (default: ./server/certs/jwt.pem)
  --no-jwt         Do not pass --jwt-key/--jwt-cert (API auth disabled)
  -h, --help       Show this help

Environment (optional overrides before parsing):
  JWT_KEY   Same as --jwt-key
  JWT_CERT  Same as --jwt-cert
EOF
}

USE_JWT=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --jwt-key)
            JWT_KEY="${2:?missing path after --jwt-key}"
            shift 2
            ;;
        --jwt-cert)
            JWT_CERT="${2:?missing path after --jwt-cert}"
            shift 2
            ;;
        --no-jwt)
            USE_JWT=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

SERVER_ARGS=(
    ./build/server/server
    --cert ./server/certs/server.pem
    --key ./server/certs/server.key
    --port 50443
    --mode http
    --document-root ./server/webroot
    --policy ./server/policies/pqc_basic.txt
)

if [[ "$USE_JWT" -eq 1 ]]; then
    SERVER_ARGS+=(--jwt-key "$JWT_KEY" --jwt-cert "$JWT_CERT")
fi

exec "${SERVER_ARGS[@]}"
