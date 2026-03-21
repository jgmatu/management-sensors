*** Settings ***
Documentation     Suite raíz: gestiona el ciclo de vida de la infraestructura
...               compartida (PostgreSQL, controller, sensor) una sola vez
...               para todas las suites hijas.
Resource          ../resources/common.resource

Suite Setup       Iniciar Infraestructura
Suite Teardown    Parar Infraestructura
