gdb --args ./build/server/server \
  --cert ../server/certs/server.pem \
  --key ../server/certs/server.key \
  --port 50443 \
  --policy ../server/policies/pqc_basic.txt
