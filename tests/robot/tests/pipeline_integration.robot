*** Settings ***
Documentation     Pipeline E2E: DB + server + controller + sensor + CLI via botan.sh bridge (telnet).
Library           Process
Library           OperatingSystem
Library           Telnet

Suite Setup       Iniciar Sistema Completo
Suite Teardown    Parar Sistema Completo
Test Setup        Preparar Caso Integración

*** Variables ***
${PG_CTL}             /usr/local/pgsql/bin/pg_ctl
${PG_DATA}            /usr/local/pgsql/data
${PG_LOGFILE}         logfile
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

*** Test Cases ***
Primer Test: CLI Devuelve OK
    Verificar CLI Configuracion OK

*** Keywords ***
Iniciar Sistema Completo
    Log    === INICIO SUITE INTEGRACION ===
    Matar Procesos Sistema Si Existen
    Iniciar Base De Datos
    Verificar Conexion PostgreSQL
    Iniciar Sistema Comunicaciones

Parar Sistema Completo
    Log    === FIN SUITE INTEGRACION ===
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

Verificar Conexion PostgreSQL
    Log    Verificando conexión a PostgreSQL...
    ${query}=    Set Variable    SELECT 1 AS connectivity_ok;
    ${result}=    Run Process
    ...    ${PSQL}
    ...    -h    ${DB_HOST}
    ...    -p    ${DB_PORT}
    ...    -d    ${DB_NAME}
    ...    -U    ${DB_USER}
    ...    -c    ${query}
    ...    stdout=PIPE    stderr=PIPE    shell=True
    Log    psql stdout: ${result.stdout}
    Log    psql stderr: ${result.stderr}
    Should Be Equal As Integers    ${result.rc}    0

Matar Procesos Sistema Si Existen
    [Documentation]    Mata instancias previas de server.sh, controller.sh, sensor.sh y botan.sh del usuario actual.
    ${user}=    Get Environment Variable    USER
    Log    Matando procesos previos para usuario ${user}...

    ${res}=    Run Process    pkill -u ${user} -f scripts/server.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill server.sh rc=${res.rc}

    ${res}=    Run Process    pkill -u ${user} -f scripts/controller.sh    shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill controller.sh rc=${res.rc}

    ${res}=    Run Process    pkill -u ${user} -f scripts/sensor.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill sensor.sh rc=${res.rc}

    ${res}=    Run Process    pkill -u ${user} -f tests/scripts/botan.sh    shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill botan.sh rc=${res.rc}

Iniciar Sistema Comunicaciones
    Log    Lanzando server, luego botan bridge, luego controller y sensor...
    Start Process    ${SERVER_CMD}        shell=True    stdout=DEVNULL    stderr=DEVNULL
    Sleep    3s

    Start Process    ${BOTAN_BRIDGE_CMD}  shell=True    stdout=DEVNULL    stderr=DEVNULL
    Esperar Puerto Abierto    ${TELNET_HOST}    ${TELNET_PORT}

    Start Process    ${CONTROLLER_CMD}    shell=True    stdout=DEVNULL    stderr=DEVNULL
    Start Process    ${SENSOR_CMD}        shell=True    stdout=DEVNULL    stderr=DEVNULL
    Sleep    3s

Parar Sistema Comunicaciones
    Log    Parando server, controller, sensor y botan bridge...
    ${user}=    Get Environment Variable    USER

    Run Process    pkill -u ${user} -f scripts/server.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Run Process    pkill -u ${user} -f scripts/controller.sh    shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Run Process    pkill -u ${user} -f scripts/sensor.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE

    Run Process    pkill -u ${user} -f tests/scripts/botan.sh   shell=True    timeout=5s    stdout=PIPE    stderr=PIPE

    # Extra: matar el listener telnet del puente si queda vivo
    Run Process    pkill -u ${user} -f "TCP-LISTEN:${TELNET_PORT}" shell=True timeout=5s stdout=PIPE stderr=PIPE

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

Verificar CLI Configuracion OK
    [Documentation]    Conecta por telnet al botan bridge, espera handshake y valida OK: (no FAILED:/ERROR:).
    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}

    # Telnet suele trabajar con CRLF; lo dejamos explícito
    Set Newline    CRLF

    Read Until Regexp    Handshake complete

    Write    CONFIG_IP 1 IP 7.7.7.7/22
    ${out1}=    Read Until Regexp    OK:    FAILED:    ERROR:
    Log    TELNET out1:\n${out1}
    Should Contain    ${out1}    OK:
    Should Not Contain    ${out1}    FAILED:
    Should Not Contain    ${out1}    ERROR:

    Write    CONFIG_IP 2 IP 0.0.0.0/24
    ${out2}=    Read Until Regexp    OK:    FAILED:    ERROR:
    Log    TELNET out2:\n${out2}
    Should Contain    ${out2}    OK:
    Should Not Contain    ${out2}    FAILED:
    Should Not Contain    ${out2}    ERROR:

    Write    quit
    Close Connection