*** Settings ***
Documentation     Pipeline E2E: DB + server + controller + sensor + CLI.
Library           Process
Library           OperatingSystem

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
${CLI_TEST_CMD}       bash server/scripts/cli_test.sh
${CLI_TIMEOUT}        60s

*** Test Cases ***
Primer Test: CLI Devuelve OK
    Verificar CLI Configuracion OK

# Aquí podrás añadir más tests E2E posteriormente

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
    ${query}=    Set Variable    SELECT now() AS server_time, version() AS postgres_version, inet_server_addr() AS server_ip, inet_server_port() AS server_port, pg_is_in_recovery() AS in_recovery, current_setting('server_version_num') AS version_num;
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
    [Documentation]    Mata instancias previas de server.sh, controller.sh y sensor.sh del usuario actual.
    ${user}=    Get Environment Variable    USER
    Log    Matando procesos previos para usuario ${user}...

    ${res}=    Run Process    pkill -u ${user} -f scripts/server.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill server.sh rc=${res.rc} stderr=${res.stderr}

    ${res}=    Run Process    pkill -u ${user} -f scripts/controller.sh    shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill controller.sh rc=${res.rc} stderr=${res.stderr}

    ${res}=    Run Process    pkill -u ${user} -f scripts/sensor.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Log    pkill sensor.sh rc=${res.rc} stderr=${res.stderr}

Iniciar Sistema Comunicaciones
    Log    Lanzando server, controller y sensor desde scripts .sh...
    Start Process    ${SERVER_CMD}        shell=True    stdout=PIPE    stderr=PIPE
    Start Process    ${CONTROLLER_CMD}    shell=True    stdout=PIPE    stderr=PIPE
    Start Process    ${SENSOR_CMD}        shell=True    stdout=PIPE    stderr=PIPE
    Sleep    3s

Parar Sistema Comunicaciones
    Log    Parando server, controller y sensor...
    ${user}=    Get Environment Variable    USER
    Run Process    pkill -u ${user} -f scripts/server.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Run Process    pkill -u ${user} -f scripts/controller.sh    shell=True    timeout=5s    stdout=PIPE    stderr=PIPE
    Run Process    pkill -u ${user} -f scripts/sensor.sh        shell=True    timeout=5s    stdout=PIPE    stderr=PIPE

Verificar CLI Configuracion OK
    [Documentation]    Ejecuta server/scripts/cli_test.sh y valida que devuelve OK: y no FAILED:/ERROR:.
    ${result}=    Run Process
    ...    ${CLI_TEST_CMD}
    ...    shell=True
    ...    stdout=PIPE    stderr=PIPE
    ...    timeout=${CLI_TIMEOUT}
    Log    CLI stdout:\n${result.stdout}
    Log    CLI stderr:\n${result.stderr}

    Should Be Equal As Integers    ${result.rc}    0
    Should Contain    ${result.stdout}    OK:
    Should Not Contain    ${result.stdout}    FAILED:
    Should Not Contain    ${result.stdout}    ERROR: