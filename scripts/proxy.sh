#!/bin/bash
cd "$(dirname "$0")"

./build/proxy/proxy <listen_port> <server_cert.pem> <server_key.pem> <backend_host:port>
