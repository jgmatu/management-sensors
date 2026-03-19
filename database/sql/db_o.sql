CREATE TABLE config_requests (
    request_id        BIGINT PRIMARY KEY,          -- viene del Dispatcher
    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    action            TEXT NOT NULL CHECK (action IN ('INSERT','UPDATE','DELETE')),

    desired_hostname  VARCHAR(100),
    desired_ip        INET,
    desired_is_active BOOLEAN,

    status            TEXT NOT NULL CHECK (status IN ('PENDING','SUCCESS','ERROR','TIMEOUT')),
    requested_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at      TIMESTAMPTZ
);

CREATE INDEX idx_config_requests_sensor_id
    ON config_requests(sensor_id);

CREATE INDEX idx_config_requests_status
    ON config_requests(status);

CREATE TABLE sensor_config_pending (
    request_id        BIGINT PRIMARY KEY REFERENCES config_requests(request_id) ON DELETE CASCADE
    -- opcional: puedes redundar campos si te facilita queries, pero no es obligatorio
);

-- Si quieres mantener requested_at en pending:
-- ALTER TABLE sensor_config_pending ADD COLUMN requested_at TIMESTAMPTZ NOT NULL DEFAULT now();

CREATE TABLE sensor_config_errors (
    error_id          BIGSERIAL PRIMARY KEY,
    request_id        BIGINT NOT NULL REFERENCES config_requests(request_id) ON DELETE CASCADE,

    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    error_code        VARCHAR(50),
    error_detail      TEXT,          -- o JSONB si prefieres
    occurred_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_sensor_config_errors_request_id
    ON sensor_config_errors(request_id);

ALTER TABLE sensor_config
    ADD COLUMN last_success_request_id BIGINT,
    ADD COLUMN last_success_at TIMESTAMPTZ,
    ADD COLUMN last_error_request_id BIGINT,
    ADD COLUMN last_error_at TIMESTAMPTZ;

CREATE TABLE config_requests (
    request_id        BIGINT PRIMARY KEY,   -- viene del dispatcher
    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    hostname          VARCHAR(100),
    desired_ip        INET,
    desired_is_active BOOLEAN,

    action            TEXT NOT NULL CHECK (action IN ('INSERT','UPDATE','DELETE')),

    status            TEXT NOT NULL CHECK (status IN ('PENDING','SUCCESS','ERROR','TIMEOUT')),

    requested_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at      TIMESTAMPTZ
);

CREATE INDEX idx_config_requests_status ON config_requests(status);

CREATE TABLE sensor_config_pending (
    request_id        BIGINT PRIMARY KEY REFERENCES config_requests(request_id) ON DELETE CASCADE,

    new_hostname      VARCHAR(100),
    new_ip_address    INET,
    new_is_active     BOOLEAN,

    requested_at      TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE sensor_config_errors (
    error_id          BIGSERIAL PRIMARY KEY,
    request_id        BIGINT NOT NULL REFERENCES config_requests(request_id) ON DELETE CASCADE,

    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    error_code        VARCHAR(50),
    error_detail      TEXT,
    occurred_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_sensor_config_errors_request_id ON sensor_config_errors(request_id);


# Peticiones pendientes en vuelo paralelas

CREATE TABLE config_requests (
    request_id        BIGINT PRIMARY KEY,   -- generado por Dispatcher (aplicación)
    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    hostname          VARCHAR(100),
    desired_ip        INET,
    desired_is_active BOOLEAN,

    action            TEXT NOT NULL CHECK (action IN ('INSERT','UPDATE','DELETE')),

    status            TEXT NOT NULL CHECK (status IN ('PENDING','SUCCESS','ERROR','TIMEOUT')),

    requested_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    completed_at      TIMESTAMPTZ
);

CREATE INDEX idx_config_requests_sensor_id ON config_requests(sensor_id);
CREATE INDEX idx_config_requests_status ON config_requests(status);

CREATE TABLE sensor_config_pending (
    request_id        BIGINT PRIMARY KEY REFERENCES config_requests(request_id) ON DELETE CASCADE,

    new_hostname     VARCHAR(100),
    new_ip_address   INET,
    new_is_active    BOOLEAN,

    requested_at     TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_sensor_config_pending_sensor_id
    ON sensor_config_pending (request_id);

CREATE TABLE sensor_config_errors (
    error_id          BIGSERIAL PRIMARY KEY,

    request_id        BIGINT NOT NULL REFERENCES config_requests(request_id) ON DELETE CASCADE,
    sensor_id         INT NOT NULL REFERENCES sensor_config(sensor_id) ON DELETE CASCADE,

    error_code        VARCHAR(50),
    error_detail      TEXT,

    occurred_at       TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX idx_sensor_config_errors_request_id
    ON sensor_config_errors(request_id);