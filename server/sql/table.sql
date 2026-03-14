-- ==========================================================
-- 1. LIMPIEZA TOTAL (DROP)
-- ==========================================================
-- Borrar Triggers y Funciones
DROP TRIGGER IF EXISTS trg_sensor_config_notify ON sensor_config;
DROP TRIGGER IF EXISTS trg_sensor_state_notify ON sensor_state;
DROP FUNCTION IF EXISTS notify_config_change();
DROP FUNCTION IF EXISTS notify_state_change();

-- Borrar Tablas (Orden inverso por Foreign Keys)
DROP TABLE IF EXISTS sensor_state;
DROP TABLE IF EXISTS sensor_config;

-- ==========================================================
-- 2. ESTRUCTURA DE TABLAS (DDL)
-- ==========================================================
-- Tabla de CONFIGURACIÓN (Canal: config_events)
CREATE TABLE sensor_config (
    sensor_id      INT PRIMARY KEY,
    hostname       VARCHAR(100) NOT NULL,
    ip_address     INET NOT NULL,
    port           INT DEFAULT 1883,
    is_active      BOOLEAN DEFAULT TRUE
);

-- Tabla de ESTADO ACTUAL (Canal: state_events)
CREATE TABLE sensor_state (
    sensor_id      INT PRIMARY KEY REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    current_temp   NUMERIC(5, 2),
    last_update    TIMESTAMP WITH TIME ZONE DEFAULT CURRENT_TIMESTAMP
);

-- ==========================================================
-- 3. LÓGICA DE NOTIFICACIONES (TRIGGERS)
-- ==========================================================
CREATE OR REPLACE FUNCTION notify_config_change() RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('config_events', json_build_object(
        'action', TG_OP, 'sensor_id', NEW.sensor_id, 
        'new_ip', NEW.ip_address::text, 'hostname', NEW.hostname
    )::text);
    RETURN NEW;
END; $$ LANGUAGE plpgsql;

CREATE OR REPLACE FUNCTION notify_state_change() RETURNS TRIGGER AS $$
BEGIN
    PERFORM pg_notify('state_events', json_build_object(
        'sensor_id', NEW.sensor_id, 'temp', NEW.current_temp, 'ts', NEW.last_update
    )::text);
    RETURN NEW;
END; $$ LANGUAGE plpgsql;

CREATE TRIGGER trg_sensor_config_notify AFTER INSERT OR UPDATE ON sensor_config
FOR EACH ROW EXECUTE FUNCTION notify_config_change();

CREATE TRIGGER trg_sensor_state_notify AFTER INSERT OR UPDATE ON sensor_state
FOR EACH ROW EXECUTE FUNCTION notify_state_change();

-- ==========================================================
-- 4. OPERACIONES DE PRUEBA (DML)
-- ==========================================================

-- A. PRUEBA DE CONFIGURACIÓN (Dispara 'config_events')
-- Insertar los primeros 3 nodos de tus 100 CPUs
INSERT INTO sensor_config (sensor_id, hostname, ip_address) VALUES 
(1, 'cpu-node-01', '192.168.1.101'),
(2, 'cpu-node-02', '192.168.1.102'),
(3, 'cpu-node-03', '192.168.1.103');

-- Actualizar IP de un nodo (Simula reconfiguración de red)
UPDATE sensor_config SET ip_address = '10.0.0.15' WHERE sensor_id = 1;

-- B. PRUEBA DE ESTADO (Dispara 'state_events')
-- Primeras lecturas (Usando UPSERT para evitar duplicados)
INSERT INTO sensor_state (sensor_id, current_temp) VALUES (1, 35.4)
ON CONFLICT (sensor_id) DO UPDATE SET current_temp = EXCLUDED.current_temp, last_update = NOW();

INSERT INTO sensor_state (sensor_id, current_temp) VALUES (2, 42.1)
ON CONFLICT (sensor_id) DO UPDATE SET current_temp = EXCLUDED.current_temp, last_update = NOW();

-- Simular cambio de temperatura en el nodo 2 (El broker recibirá el JSON con 45.8)
UPDATE sensor_state SET current_temp = 45.8, last_update = NOW() WHERE sensor_id = 2;

-- ==========================================================
-- 5. CONSULTA DE VERIFICACIÓN FINAL
-- ==========================================================

-- Esta consulta une ambas tablas para mostrar el estado global
SELECT 
    c.sensor_id AS "ID",
    c.hostname AS "Host",
    c.ip_address AS "IP",
    COALESCE(s.current_temp::text, 'N/A') AS "Temp (ºC)",
    COALESCE(s.last_update::text, 'Sin datos') AS "Última Actualización",
    CASE WHEN c.is_active THEN 'ACTIVO' ELSE 'INACTIVO' END AS "Estado"
FROM sensor_config c
LEFT JOIN sensor_state s ON c.sensor_id = s.sensor_id
ORDER BY c.sensor_id ASC;
