*** Settings ***
Documentation     Pipeline E2E: DB + server + controller + sensor + CLI via botan.sh bridge (telnet).
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
${CONTROLLER_CMD}     bash scripts/controller.sh
${SENSOR_CMD}         bash scripts/sensor.sh

# Telnet bridge (botan.sh abre TCP 2000 -> PTY -> botan tls_client)
${TELNET_HOST}        127.0.0.1
${TELNET_PORT}        2000
${CLI_TIMEOUT}        60s

# Botan bridge
${BOTAN_BRIDGE_CMD}  bash tests/scripts/botan.sh

*** Keywords ***
Iniciar Sistema Completo
    Iniciar Base De Datos
    Iniciar Sistema Comunicaciones

Parar Sistema Completo
    Parar Sistema Comunicaciones
    Parar Base De Datos

Preparar Caso Integración
    Log    Preparando estado previo al caso de integración (placeholder).

Iniciar Base De Datos
    Log    Iniciando PostgreSQL para la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    -l    ${PG_LOGFILE}    start
    ...    timeout=${DB_START_TIMEOUT}

Parar Base De Datos
    Log    Parando PostgreSQL al finalizar la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    stop
    ...    timeout=10s

Iniciar Sistema Comunicaciones
    Log    Lanzando server, luego botan bridge, luego controller y sensor...
    Start Process    ${SERVER_CMD}        shell=True    stdout=DEVNULL    stderr=DEVNULL
    Start Process    ${BOTAN_BRIDGE_CMD}  shell=True    stdout=DEVNULL    stderr=DEVNULL
    Start Process    ${CONTROLLER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Iniciar Sensor
    Esperar Puerto Abierto    ${TELNET_HOST}    ${TELNET_PORT}

Parar Sistema Comunicaciones
    Log    Parando todo (server/controller/sensor/puente) usando scripts/kill.sh...
    ${res}=    Run Process
    ...    bash    -lc    "scripts/kill.sh"
    ...    shell=True    timeout=30s    stdout=PIPE    stderr=PIPE
    Log    kill.sh stdout:\n${res.stdout}
    Log    kill.sh stderr:\n${res.stderr}

Iniciar Sensor
    Start Process    ${SENSOR_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Sleep    1s

Parar Sensor
    ${user}=    Get Environment Variable    USER
    Run Process
    ...    bash    -lc    "pkill -TERM -u ${user} -f 'bash scripts/sensor.sh' || true; pkill -TERM -u ${user} -f './build/sensor/sensor' || true; pkill -TERM -u ${user} -f 'build/sensor/sensor' || true; sleep 0.5; pkill -KILL -u ${user} -f 'bash scripts/sensor.sh' || true; pkill -KILL -u ${user} -f './build/sensor/sensor' || true; pkill -KILL -u ${user} -f 'build/sensor/sensor' || true"
    ...    shell=True    timeout=20s    stdout=PIPE    stderr=PIPE
    Sleep    1s

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