/*
=============================================================================
SISTEMA:     Núcleo de datos para gestión de sensores
DESCRIPCIÓN: Esquema relacional, reactivo y orientado a consistencia
MOTOR:       PostgreSQL 16+ (RHEL 10)
=============================================================================

PRINCIPIOS ARQUITECTÓNICOS DEL MODELO
1) FUENTE DE VERDAD Y ÚNICO PUNTO DE AUTORIDAD
   - Esta base de datos es la fuente de verdad del sistema.
   - También es el único punto de autoridad para validar el estado de:
     seguridad, configuración y telemetría.
   - Ningún cambio se considera efectivo hasta quedar persistido aquí.

2) ABSTRACCIÓN DE COLA DE MENSAJES CON PERSISTENCIA
   - El modelo usa PostgreSQL como abstracción de cola de mensajes
     persistente para desacoplar API/Dispatcher y Controlador MQTT.
   - sensor_config_pending representa la intención de cambio (trabajo en cola).
   - sensor_config_errors registra fallos asíncronos sin bloquear el flujo.
   - Este enfoque evita pérdida de órdenes ante reinicios o caídas parciales.

3) MODELO REACTIVO
   - El esquema es reactivo: combina persistencia + eventos NOTIFY/LISTEN.
   - Los canales de eventos sincronizan componentes sin polling continuo:
     config_requested, config_errors, state_events, cert_events, config_events.
   - Canales actuales publicados por la BD (triggers/funciones):
     a) config_requested:
        emitido por notify_config_request() sobre sensor_config_pending
        en operaciones INSERT/UPDATE.
     b) config_errors:
        emitido por notify_config_error() sobre sensor_config_errors
        en operaciones INSERT/UPDATE/DELETE.
     c) state_events:
        emitido por notify_state_change() sobre sensor_state
        en operaciones INSERT/UPDATE.
     d) cert_events:
        emitido por notify_cert_change() sobre sensor_certs
        en operaciones INSERT/UPDATE/DELETE.
     e) config_events:
        emitido por notify_config_change() sobre sensor_config
        en operaciones INSERT/UPDATE/DELETE.

MODELO RELACIONAL PRINCIPAL
1) sensor_certs (seguridad)
   - Identidad criptográfica X.509 (PEM) de cada nodo.

2) sensor_config (realidad confirmada)
   - Estado operativo confirmado del sensor en red.
   - Relación 1:1 con sensor_certs mediante cert_id UNIQUE y NOT NULL.

3) sensor_config_pending (estado deseado)
   - Cola persistente de peticiones de configuración.
   - Relación 1:N con sensor_config.

4) sensor_config_errors (retroalimentación)
   - Historial de errores por sensor/petición para trazabilidad.
   - Relación 1:N con sensor_config.

5) sensor_state (telemetría)
   - Último estado observado por sensor (snapshot operativo).

ABSTRACCIÓN DE LÓGICA RELACIONAL (JERARQUÍA DE DATOS)
1) Capa de seguridad: sensor_certs
   - Raíz criptográfica de confianza del sistema.

2) Capa de identidad/configuración confirmada: sensor_config
   - Representa la realidad efectiva del sensor.
   - Relación 1:1 con sensor_certs (cert_id UNIQUE, NOT NULL).

3) Capa de comunicación/intención: sensor_config_pending
   - Materializa la cola persistente de peticiones.
   - Relación 1:N con sensor_config (sensor -> peticiones pendientes).

4) Capa de retroalimentación de fallos: sensor_config_errors
   - Persistencia de errores funcionales/técnicos.
   - Relación 1:N con sensor_config (sensor -> errores).
   - Relación 0..1 por request_id (una petición puede provocar un error).

5) Capa de estado operativo: sensor_state
   - Snapshot de telemetría actual por sensor.
   - Relación 1:1 con sensor_config por sensor_id.

INTEGRIDAD Y CONSISTENCIA
La integridad referencial (FKs), unicidad y reglas de borrado mantienen la
coherencia entre intención (pending), resultado (config) y observabilidad
(errors/state), reforzando el rol de la BD como núcleo transaccional del sistema.
=============================================================================
*/

