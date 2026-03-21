*** Settings ***
Documentation     HTTPS REST API E2E: verifica endpoints REST sobre TLS v1.3 PQC.
...               Robot envía HTTP plano al proxy C++ que reenvía por TLS PQC
...               al servidor HTTPS (puerto ${HTTPS_PORT}).
Library           Process
Library           OperatingSystem
Resource          ../resources/common.resource

Test Setup        Preparar Caso Integración

*** Test Cases ***
API REST: CONFIG_IP Exitoso
    [Documentation]    POST /api/config_ip con sensor_id y ip válidos.
    ...                Espera respuesta JSON con status success.
    [Tags]    skip
    ${body}=    Set Variable    {"sensor_id":1,"ip":"7.7.7.7/22"}

    ${result}=    HTTPS POST    /api/config_ip    ${body}

    Should Contain    ${result}    "status":"success"
    Should Contain    ${result}    "sensor_id":1
    Should Contain    ${result}    updated successfully

API REST: CONFIG_IP Segundo Sensor
    [Documentation]    POST /api/config_ip para sensor_id 2.
    [Tags]    skip
    ${body}=    Set Variable    {"sensor_id":2,"ip":"10.0.0.1/24"}

    ${result}=    HTTPS POST    /api/config_ip    ${body}

    Should Contain    ${result}    "status":"success"
    Should Contain    ${result}    "sensor_id":2

API REST: CONFIG_IP Body Inválido
    [Documentation]    POST /api/config_ip con JSON malformado devuelve 400.
    [Tags]    skip
    ${body}=    Set Variable    {esto no es json}

    ${result}=    HTTPS POST    /api/config_ip    ${body}

    Should Contain    ${result}    400

API REST: CONFIG_IP Campos Faltantes
    [Documentation]    POST /api/config_ip sin el campo ip devuelve 400.
    [Tags]    skip
    ${body}=    Set Variable    {"sensor_id":1}

    ${result}=    HTTPS POST    /api/config_ip    ${body}

    Should Contain    ${result}    400

API REST: Connection Details
    [Documentation]    GET /api/connection_details devuelve info de la conexión TLS.
    [Tags]    skip

    ${result}=    HTTPS GET    /api/connection_details

    Should Contain    ${result}    kex_algo
    Should Contain    ${result}    is_quantum_safe

API REST: Ruta Inexistente Devuelve 404
    [Documentation]    GET /api/nonexistent devuelve 404.
    [Tags]    skip

    ${result}=    HTTPS GET    /api/nonexistent

    Should Contain    ${result}    404
