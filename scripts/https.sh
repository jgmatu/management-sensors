#!/bin/bash

cd "$(dirname "$0")/.."

./build/server/server \
  --cert ./server/certs/server.pem \
  --key ./server/certs/server.key \
  --port 50443 \
  --mode http \
  --document-root ./server/webroot \
  --policy ./server/policies/pqc_basic.txt
