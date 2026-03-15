/* 
=============================================================================
SISTEMA:     Infraestructura de Datos para 100 CPUs (Nodos de Computación)
DESCRIPCIÓN: Base de Datos RELACIONAL, REACTIVA, SEGURA y DESACOPLADA
MOTOR:       PostgreSQL 16+ (Optimizado para RHEL 10)
=============================================================================

FUENTE DE VERDAD (SOURCE OF TRUTH):
Esta base de datos se define como el UNICO PUNTO DE AUTORIDAD del sistema. 
Cualquier cambio en la seguridad, red o telemetría solo se considera válido 
una vez que ha sido persistido y validado en este esquema. El estado maestro 
reside exclusivamente en estas tablas, eliminando configuraciones locales.

COLA DE MENSAJES CON PERSISTENCIA (MESSAGE QUEUE ARCHITECTURE):
El sistema utiliza PostgreSQL como una cola de mensajes persistente para 
desacoplar el Proceso de Servicio (API) del Controlador de Sensores (MQTT):
1. [CAPA DE COMUNICACIÓN] sensor_config_pending:
   - Actúa como "Buffer de Intenciones". El servidor deposita aquí los cambios 
     deseados sin necesidad de conocer el estado de conexión del sensor.
   - Garantiza que ninguna orden se pierda ante caídas del controlador MQTT.

2. [CAPA DE RETROALIMENTACIÓN] sensor_config_errors:
   - Almacén de fallos asíncronos. Permite al controlador reportar problemas 
     de aplicación (timeouts, rechazos) sin bloquear el flujo principal.

LÓGICA RELACIONAL (Jerarquía de Datos):
1. [CAPA DE SEGURIDAD]  sensor_certs: 
   - Raíz de confianza y gestión de identidades X.509 (Formato PEM).
   - Base de la pirámide: sin certificado válido, no existe nodo.

2. [CAPA DE IDENTIDAD]   sensor_config: 
   - Vínculo lógico-físico. Define la "Realidad Confirmada" del nodo.
   - Representa el estado actual exitoso del sensor en la red.

3. [CAPA DE COMUNICACIÓN] sensor_config_pending:
   - Capa de "Estado Deseado" (Desired State). 
   - Relación 1:1 con sensor_config que actúa como cola de comandos.
   - Solo existe si hay una discrepancia entre el Servidor y el Sensor.

4. [CAPA DE RETROALIMENTACIÓN] sensor_config_errors:
   - Registro histórico de excepciones y fallos de aplicación.
   - Relación 1:N con sensor_config para trazabilidad de problemas técnicos.
   
5. [CAPA DE ESTADO]      sensor_state: 
   - Telemetría efímera (Datos operativos).
   - Optimizado para acceso en tiempo constante (O(1)) al último valor conocido.

ARQUITECTURA REACTIVA (Mecanismo NOTIFY/LISTEN):
La base de datos emite notificaciones en canales aislados para sincronización 
inmediata, eliminando la necesidad de consultas constantes (polling):

- config_requested: Aviso al Controlador MQTT de que hay trabajo pendiente en 'pending'.
- config_confirmed: Aviso al Servidor de que la configuración se aplicó con éxito 
                    (Emitido tras actualizar 'sensor_config' y limpiar 'pending').
- config_errors:    Aviso al Servidor de que una configuración ha fallado.
- state_events:     Actualizaciones de telemetría en tiempo real.
- cert_events:      Eventos de seguridad y revocación de identidades.

NOTA FINAL SOBRE INTEGRIDAD:
Implementa integridad referencial estricta. El borrado en cascada (ON DELETE CASCADE) 
y la limpieza atómica de la tabla 'pending' tras el éxito aseguran que el sistema 
nunca mantenga estados incoherentes entre la intención y la realidad.
=============================================================================
*/

-- ==========================================================
-- 1. LIMPIEZA TOTAL (DROP)
-- ==========================================================
-- Borrar Triggers y Funciones
DROP TRIGGER IF EXISTS trg_sensor_cert_notify ON sensor_certs;
DROP TRIGGER IF EXISTS trg_sensor_config_notify ON sensor_config;
DROP TRIGGER IF EXISTS trg_sensor_state_notify ON sensor_state;
DROP TRIGGER IF EXISTS trg_notify_config_error ON sensor_config_errors;
DROP TRIGGER IF EXISTS trg_notify_config_pending ON sensor_config_pending;
-- ==========================================================
-- 2. BORRADO DE TABLAS (Orden inverso por Foreign Keys)
-- ==========================================================
-- Tablas de comunicación y estado (Dependientes)
DROP TABLE IF EXISTS sensor_config_errors;
DROP TABLE IF EXISTS sensor_config_pending;
DROP TABLE IF EXISTS sensor_state;

