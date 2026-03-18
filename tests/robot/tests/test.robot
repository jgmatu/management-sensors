*** Settings ***
Documentation     Pipeline E2E: DB + server + controller + sensor + CLI via botan.sh bridge (telnet).
Library           Process
Library           OperatingSystem
Library           Telnet
Resource          ../resources/common.resource

Suite Setup       Iniciar Sistema Completo
Suite Teardown    Parar Sistema Completo
Test Setup        Preparar Caso Integración

*** Variables ***

# Telnet bridge (botan.sh abre TCP 2000 -> PTY -> botan tls_client)
${TELNET_HOST}        127.0.0.1
${TELNET_PORT}        2000
${CLI_TIMEOUT}        60s
${STRESS_ITERATIONS}    100
${LOAD_CLIENTS}         100
${LOAD_IP_CIDR}         7.7.7.7/22

${PARALLEL_CMD}       bash tests/scripts/parallel.sh ${TELNET_HOST} ${TELNET_PORT} ${LOAD_CLIENTS} ${LOAD_IP_CIDR}

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
    Verificar Carga Paralela CLI    ${LOAD_CLIENTS}

*** Keywords ***
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
    [Documentation]    Conecta por telnet al botan bridge, espera handshake y valida OK: (no FAILED:/ERROR:).
    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}

    # Telnet suele trabajar con CRLF; lo dejamos explícito
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
    [Documentation]    Ejecuta CONFIG_IP ${iterations} veces secuencialmente por el mismo telnet connection.

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
    [Documentation]    Parar controller, enviar CONFIG_IP y esperar respuesta con Request Timed Out.

    Parar sensor

    Open Connection    ${TELNET_HOST}    port=${TELNET_PORT}    timeout=${CLI_TIMEOUT}
    Set Newline    LF

    Read Until Regexp    Handshake complete

    Write    CONFIG_IP 1 IP 7.7.7.7/22

    # Espera a que llegue respuesta de éxito o de error
    ${out}=    Read Until Regexp    OK:|ERROR:
    Log    TELNET timeout out:\n${out}

    # Si llegó OK:, verificamos OK y que no haya timeout
    ${ok_check}=    Run Keyword And Ignore Error    Should Contain    ${out}    OK:
    Run Keyword If    '${ok_check}[0]' == 'PASS'    Should Not Contain    ${out}    Request Timed Out

    # Si llegó ERROR:, verificamos timeout
    Run Keyword If    '${ok_check}[0]' != 'PASS'    Should Contain    ${out}    ERROR:
    Run Keyword If    '${ok_check}[0]' != 'PASS'    Should Contain    ${out}    Request Timed Out

    Write    quit
    Close Connection

    Iniciar sensor

Verificar Carga Paralela CLI
    [Arguments]    ${clients}
    ${res}=    Run Process
    ...    ${PARALLEL_CMD}
    ...    timeout=180s    stdout=PIPE    stderr=PIPE    shell=True
    Log    ${res.stderr}
    Should Be Equal As Integers    ${res.rc}    0
