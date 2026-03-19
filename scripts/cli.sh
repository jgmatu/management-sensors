#!/bin/bash

cd "$(dirname "$0")"

botan tls_client localhost --port=50443 \
  --policy=../server/policies/client_policies.txt \
  --trusted-cas=../server/certs/ca.pem
