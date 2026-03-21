# Database Architecture

## Sistema

- **Sistema:** Núcleo de datos para gestión de sensores.
- **Descripción:** Esquema relacional, reactivo y orientado a consistencia.
- **Motor:** PostgreSQL 16+ (recomendado en RHEL 10; el proyecto se prueba también con versiones recientes tipo 18.x).

## DDL de referencia

| Archivo | Uso |
|---|---|
| [`sql/db.sql`](sql/db.sql) | **Canónico:** `CREATE TABLE`, triggers, funciones `NOTIFY`, datos de prueba. |
| [`sql/db_o.sql`](sql/db_o.sql) | Borrador / variante antigua (referencias inconsistentes); preferir `db.sql`. |

Aplicar en un base de datos vacía (o revisar los `DROP` del script) antes de arrancar servidor / controlador.

---

## Diagrama entidad–relación (estado actual)

Relaciones según el DDL en `sql/db.sql`:

```mermaid
erDiagram
    sensor_certs ||--|| sensor_config : "cert_id UNIQUE"
    sensor_config ||--o{ sensor_config_pending : "sensor_id"
    sensor_config ||--o{ sensor_config_errors : "sensor_id"
    sensor_config ||--o| sensor_state : "sensor_id PK"
    sensor_config_pending ||--o| sensor_config_errors : "request_id UNIQUE"

    sensor_certs {
        serial cert_id PK
        varchar fingerprint UK
        varchar common_name
        timestamptz not_after
        boolean is_revoked
        text pem_data
    }

    sensor_config {
        int sensor_id PK
        varchar hostname
        inet ip_address
        boolean is_active
        int cert_id FK_UK
        bigint request_id
    }

    sensor_config_pending {
        bigint request_id PK
        int sensor_id FK
        varchar requested_hostname
        inet requested_ip
        boolean requested_is_active
        text status
        timestamptz requested_at
        timestamptz completed_at
    }

    sensor_config_errors {
        bigserial error_id PK
        bigint request_id FK_UK
        int sensor_id FK
        varchar error_code
        text error_detail
        timestamptz occurred_at
    }

    sensor_state {
        int sensor_id PK_FK
        numeric current_temp
        timestamptz last_update
    }
```

### Vista lógica (capas)

```
sensor_certs          (identidad X.509, PEM)
       │
       │ 1:1 (cert_id UNIQUE, NOT NULL)
       ▼
sensor_config         (configuración confirmada por sensor_id)
       │
       ├──1:N──► sensor_config_pending   (cola de intención / pipeline CONFIG_IP)
       ├──1:N──► sensor_config_errors    (fallos ligados a sensor y opcionalmente a request)
       └──1:1──► sensor_state            (última telemetría conocida)
```

---

## Tablas y columnas (resumen)

### `sensor_certs`

| Columna | Tipo | Notas |
|---|---|---|
| `cert_id` | `SERIAL` | PK |
| `fingerprint` | `VARCHAR(64)` | Único; huella del certificado |
| `common_name` | `VARCHAR(255)` | |
| `not_after` | `TIMESTAMPTZ` | |
| `is_revoked` | `BOOLEAN` | Default `FALSE` |
| `pem_data` | `TEXT` | PEM completo (`STORAGE EXTERNAL`) |

### `sensor_config`

| Columna | Tipo | Notas |
|---|---|---|
| `sensor_id` | `INT` | PK |
| `hostname` | `VARCHAR(100)` | |
| `ip_address` | `INET` | |
| `is_active` | `BOOLEAN` | Default `TRUE` |
| `cert_id` | `INT` | FK → `sensor_certs`, **UNIQUE**, `NOT NULL`, `ON DELETE CASCADE` |
| `request_id` | `BIGINT` | Correlación con última petición relevante (uso aplicación) |

### `sensor_config_pending`

| Columna | Tipo | Notas |
|---|---|---|
| `request_id` | `BIGINT` | PK (valor alineado con secuencia en aplicación) |
| `sensor_id` | `INT` | FK → `sensor_config`, `ON DELETE CASCADE` |
| `requested_hostname` | `VARCHAR(100)` | |
| `requested_ip` | `INET` | |
| `requested_is_active` | `BOOLEAN` | |
| `status` | `TEXT` | `CHECK` ∈ `PENDING`, `SUCCESS`, `ERROR`, `TIMEOUT` |
| `requested_at` | `TIMESTAMPTZ` | Default `now()` |
| `completed_at` | `TIMESTAMPTZ` | |

Índices: `sensor_id`, `status`.

### `sensor_config_errors`

| Columna | Tipo | Notas |
|---|---|---|
| `error_id` | `BIGSERIAL` | PK |
| `request_id` | `BIGINT` | FK → `sensor_config_pending`, **UNIQUE**, `ON DELETE CASCADE` |
| `sensor_id` | `INT` | FK → `sensor_config`, `ON DELETE CASCADE` |
| `error_code` | `VARCHAR(50)` | |
| `error_detail` | `TEXT` | |
| `occurred_at` | `TIMESTAMPTZ` | Default `now()` |