-- Tablas base
DROP TABLE IF EXISTS sensor_config;
DROP TABLE IF EXISTS sensor_certs CASCADE;

-- ==========================================================
-- 2. ESTRUCTURA DE TABLAS (DDL)
-- ==========================================================
-- 2.1. TABLA DE CERTIFICADOS X509 (Identidad de los 100 CPUs)
CREATE TABLE sensor_certs (
    cert_id        SERIAL PRIMARY KEY,
    fingerprint    VARCHAR(64) UNIQUE NOT NULL, -- SHA256 del certificado
    common_name    VARCHAR(255),
    not_after      TIMESTAMP WITH TIME ZONE,
    is_revoked     BOOLEAN DEFAULT FALSE,
    pem_data       TEXT NOT NULL                -- Certificado en formato PEM
);

-- En tu tabla actual, el tipo TEXT es correcto. 
-- PostgreSQL maneja TEXT de forma eficiente (TOAST) fuera de la fila principal 
-- si el tamaño excede los 2KB, así que no ralentiza las búsquedas por ID.
ALTER TABLE sensor_certs ALTER COLUMN pem_data SET STORAGE EXTERNAL;

-- 2.2. TABLA DE CONFIGURACIÓN (Vínculo con Seguridad)
CREATE TABLE sensor_config (
    sensor_id      INT PRIMARY KEY,
    hostname       VARCHAR(100) NOT NULL,
    ip_address     INET NOT NULL,
    is_active      BOOLEAN DEFAULT TRUE,
    cert_id        INT REFERENCES sensor_certs(cert_id) ON DELETE SET NULL
);