-- ==========================================================
-- 1. LIMPIEZA TOTAL (DROP)
-- ==========================================================
-- Nota: no se eliminan triggers explícitamente aquí.
-- Al eliminar las tablas asociadas, PostgreSQL elimina sus triggers de forma implícita.
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
    -- Relación 1:1 con sensor_certs:
    -- cada sensor tiene un único certificado y cada certificado solo puede pertenecer a un sensor.
    cert_id        INT NOT NULL UNIQUE REFERENCES sensor_certs(cert_id) ON DELETE CASCADE,
    request_id     BIGINT
);

-- 2.3. TABLA DE CONFIGURACIÓN PENDIENTE (Órdenes de Cambio)
-- El Servidor INSERT/UPDATE aquí. El Controlador la VACÍA tras éxito.
CREATE TABLE sensor_config_pending (
    request_id          BIGINT PRIMARY KEY, -- generado por Dispatcher (aplicación)
    sensor_id           INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    requested_hostname  VARCHAR(100),
    requested_ip        INET,
    requested_is_active BOOLEAN,
    status              TEXT NOT NULL CHECK (status IN ('PENDING', 'SUCCESS', 'ERROR', 'TIMEOUT')),
    requested_at        TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at        TIMESTAMPTZ
);

CREATE INDEX idx_config_requests_sensor_id ON sensor_config_pending (sensor_id);
CREATE INDEX idx_config_requests_status ON sensor_config_pending (status);

-- 2.5. TABLA DE ERRORES (Persistencia de fallos)
-- Si el controlador falla, lo registra aquí. 
-- El Servidor puede limpiar esta tabla cuando el usuario "reintenta" o "descarta" el error.
CREATE TABLE sensor_config_errors (
    error_id        BIGSERIAL PRIMARY KEY,
    request_id      BIGINT UNIQUE REFERENCES sensor_config_pending(request_id) ON DELETE CASCADE,
    sensor_id       INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    error_code      VARCHAR(50),
    error_detail    TEXT,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_config_errors_request_id ON sensor_config_errors (request_id);
CREATE INDEX idx_config_errors_sensor_id ON sensor_config_errors (sensor_id);

-- 2.4. TABLA DE ESTADO (Telemetría Efímera)
CREATE TABLE sensor_state (
    sensor_id      INT PRIMARY KEY REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    current_temp   NUMERIC(5, 2),
    last_update    TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
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
            'hostname', OLD.hostname,
            'request_id', OLD.request_id
        );
    ELSE
        payload = jsonb_build_object(
            'action', TG_OP,
            'sensor_id', NEW.sensor_id,
            'new_ip', NEW.ip_address::text,
            'hostname', NEW.hostname,
            'request_id', NEW.request_id
        );
    END IF;

    PERFORM pg_notify('config_events', payload::text);
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION notify_state_change()
RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('state_events', json_build_object(
        'sensor_id', NEW.sensor_id,
        'temp', NEW.current_temp,
        'ts', NEW.last_update
        )::text
    );
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

-- 5. LÓGICA REACTIVA PARA SEGURIDAD (Canal: cert_events)
CREATE OR REPLACE FUNCTION notify_cert_change()
RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('cert_events', json_build_object(
            'action', TG_OP,
            'cert_id', COALESCE(NEW.cert_id, OLD.cert_id),
            'revoked', COALESCE(NEW.is_revoked, FALSE),
            'cn', COALESCE(NEW.common_name, OLD.common_name)
        )::text
    );
    RETURN NULL;
END; $$ LANGUAGE plpgsql;

-- Función para notificar nueva configuración pendiente
CREATE OR REPLACE FUNCTION notify_config_request()
RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('config_requested', json_build_object(
            'sensor_id', NEW.sensor_id,
            'hostname',  NEW.new_hostname,
            'ip_address', NEW.new_ip_address,
            'is_active', NEW.new_is_active,
            'request_id', NEW.request_id,
            'action',    TG_OP -- Returns 'INSERT' or 'UPDATE'
        )::text
    );
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Función para notificar errores de configuración
CREATE OR REPLACE FUNCTION notify_config_error()
RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('config_errors', json_build_object(
            'sensor_id', NEW.sensor_id,
            'error_code', NEW.error_code,
            'detail', NEW.error_detail,
            'timestamp', NEW.occurred_at,
            'request_id', NEW.request_id
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
AFTER INSERT OR UPDATE OR DELETE ON sensor_config_errors
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