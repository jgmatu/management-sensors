*** Settings ***
Documentation     Recursos compartidos para el pipeline E2E.
Library           Process
Library           OperatingSystem
Library           Telnet

*** Variables ***
${PG_CTL}             /usr/local/pgsql/bin/pg_ctl
${PG_DATA}            /usr/local/pgsql/data
${PG_LOGFILE}         logs/postgresql.log
${DB_START_TIMEOUT}   30s

${PSQL}               psql
${DB_HOST}            localhost
${DB_PORT}            5432
${DB_NAME}            javi
${DB_USER}            javi

${SERVER_BIN}         ./build/server/server
${SERVER_CERT}        ./server/certs/server.pem
${SERVER_KEY}         ./server/certs/server.key
${SERVER_POLICY}      ./server/policies/pqc_basic.txt
${SERVER_WEBROOT}     ./server/webroot

${RAW_PORT}           50443
${HTTPS_PORT}         50444

${CONTROLLER_CMD}     bash scripts/controller.sh
${SENSOR_CMD}         bash scripts/sensor.sh

${TELNET_HOST}        127.0.0.1
${TELNET_PORT}        2000
${CLI_TIMEOUT}        60s

${BOTAN_BRIDGE_CMD}   bash tests/scripts/botan.sh

${API_TIMEOUT}        60s

# Proxy C++ HTTP plano -> TLS PQC (Botan)
${PROXY_HOST}         127.0.0.1
${PROXY_PORT}         8443

*** Keywords ***
# ── Infraestructura compartida ──

