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

${SERVER_CMD}         bash scripts/server.sh
${HTTPS_SERVER_CMD}   bash scripts/https.sh
${CONTROLLER_CMD}     bash scripts/controller.sh
${SENSOR_CMD}         bash scripts/sensor.sh

${TELNET_HOST}        127.0.0.1
${TELNET_PORT}        2000
${CLI_TIMEOUT}        60s

${BOTAN_BRIDGE_CMD}   bash tests/scripts/botan.sh

${HTTPS_HOST}         localhost
${HTTPS_PORT}         50443
${API_TIMEOUT}        60s

# Proxy C++ HTTP plano -> TLS PQC (Botan)
${HTTPS_PROXY_CMD}    bash tests/scripts/https_proxy.sh
${PROXY_HOST}         127.0.0.1
${PROXY_PORT}         8443

*** Keywords ***
# ── Infraestructura compartida (DB + controller + sensor) ──

Iniciar Infraestructura
    Iniciar Base De Datos
    Start Process    ${CONTROLLER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Iniciar Sensor

Parar Infraestructura
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

# ── Servidor (raw o http) ──

Iniciar Servidor Raw
    Log    Lanzando servidor en modo raw...
    Start Process    ${SERVER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    ${HTTPS_HOST}    ${HTTPS_PORT}

Iniciar Servidor HTTPS
    Log    Lanzando servidor en modo HTTPS...
    Start Process    ${HTTPS_SERVER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    ${HTTPS_HOST}    ${HTTPS_PORT}

Parar Servidor
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'build/server/server' || true; sleep 0.3; pkill -KILL -u ${user} -f 'build/server/server' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE
    Sleep    0.5s

# ── Bridge botan (socat + tls_client) ──

Iniciar Bridge
    Log    Lanzando botan bridge (socat)...
    Start Process    ${BOTAN_BRIDGE_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
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
    Log    Lanzando proxy C++ HTTP->TLS PQC...
    Start Process    ${HTTPS_PROXY_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    ${PROXY_HOST}    ${PROXY_PORT}

Parar Proxy HTTPS
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'build/proxy/proxy' || true; sleep 0.3; pkill -KILL -u ${user} -f 'build/proxy/proxy' || true"
    ...    shell=True    timeout=10s    stdout=PIPE    stderr=PIPE
    Sleep    0.5s

# ── Modos compuestos (servidor + proxy/bridge) ──

Iniciar Modo HTTPS
    Iniciar Servidor HTTPS
    Iniciar Proxy HTTPS

Parar Modo HTTPS
    Parar Proxy HTTPS
    Parar Servidor

# ── HTTPS helpers (curl a través del proxy) ──

HTTPS POST
    [Arguments]    ${path}    ${body}
    ${url}=    Catenate    SEPARATOR=    http://${PROXY_HOST}:${PROXY_PORT}    ${path}
    ${result}=    Run Process
    ...    curl    -s    -S    --max-time    30
    ...    -X    POST
    ...    -H    Content-Type: application/json
    ...    -H    Connection: close
    ...    -d    ${body}
    ...    ${url}
    ...    stdout=PIPE    stderr=PIPE    timeout=${API_TIMEOUT}
    Log    HTTPS POST ${path} stdout:\n${result.stdout}
    Log    HTTPS POST ${path} stderr:\n${result.stderr}
    RETURN    ${result.stdout}

HTTPS GET
    [Arguments]    ${path}
    ${url}=    Catenate    SEPARATOR=    http://${PROXY_HOST}:${PROXY_PORT}    ${path}
    ${result}=    Run Process
    ...    curl    -s    -S    --max-time    30
    ...    -H    Connection: close
    ...    ${url}
    ...    stdout=PIPE    stderr=PIPE    timeout=${API_TIMEOUT}
    Log    HTTPS GET ${path} stdout:\n${result.stdout}
    Log    HTTPS GET ${path} stderr:\n${result.stderr}
    RETURN    ${result.stdout}