-- 2.3. TABLA DE ESTADO (Telemetría Efímera)
CREATE TABLE sensor_state (
    sensor_id      INT PRIMARY KEY REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    current_temp   NUMERIC(5, 2),
    last_update    TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 3.1. TABLA DE CONFIGURACIÓN PENDIENTE (Órdenes de Cambio)
-- El Servidor INSERT/UPDATE aquí. El Controlador la VACÍA tras éxito.
CREATE TABLE sensor_config_pending (
    sensor_id      INT PRIMARY KEY REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    new_hostname   VARCHAR(100),
    new_ip_address INET,
    new_is_active  BOOLEAN,
    requested_at   TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- 3.2. TABLA DE ERRORES (Persistencia de fallos)
-- Si el controlador falla, lo registra aquí. 
-- El Servidor puede limpiar esta tabla cuando el usuario "reintenta" o "descarta" el error.
CREATE TABLE sensor_config_errors (
    error_id       SERIAL PRIMARY KEY,
    sensor_id      INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    error_code     VARCHAR(50), 
    error_detail   TEXT,
    failed_config  JSONB, -- Guardamos aquí lo que se intentó para no perder el dato al borrar la pendiente
    occurred_at    TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- ==========================================================
-- 3. LÓGICA DE NOTIFICACIONES (TRIGGERS)
-- ==========================================================
-- Función de Notificación Robusta para Configuración
CREATE OR REPLACE FUNCTION notify_config_change()
RETURNS TRIGGER AS $$
DECLARE
    payload JSONB;
BEGIN
    -- Si es DELETE, usamos OLD. Si es INSERT/UPDATE, usamos NEW.
    IF (TG_OP = 'DELETE') THEN
        payload = jsonb_build_object(
            'action', 'DELETE',
            'sensor_id', OLD.sensor_id,
            'hostname', OLD.hostname
        );
    ELSE
        payload = jsonb_build_object(
            'action', TG_OP,
            'sensor_id', NEW.sensor_id,
            'new_ip', NEW.ip_address::text,
            'hostname', NEW.hostname
        );
    END IF;

    PERFORM pg_notify('config_events', payload::text);
    RETURN NULL; -- En AFTER triggers el retorno da igual, pero mejor ser explícitos
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION notify_state_change() RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('state_events', json_build_object(
        'sensor_id', NEW.sensor_id, 'temp', NEW.current_temp, 'ts', NEW.last_update
    )::text);
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- 5. LÓGICA REACTIVA PARA SEGURIDAD (Canal: cert_events)
CREATE OR REPLACE FUNCTION notify_cert_change() RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('cert_events', json_build_object(
        'action', TG_OP,
        'cert_id', COALESCE(NEW.cert_id, OLD.cert_id),
        'revoked', COALESCE(NEW.is_revoked, FALSE),
        'cn', COALESCE(NEW.common_name, OLD.common_name)
    )::text);
    RETURN NULL;
END; $$ LANGUAGE plpgsql;

-- Función para notificar nueva configuración pendiente
CREATE OR REPLACE FUNCTION notify_config_request() RETURNS TRIGGER AS $$
BEGIN
    -- Enviamos el ID del sensor y el payload en formato JSON
    PERFORM pg_notify(
        'config_requested', 
        json_build_object(
            'sensor_id', NEW.sensor_id,
            'hostname', NEW.new_hostname,
            'ip_address', NEW.new_ip_address,
            'is_active', NEW.new_is_active,
            'action', 'UPDATE_CONFIG'
        )::text
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Función para notificar errores de configuración
CREATE OR REPLACE FUNCTION notify_config_error() RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify(
        'config_errors', 
        json_build_object(
            'sensor_id', NEW.sensor_id,
            'error_code', NEW.error_code,
            'detail', NEW.error_detail,
            'timestamp', NEW.occurred_at
        )::text
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- 6. TRIGGERS

-- Notifica cambios en los Certificados
CREATE TRIGGER trg_sensor_cert_notify 
AFTER INSERT OR UPDATE OR DELETE ON sensor_certs
FOR EACH ROW EXECUTE FUNCTION notify_cert_change();

-- Notifica cambios en la Configuración
CREATE TRIGGER trg_sensor_config_notify 
AFTER INSERT OR UPDATE OR DELETE ON sensor_config
FOR EACH ROW EXECUTE FUNCTION notify_config_change();

-- Trigger asociado a la tabla de configuraciones pendientes
CREATE TRIGGER trg_notify_config_pending
AFTER INSERT OR UPDATE ON sensor_config_pending
FOR EACH ROW EXECUTE FUNCTION notify_config_request();

-- Trigger asociado a la tabla de errores
CREATE TRIGGER trg_notify_config_error
AFTER INSERT ON sensor_config_errors
FOR EACH ROW EXECUTE FUNCTION notify_config_error();

-- Notifica cambios en la Telemetría
CREATE TRIGGER trg_sensor_state_notify 
AFTER INSERT OR UPDATE ON sensor_state
FOR EACH ROW EXECUTE FUNCTION notify_state_change();

-- ==========================================================
-- 7. OPERACIONES DE PRUEBA (DML)
-- ==========================================================

-- A. PRUEBA DE SEGURIDAD (Dispara 'cert_events')
-- Creamos las identidades para nuestros primeros nodos
INSERT INTO sensor_certs (fingerprint, common_name, not_after, pem_data) VALUES 
('hash_node_01_abc', 'cpu-node-01.pqc.internal', '2030-01-01', '-----BEGIN CERTIFICATE----- NODE 1 -----END CERTIFICATE-----'),
('hash_node_02_xyz', 'cpu-node-02.pqc.internal', '2030-01-01', '-----BEGIN CERTIFICATE----- NODE 2 -----END CERTIFICATE-----'),
('hash_node_03_123', 'cpu-node-03.pqc.internal', '2030-01-01', '-----BEGIN CERTIFICATE----- NODE 3 -----END CERTIFICATE-----');

-- B. PRUEBA DE CONFIGURACIÓN (Dispara 'config_events')
-- Insertamos los nodos vinculándolos a su certificado correspondiente
INSERT INTO sensor_config (sensor_id, hostname, ip_address, cert_id) VALUES 
(1, 'cpu-node-01', '192.168.1.101', 1),
(2, 'cpu-node-02', '192.168.1.102', 2),
(3, 'cpu-node-03', '192.168.1.103', 3);

-- Simulamos un cambio de red para el nodo 1
UPDATE sensor_config SET ip_address = '10.0.0.15' WHERE sensor_id = 1;

-- C. PRUEBA DE ESTADO / TELEMETRÍA (Dispara 'state_events')
-- Usamos UPSERT para registrar las primeras temperaturas
INSERT INTO sensor_state (sensor_id, current_temp) VALUES (1, 35.4)
ON CONFLICT (sensor_id) DO UPDATE SET current_temp = EXCLUDED.current_temp, last_update = NOW();

INSERT INTO sensor_state (sensor_id, current_temp) VALUES (2, 42.1)
ON CONFLICT (sensor_id) DO UPDATE SET current_temp = EXCLUDED.current_temp, last_update = NOW();

-- D. PRUEBA DE REVOCACIÓN (Seguridad Reactiva)
-- Si marcamos el certificado como revocado, el trigger 'cert_events' avisará al Broker
UPDATE sensor_certs SET is_revoked = TRUE WHERE cert_id = 1;


-- ==========================================================
-- 2. TEST: Server requests a configuration change
-- ==========================================================
INSERT INTO sensor_config_pending (sensor_id, new_hostname, new_ip_address, new_is_active)
VALUES (1, 'sensor-living-01-REVISED', '192.168.1.55', TRUE);

-- CHECK 1: The "pending" state should exist
SELECT * FROM sensor_config_pending WHERE sensor_id = 1;

-- ==========================================================
-- 3. TEST: MQTT Controller reports a failure (Sanity of Error Table)
-- ==========================================================
INSERT INTO sensor_config_errors (sensor_id, error_code, error_detail, failed_config)
VALUES (2, 'MQTT_TIMEOUT', 'Sensor did not respond to PUB message within 5s', 
       (SELECT row_to_json(p) FROM sensor_config_pending p WHERE sensor_id = 2));

-- ==========================================================
-- 8. CONSULTA DE VERIFICACIÓN FINAL (Con Seguridad)
-- ==========================================================

SELECT 
    c.sensor_id AS "ID",
    c.hostname AS "Host",
    c.ip_address AS "IP",
    COALESCE(s.current_temp::text, 'N/A') AS "Temp (ºC)",
    cert.is_revoked AS "Revocado",
    cert.common_name AS "CN Certificado",
    CASE WHEN c.is_active THEN 'ACTIVO' ELSE 'INACTIVO' END AS "Estado"
FROM sensor_config c
LEFT JOIN sensor_state s ON c.sensor_id = s.sensor_id
LEFT JOIN sensor_certs cert ON c.cert_id = cert.cert_id
ORDER BY c.sensor_id ASC;

SELECT 
    c.sensor_id AS "ID",
    c.hostname AS "Host Real",
    -- Mostramos la IP nueva si hay una pendiente, resaltando el cambio
    CASE 
        WHEN p.new_ip_address IS NOT NULL THEN c.ip_address::text || ' -> ' || p.new_ip_address::text
        ELSE c.ip_address::text 
    END AS "IP (Actual -> Nueva)",
    
    COALESCE(s.current_temp::text, 'N/A') AS "Temp (ºC)",
    
    -- Estado de la comunicación
    CASE 
        WHEN p.sensor_id IS NOT NULL THEN '⏳ PENDIENTE'
        WHEN e.sensor_id IS NOT NULL THEN '❌ ERROR'
        ELSE '✅ SINCRO'
    END AS "Sincronización",

    -- Detalle del último error si existe
    COALESCE(e.error_code, 'Ninguno') AS "Último Error",
    
    cert.is_revoked AS "Revocado",
    CASE WHEN c.is_active THEN 'ACTIVO' ELSE 'INACTIVO' END AS "Estado Global"

FROM sensor_config c
LEFT JOIN sensor_state s ON c.sensor_id = s.sensor_id
LEFT JOIN sensor_certs cert ON c.cert_id = cert.cert_id
-- Unimos con la tabla de pendientes para saber si hay trabajo para el MQTT
LEFT JOIN sensor_config_pending p ON c.sensor_id = p.sensor_id
-- Unimos con el error más reciente para no duplicar filas si hay muchos errores
LEFT JOIN (
    SELECT DISTINCT ON (sensor_id) sensor_id, error_code 
    FROM sensor_config_errors 
    ORDER BY sensor_id, occurred_at DESC
) e ON c.sensor_id = e.sensor_id

ORDER BY c.sensor_id ASC;