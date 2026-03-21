#!/bin/bash
cd "$(dirname "$0")/.."

./build/proxy/proxy \
  --listen-port 8443 \
  --backend-host localhost \
  --backend-port 50443 \
  --ca-cert ./server/certs/ca.pem \
  --cert ./server/certs/client.pem \
  --key ./server/certs/client.key \
  --policy ./server/policies/pqc_basic.txt
