*** Settings ***
Documentation     Pruebas de pipeline E2E con PostgreSQL: hola mundo + conexión.
Library           Process

Suite Setup       Iniciar Base De Datos
Suite Teardown    Parar Base De Datos
Test Setup        Preparar Estado De Prueba

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

*** Test Cases ***
Hola Mundo Con Base De Datos
    Log    Ejecutando prueba de hola mundo con PostgreSQL levantado.
    Should Be Equal    1    1

*** Keywords ***
Iniciar Base De Datos
    Log    Iniciando PostgreSQL para la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    -l    ${PG_LOGFILE}    start
    ...    timeout=${DB_START_TIMEOUT}

Parar Base De Datos
    Log    Parando PostgreSQL al finalizar la suite...
    Run Process    ${PG_CTL}    -D    ${PG_DATA}    stop
    ...    timeout=10s

Preparar Estado De Prueba
    Log    Preparando estado de prueba y verificando conexión a PostgreSQL...
    Verificar Conexion PostgreSQL

Verificar Conexion PostgreSQL
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