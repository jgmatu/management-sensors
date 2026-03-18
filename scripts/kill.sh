user="$USER"

SLEEP_STEP=0.5

# 1) Server (wrapper + binario real)
pkill -TERM -u "$user" -f 'bash scripts/server.sh' 2>/dev/null || true
pkill -TERM -u "$user" -f 'build/server/server' 2>/dev/null || true
sleep $SLEEP_STEP
pkill -KILL -u "$user" -f 'bash scripts/server.sh' 2>/dev/null || true
pkill -KILL -u "$user" -f 'build/server/server' 2>/dev/null || true

# 2) Controller (wrapper + binario real)
pkill -TERM -u "$user" -f 'bash scripts/controller.sh' 2>/dev/null || true
pkill -TERM -u "$user" -f 'build/controller/controller' 2>/dev/null || true
sleep $SLEEP_STEP
pkill -KILL -u "$user" -f 'bash scripts/controller.sh' 2>/dev/null || true
pkill -KILL -u "$user" -f 'build/controller/controller' 2>/dev/null || true

# 3) Sensor (wrapper + binario real)
# OJO: en tu repo es scripts/sensors.sh (plural) según lo que vimos antes.
pkill -TERM -u "$user" -f 'bash scripts/sensors.sh' 2>/dev/null || true
pkill -TERM -u "$user" -f 'build/sensor/sensor' 2>/dev/null || true
sleep $SLEEP_STEP
pkill -KILL -u "$user" -f 'bash scripts/sensors.sh' 2>/dev/null || true
pkill -KILL -u "$user" -f 'build/sensor/sensor' 2>/dev/null || true

# 4) Puente telnet (socat listener 2000) + botan tls_client huérfano
pkill -TERM -u "$user" -f 'socat.*TCP-LISTEN:2000' 2>/dev/null || true
pkill -TERM -u "$user" -f 'TCP-LISTEN:2000.*botan tls_client' 2>/dev/null || true
sleep $SLEEP_STEP
pkill -KILL -u "$user" -f 'socat.*TCP-LISTEN:2000' 2>/dev/null || true

pkill -TERM -u "$user" -f 'botan tls_client' 2>/dev/null || true
sleep $SLEEP_STEP
pkill -KILL -u "$user" -f 'botan tls_client' 2>/dev/null || true

# Verifica
ss -ltnp | grep ':2000' || true
ps aux | grep -E 'build/server/server|build/controller/controller|build/sensor/sensor|socat.*TCP-LISTEN:2000|botan tls_client' | grep -v grep || true