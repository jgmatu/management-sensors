#!/usr/bin/env bash
# tests/scripts/https_request.sh
# Sends an HTTP request through Botan's PQC TLS client.
#
# Usage:
#   https_request.sh GET  /api/connection_details
#   https_request.sh POST /api/config_ip '{"sensor_id":1,"ip":"7.7.7.7/22"}'

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

METHOD="${1:?Usage: $0 METHOD PATH [BODY]}"
URL_PATH="${2:?Usage: $0 METHOD PATH [BODY]}"
BODY="${3:-}"

HOST="${HTTPS_HOST:-localhost}"
PORT="${HTTPS_PORT:-50443}"
POLICY="${REPO_ROOT}/server/policies/client_policies.txt"
CA_CERT="${REPO_ROOT}/server/certs/ca.pem"

if [[ "$METHOD" == "POST" && -n "$BODY" ]]; then
    CONTENT_LENGTH=${#BODY}
    REQUEST=$(printf "%s %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s" \
        "$METHOD" "$URL_PATH" "$HOST" "$CONTENT_LENGTH" "$BODY")
else
    REQUEST=$(printf "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n" \
        "$METHOD" "$URL_PATH" "$HOST")
fi

RESPONSE=$(printf '%s' "$REQUEST" | timeout 30s botan tls_client "$HOST" \
    --port="$PORT" \
    --policy="$POLICY" \
    --trusted-cas="$CA_CERT" 2>/dev/null || true)

# Strip Botan handshake output, keep only the HTTP response
echo "$RESPONSE" | sed '1,/Handshake complete/d'