Iniciar Infraestructura
    Iniciar Base De Datos
    Start Process    ${CONTROLLER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Iniciar Sensor
    Iniciar Servidor Raw
    Iniciar Servidor HTTPS
    Iniciar Bridge
    Iniciar Proxy HTTPS

Parar Infraestructura
    Parar Proxy HTTPS
    Parar Bridge
    Parar Servidores
    Parar Sensor
    Parar Controller
    Parar Base De Datos

Preparar Caso Integración
    Log    Preparando estado previo al caso de integración (placeholder).

# ── Base de datos ──

Iniciar Base De Datos
    Log    Iniciando PostgreSQL para la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    -l    ${PG_LOGFILE}    start
    ...    timeout=${DB_START_TIMEOUT}

Parar Base De Datos
    Log    Parando PostgreSQL al finalizar la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    stop
    ...    timeout=10s

# ── Servidores ──

Iniciar Servidor Raw
    Log    Lanzando servidor en modo raw (puerto ${RAW_PORT})...
    Start Process
    ...    ${SERVER_BIN}    --cert    ${SERVER_CERT}    --key    ${SERVER_KEY}
    ...    --port    ${RAW_PORT}    --mode    raw    --policy    ${SERVER_POLICY}
    ...    alias=server_raw    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    localhost    ${RAW_PORT}

Iniciar Servidor HTTPS
    Log    Lanzando servidor en modo HTTPS (puerto ${HTTPS_PORT})...
    Start Process
    ...    ${SERVER_BIN}    --cert    ${SERVER_CERT}    --key    ${SERVER_KEY}
    ...    --port    ${HTTPS_PORT}    --mode    http    --document-root    ${SERVER_WEBROOT}
    ...    --policy    ${SERVER_POLICY}
    ...    alias=server_https    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    localhost    ${HTTPS_PORT}

Parar Servidores
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'build/server/server' || true; sleep 0.3; pkill -KILL -u ${user} -f 'build/server/server' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE
    Sleep    0.5s

# ── Bridge botan (socat + tls_client) ──

Iniciar Bridge
    Log    Lanzando botan bridge (socat) -> puerto ${RAW_PORT}...
    Start Process    ${BOTAN_BRIDGE_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    ...    env:BOTAN_PORT=${RAW_PORT}
    Esperar Puerto Abierto    ${TELNET_HOST}    ${TELNET_PORT}

Parar Bridge
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'socat.*TCP-LISTEN:2000' || true; pkill -TERM -u ${user} -f 'botan tls_client' || true; sleep 0.3; pkill -KILL -u ${user} -f 'socat.*TCP-LISTEN:2000' || true; pkill -KILL -u ${user} -f 'botan tls_client' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE
    Sleep    0.5s

# ── Controller ──

Parar Controller
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'build/controller/controller' || true; sleep 0.3; pkill -KILL -u ${user} -f 'build/controller/controller' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE

# ── Sensor ──

Iniciar Sensor
    Start Process    ${SENSOR_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Sleep    1s

Parar Sensor
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'bash scripts/sensor.sh' || true; pkill -TERM -u ${user} -f 'build/sensor/sensor' || true; sleep 0.5; pkill -KILL -u ${user} -f 'bash scripts/sensor.sh' || true; pkill -KILL -u ${user} -f 'build/sensor/sensor' || true"
    ...    shell=True    timeout=20s    stdout=PIPE    stderr=PIPE
    Sleep    1s

# ── Utilidades ──

Esperar Puerto Abierto
    [Arguments]    ${host}    ${port}    ${retries}=30    ${sleep_s}=1s
    ${last_rc}=    Set Variable    1

    FOR    ${i}    IN RANGE    ${retries}
        ${cmd}=    Catenate    SEPARATOR=
        ...    cat < /dev/null > /dev/tcp/${host}/${port}

        ${res}=    Run Process
        ...    bash    -lc    ${cmd}
        ...    shell=False    stdout=PIPE    stderr=PIPE

        ${last_rc}=    Set Variable    ${res.rc}
        Exit For Loop If    ${last_rc} == 0
        Sleep    ${sleep_s}
    END

    Should Be Equal As Integers    ${last_rc}    0

# ── Proxy HTTPS (C++ HTTP plano -> TLS PQC) ──

Iniciar Proxy HTTPS
    Log    Lanzando proxy C++ HTTP->TLS PQC -> puerto ${HTTPS_PORT}...
    Start Process    bash    tests/scripts/https_proxy.sh
    ...    shell=True    stdout=DEVNULL    stderr=DEVNULL
    ...    env:PROXY_PORT=${PROXY_PORT}    env:BACKEND_PORT=${HTTPS_PORT}
    Esperar Puerto Abierto    ${PROXY_HOST}    ${PROXY_PORT}

Parar Proxy HTTPS
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'build/proxy/proxy' || true; sleep 0.3; pkill -KILL -u ${user} -f 'build/proxy/proxy' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE
    Sleep    0.5s

# ── HTTPS helpers (curl a través del proxy) ──

HTTPS POST
    [Arguments]    ${path}    ${body}
    ${url}=    Catenate    SEPARATOR=    http://${PROXY_HOST}:${PROXY_PORT}    ${path}
    Log    [DIAG] HTTPS POST ${url}
    Log    [DIAG] Body: ${body}
    Log    [DIAG] Body length: ${{len($body)}}
    ${result}=    Run Process
    ...    curl    -v    -s    -S    --max-time    30
    ...    -X    POST
    ...    -H    Content-Type: application/json
    ...    -H    Connection: close
    ...    -d    ${body}
    ...    ${url}
    ...    stdout=PIPE    stderr=PIPE    timeout=${API_TIMEOUT}
    Log    [DIAG] curl exit code: ${result.rc}
    Log    [DIAG] stdout (${${{len($result.stdout)}}} bytes):\n${result.stdout}
    Log    [DIAG] stderr (curl verbose):\n${result.stderr}
    Should Be Equal As Integers    ${result.rc}    0
    ...    curl failed with rc=${result.rc}: ${result.stderr}
    RETURN    ${result.stdout}

HTTPS GET
    [Arguments]    ${path}
    ${url}=    Catenate    SEPARATOR=    http://${PROXY_HOST}:${PROXY_PORT}    ${path}
    Log    [DIAG] HTTPS GET ${url}
    ${result}=    Run Process
    ...    curl    -v    -s    -S    --max-time    30
    ...    -H    Connection: close
    ...    ${url}
    ...    stdout=PIPE    stderr=PIPE    timeout=${API_TIMEOUT}
    Log    [DIAG] curl exit code: ${result.rc}
    Log    [DIAG] stdout (${${{len($result.stdout)}}} bytes):\n${result.stdout}
    Log    [DIAG] stderr (curl verbose):\n${result.stderr}
    Should Be Equal As Integers    ${result.rc}    0
    ...    curl failed with rc=${result.rc}: ${result.stderr}
    RETURN    ${result.stdout}