Índices: `request_id`, `sensor_id`.

### `sensor_state`

| Columna | Tipo | Notas |
|---|---|---|
| `sensor_id` | `INT` | PK y FK → `sensor_config`, `ON DELETE CASCADE` |
| `current_temp` | `NUMERIC(5,2)` | |
| `last_update` | `TIMESTAMPTZ` | Default `CURRENT_TIMESTAMP` |

---

## Secuencia `request_id_seq` (aplicación)

No está creada en el script SQL inicial; la inicializa el servidor vía `DatabaseManager::init_request_id_sequence()`:

- `CREATE SEQUENCE IF NOT EXISTS request_id_seq`
- `setval` al máximo de `request_id` presente en `sensor_config_pending` y `sensor_config` (y mínimo 1 si hace falta).

Los nuevos `request_id` para el pipeline salen de `nextval('request_id_seq')` (`generate_request_id()`), de modo que **varias instancias del servidor** comparten un único contador coherente en BD.

---

## Modelo reactivo: `NOTIFY` / `LISTEN`

El esquema combina persistencia con eventos en estos canales (payload JSON según trigger en `db.sql`):

| Canal `NOTIFY` | Tabla / trigger | Rol típico |
|---|---|---|
| `config_events` | `sensor_config` | Servidor: confirmación de config aplicada / cambios |
| `config_requested` | `sensor_config_pending` | Controlador: nueva orden en cola |
| `error_events` | `sensor_config_errors` | Errores de configuración |
| `state_events` | `sensor_state` | Telemetría |
| `cert_events` | `sensor_certs` | Revocación / ciclo de vida de certificados |

**Consumidores en código (referencia):**

- **Servidor** (`server/src/main.cpp`): `LISTEN` `config_events`, `state_events`, `error_events`.
- **Controlador** (`controller/src/main.cpp`): `LISTEN` `config_requested`.

---

## Principios arquitectónicos del modelo

### 1) Fuente de verdad y único punto de autoridad

- Esta base de datos es la fuente de verdad del sistema.
- También es el único punto de autoridad para validar el estado de seguridad, configuración y telemetría.
- Ningún cambio se considera efectivo hasta quedar persistido aquí.

### 2) Abstracción de cola de mensajes con persistencia

- El modelo usa PostgreSQL como abstracción de cola de mensajes persistente para desacoplar API/Dispatcher y controlador MQTT.
- `sensor_config_pending` representa la intención de cambio (trabajo en cola).
- `sensor_config_errors` registra fallos asíncronos sin bloquear el flujo.
- Este enfoque evita pérdida de órdenes ante reinicios o caídas parciales.

### 3) Modelo reactivo

- El esquema es reactivo: combina persistencia + eventos `NOTIFY` / `LISTEN`.
- Los canales de eventos sincronizan componentes sin polling continuo (ver tabla arriba).

### Canales publicados por la BD (detalle)

- **`config_requested`:** emitido por `notify_config_request()` sobre `sensor_config_pending` (INSERT/UPDATE).
- **`error_events`:** emitido por `notify_config_error()` sobre `sensor_config_errors` (INSERT/UPDATE/DELETE).
- **`state_events`:** emitido por `notify_state_change()` sobre `sensor_state` (INSERT/UPDATE).
- **`cert_events`:** emitido por `notify_cert_change()` sobre `sensor_certs` (INSERT/UPDATE/DELETE).
- **`config_events`:** emitido por `notify_config_change()` sobre `sensor_config` (INSERT/UPDATE/DELETE).

---

## Modelo relacional principal (lista corta)

1. **`sensor_certs`** — Identidad criptográfica X.509 (PEM) de cada nodo.
2. **`sensor_config`** — Configuración confirmada; relación 1:1 con `sensor_certs` (`cert_id` UNIQUE, NOT NULL).
3. **`sensor_config_pending`** — Cola de peticiones; relación 1:N con `sensor_config`.
4. **`sensor_config_errors`** — Errores por sensor/petición; relación 1:N con `sensor_config`; opcional 0..1 por `request_id`.
5. **`sensor_state`** — Snapshot de telemetría; relación 1:1 con `sensor_config` por `sensor_id`.

## Abstracción de lógica relacional (jerarquía de datos)

1. Capa de seguridad: `sensor_certs`.
2. Capa de identidad / configuración confirmada: `sensor_config`.
3. Capa de comunicación / intención: `sensor_config_pending`.
4. Capa de retroalimentación de fallos: `sensor_config_errors`.
5. Capa de estado operativo: `sensor_state`.

## Integridad y consistencia

La integridad referencial (FKs), unicidad y reglas de borrado mantienen la coherencia entre intención (`pending`), resultado (`config`) y observabilidad (`errors` / `state`), reforzando el rol de la BD como núcleo transaccional del sistema.
