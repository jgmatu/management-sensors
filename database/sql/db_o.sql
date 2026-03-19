-- Modelo simplificado de configuración:
-- 1 sensor (sensor_config.sensor_id) -> N peticiones (config_requests)
-- 1 petición -> N errores (config_errors)

DROP TABLE IF EXISTS sensor_config_errors;
DROP TABLE IF EXISTS sensor_config_pending;

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

CREATE TABLE sensor_config_errors (
    error_id        BIGSERIAL PRIMARY KEY,
    request_id      BIGINT UNIQUE REFERENCES config_requests(request_id) ON DELETE CASCADE,
    sensor_id       INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,
    error_code      VARCHAR(50),
    error_detail    TEXT,
    occurred_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_config_errors_request_id ON sensor_config_errors (request_id);
CREATE INDEX idx_config_errors_sensor_id ON sensor_config_errors (sensor_id);