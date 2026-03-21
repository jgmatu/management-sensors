*** Settings ***
Documentation     HTTPS REST API E2E: verifica endpoints REST sobre TLS v1.3 PQC.
...               Robot envía HTTP plano al proxy C++ que reenvía por TLS PQC
...               al servidor HTTPS (puerto ${HTTPS_PORT}). API protegida con JWT
...               (usuario admin / contraseña admin, hardcoded en servidor).
Library           Process
Library           OperatingSystem
Resource          ../resources/common.resource

Suite Setup       Obtener JWT Token Admin Y Guardar En Suite
Test Setup        Preparar Caso Integración

*** Test Cases ***
API REST: Login Admin Devuelve Token
    [Documentation]    POST /api/auth/login con admin/admin devuelve JWT ES384.
    ${body}=    Set Variable    {"username":"admin","password":"admin"}
    ${resp}=    HTTPS POST    /api/auth/login    ${body}
    Should Contain    ${resp}    "status":"success"
    Should Contain    ${resp}    "token"
    Should Contain    ${resp}    "expires_in"

API REST: CONFIG_IP Sin Token Devuelve 401
    [Documentation]    POST /api/config_ip sin Bearer debe rechazarse.
    ${body}=    Set Variable    {"sensor_id":1,"ip":"1.1.1.1/32"}
    ${result}=    HTTPS POST    /api/config_ip    ${body}
    Should Contain    ${result}    Missing Authorization header

API REST: CONFIG_IP Exitoso Con Bearer
    [Documentation]    POST /api/config_ip con JWT válido; espera JSON success.
    ${body}=    Set Variable    {"sensor_id":1,"ip":"7.7.7.7/22"}

    ${result}=    HTTPS POST    /api/config_ip    ${body}    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    "status":"success"
    Should Contain    ${result}    "sensor_id":1
    Should Contain    ${result}    updated successfully

API REST: CONFIG_IP Segundo Sensor Con Bearer
    [Documentation]    POST /api/config_ip sensor_id 2 con JWT.
    ${body}=    Set Variable    {"sensor_id":2,"ip":"10.0.0.1/24"}

    ${result}=    HTTPS POST    /api/config_ip    ${body}    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    "status":"success"
    Should Contain    ${result}    "sensor_id":2

API REST: CONFIG_IP Body Inválido Con Bearer
    [Documentation]    POST /api/config_ip con JSON malformado; cuerpo indica error de parseo (HTTP 400).
    ${body}=    Set Variable    {esto no es json}

    ${result}=    HTTPS POST    /api/config_ip    ${body}    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    Invalid JSON body

API REST: CONFIG_IP Campos Faltantes Con Bearer
    [Documentation]    POST /api/config_ip sin ip; mensaje de campo faltante (HTTP 400).
    ${body}=    Set Variable    {"sensor_id":1}

    ${result}=    HTTPS POST    /api/config_ip    ${body}    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    Missing required field

API REST: Connection Details Con Bearer
    [Documentation]    GET /api/connection_details con JWT válido.

    ${result}=    HTTPS GET    /api/connection_details    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    kex_algo
    Should Contain    ${result}    is_quantum_safe

API REST: Ruta Inexistente Devuelve 404 Con Bearer
    [Documentation]    GET /api/nonexistent con JWT; cuerpo es la ruta (HTTP 404 vía curl -s sin código en body).
    ${result}=    HTTPS GET    /api/nonexistent    ${JWT_ACCESS_TOKEN}

    Should Contain    ${result}    /api/nonexistent
