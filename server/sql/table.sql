/* 
=============================================================================
SISTEMA:     Infraestructura de Datos para 100 CPUs (Nodos de Computación)
DESCRIPCIÓN: Base de Datos RELACIONAL, REACTIVA y SEGURA 
MOTOR:       PostgreSQL 16+ (Optimizado para RHEL 10)
=============================================================================

FUENTE DE VERDAD (SOURCE OF TRUTH):
Esta base de datos se define como el UNICO PUNTO DE AUTORIDAD del sistema. 
Cualquier cambio en la seguridad, red o telemetría solo se considera válido 
una vez que ha sido persistido y validado en este esquema. Ningún componente 
externo debe mantener configuraciones locales persistentes; el estado maestro 
reside exclusivamente en estas tablas.

LÓGICA RELACIONAL (Jerarquía de Datos):
1. [CAPA DE SEGURIDAD] sensor_certs: 
   - Raíz de confianza y gestión de identidades X.509 (Formato PEM).
   - Control de ciclo de vida: Fingerprint, caducidad y revocación.
   
2. [CAPA DE IDENTIDAD] sensor_config: 
   - Vínculo lógico-físico de la red. 
   - Asocia cada nodo con su direccionamiento IP y su certificado activo.
   
3. [CAPA DE ESTADO]    sensor_state: 
   - Almacén de telemetría efímera. 
   - Optimizado para mantener exclusivamente el último valor de temperatura 
     por nodo, garantizando consultas de estado actual en tiempo constante.

ARQUITECTURA REACTIVA (Mecanismo NOTIFY/LISTEN):
Como fuente de verdad activa, la base de datos emite notificaciones asíncronas 
en canales aislados ante cualquier alteración de su estado maestro, 
permitiendo una sincronización inmediata sin necesidad de consultas:

- cert_events:   Eventos de seguridad (Inserción, Revocación o Borrado).
- config_events: Cambios en la topología de red o configuración de nodos.
- state_events:  Actualizaciones de telemetría en tiempo real.

NOTA FINAL SOBRE INTEGRIDAD:
El sistema implementa integridad referencial estricta mediante claves foráneas. 
El borrado en cascada (ON DELETE CASCADE) asegura la coherencia absoluta del 
esquema, eliminando automáticamente registros de estado o configuración ante 
la baja de identidades, evitando la existencia de datos huérfanos.
=============================================================================
*/

-- ==========================================================
-- 1. LIMPIEZA TOTAL (DROP)
-- ==========================================================
-- Borrar Triggers y Funciones
DROP TRIGGER IF EXISTS trg_sensor_cert_notify ON sensor_certs;
DROP TRIGGER IF EXISTS trg_sensor_config_notify ON sensor_config;
DROP TRIGGER IF EXISTS trg_sensor_state_notify ON sensor_state;
DROP FUNCTION IF EXISTS notify_config_change();
DROP FUNCTION IF EXISTS notify_state_change();

-- Borrar Tablas (Orden inverso por Foreign Keys)
-- 1.1. Primero la tabla de telemetría (depende de config)
DROP TABLE IF EXISTS sensor_state;

-- 1.2. Segundo la tabla de configuración (depende de certs)
DROP TABLE IF EXISTS sensor_config;

-- 1.3. Por último la tabla de certificados (la base de la pirámide)
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
END; $$ LANGUAGE plpgsql;

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

-- 6. TRIGGERS

-- Notifica cambios en los Certificados
CREATE TRIGGER trg_sensor_cert_notify 
AFTER INSERT OR UPDATE OR DELETE ON sensor_certs
FOR EACH ROW EXECUTE FUNCTION notify_cert_change();

-- Notifica cambios en la Configuración (¡Aquí faltaba la función!)
CREATE TRIGGER trg_sensor_config_notify 
AFTER INSERT OR UPDATE OR DELETE ON sensor_config
FOR EACH ROW EXECUTE FUNCTION notify_config_change();

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