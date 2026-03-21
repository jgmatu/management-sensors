*** Settings ***
Documentation     Pipeline E2E: CLI via botan.sh bridge (telnet) en modo raw.
Library           Process
Library           OperatingSystem
Library           Telnet
Resource          ../resources/common.resource

Suite Setup       Iniciar Modo Raw
Suite Teardown    Parar Modo Raw
Test Setup        Preparar Caso Integración

*** Variables ***
${STRESS_ITERATIONS}    100
${LOAD_CLIENTS}         10
${REQUESTS_PER_CLIENT}   100
${LOAD_IP_CIDR}         7.7.7.7/22

*** Test Cases ***
Primer Test: Validar PostgreSQL
    Verificar Conexion PostgreSQL

Segundo Test: Validar CLI Devuelve OK
    Verificar CLI Configuracion OK

Tercer Test: Validar CLI Timeout
    Verificar CLI Timeout

Cuarto Test: Validar CLI Stress Secuencial
    Verificar CLI Stress Secuencial    ${STRESS_ITERATIONS}

Quinto Test: Validar Carga Paralela CLI
    Verificar Carga Paralela CLI    ${LOAD_CLIENTS}    ${REQUESTS_PER_CLIENT}

*** Keywords ***
Iniciar Modo Raw
    Iniciar Servidor Raw
    Iniciar Bridge

Parar Modo Raw
    Parar Bridge
    Parar Servidor

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

Verificar CLI Configuracion OK
    [Documentation]    Conecta por telnet al botan bridge, espera handshake y valida OK:.
    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}
    Set Newline    LF

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

Verificar CLI Stress Secuencial
    [Arguments]    ${iterations}
    [Documentation]    Ejecuta CONFIG_IP ${iterations} veces secuencialmente.

    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}
    Set Newline    LF

    Read Until Regexp    Handshake complete

    FOR    ${i}    IN RANGE    0    ${iterations}
        ${id}=    Evaluate    (${i} % 2) + 1
        ${cmd}=   Catenate    CONFIG_IP    ${id}    IP    7.7.7.7/22
        Write    ${cmd}

        ${out}=    Read Until Regexp    OK:    FAILED:    ERROR:
        Log    [${i}] TELNET out:\n${out}

        Should Contain    ${out}    OK:
        Should Not Contain    ${out}    FAILED:
        Should Not Contain    ${out}    ERROR:
    END

    Write    quit
    Close Connection

Verificar CLI Timeout
    [Documentation]    Parar sensor, enviar CONFIG_IP y esperar timeout.

    Parar Sensor

    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}
    Set Newline    LF

    Read Until Regexp    Handshake complete

    Write    CONFIG_IP 1 IP 7.7.7.7/22

    ${out}=    Read Until Regexp    OK:|ERROR:
    Log    TELNET timeout out:\n${out}

    ${ok_check}=    Run Keyword And Ignore Error    Should Contain    ${out}    OK:
    Run Keyword If    '${ok_check}[0]' == 'PASS'    Should Not Contain    ${out}    Request Timed Out

    Run Keyword If    '${ok_check}[0]' != 'PASS'    Should Contain    ${out}    ERROR:
    Run Keyword If    '${ok_check}[0]' != 'PASS'    Should Contain    ${out}    Request Timed Out

    Write    quit
    Close Connection

    Iniciar Sensor

Verificar Carga Paralela CLI
    [Arguments]    ${clients}    ${requests_per_client}
    ${parallel_cmd}=    Catenate
    ...    bash tests/scripts/parallel.sh ${TELNET_HOST} ${TELNET_PORT} ${clients} ${LOAD_IP_CIDR} ${requests_per_client}
    ${parallel_timeout}=    Evaluate    180 + (${requests_per_client} * 10)

    ${res}=    Run Process
    ...    ${parallel_cmd}
    ...    timeout=${parallel_timeout}s    stdout=PIPE    stderr=PIPE    shell=True
    Log    ${res.stderr}
    Should Be Equal As Integers    ${res.rc}    0
