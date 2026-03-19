# Database Architecture

## Sistema

- **Sistema:** Nucleo de datos para gestion de sensores
- **Descripcion:** Esquema relacional, reactivo y orientado a consistencia
- **Motor:** PostgreSQL 16+ (RHEL 10)

## Principios Arquitectonicos Del Modelo

### 1) Fuente de verdad y unico punto de autoridad

- Esta base de datos es la fuente de verdad del sistema.
- Tambien es el unico punto de autoridad para validar el estado de seguridad, configuracion y telemetria.
- Ningun cambio se considera efectivo hasta quedar persistido aqui.

### 2) Abstraccion de cola de mensajes con persistencia

- El modelo usa PostgreSQL como abstraccion de cola de mensajes persistente para desacoplar API/Dispatcher y Controlador MQTT.
- `sensor_config_pending` representa la intencion de cambio (trabajo en cola).
- `sensor_config_errors` registra fallos asincronos sin bloquear el flujo.
- Este enfoque evita perdida de ordenes ante reinicios o caidas parciales.

### 3) Modelo reactivo

- El esquema es reactivo: combina persistencia + eventos `NOTIFY/LISTEN`.
- Los canales de eventos sincronizan componentes sin polling continuo:
  - `config_requested`
  - `config_errors`
  - `state_events`
  - `cert_events`
  - `config_events`

### Canales actuales publicados por la BD

- `config_requested`:
  - emitido por `notify_config_request()` sobre `sensor_config_pending`
  - en operaciones `INSERT/UPDATE`
- `config_errors`:
  - emitido por `notify_config_error()` sobre `sensor_config_errors`
  - en operaciones `INSERT/UPDATE/DELETE`
- `state_events`:
  - emitido por `notify_state_change()` sobre `sensor_state`
  - en operaciones `INSERT/UPDATE`
- `cert_events`:
  - emitido por `notify_cert_change()` sobre `sensor_certs`
  - en operaciones `INSERT/UPDATE/DELETE`
- `config_events`:
  - emitido por `notify_config_change()` sobre `sensor_config`
  - en operaciones `INSERT/UPDATE/DELETE`

## Modelo Relacional Principal

1. `sensor_certs` (seguridad)
   - Identidad criptografica X.509 (PEM) de cada nodo.

2. `sensor_config` (realidad confirmada)
   - Estado operativo confirmado del sensor en red.
   - Relacion 1:1 con `sensor_certs` mediante `cert_id UNIQUE` y `NOT NULL`.

3. `sensor_config_pending` (estado deseado)
   - Cola persistente de peticiones de configuracion.
   - Relacion 1:N con `sensor_config`.

4. `sensor_config_errors` (retroalimentacion)
   - Historial de errores por sensor/peticion para trazabilidad.
   - Relacion 1:N con `sensor_config`.

5. `sensor_state` (telemetria)
   - Ultimo estado observado por sensor (snapshot operativo).

## Abstraccion de Logica Relacional (Jerarquia De Datos)

1. Capa de seguridad: `sensor_certs`
   - Raiz criptografica de confianza del sistema.

2. Capa de identidad/configuracion confirmada: `sensor_config`
   - Representa la realidad efectiva del sensor.
   - Relacion 1:1 con `sensor_certs` (`cert_id UNIQUE`, `NOT NULL`).

3. Capa de comunicacion/intencion: `sensor_config_pending`
   - Materializa la cola persistente de peticiones.
   - Relacion 1:N con `sensor_config` (sensor -> peticiones pendientes).

4. Capa de retroalimentacion de fallos: `sensor_config_errors`
   - Persistencia de errores funcionales/tecnicos.
   - Relacion 1:N con `sensor_config` (sensor -> errores).
   - Relacion 0..1 por `request_id` (una peticion puede provocar un error).

5. Capa de estado operativo: `sensor_state`
   - Snapshot de telemetria actual por sensor.
   - Relacion 1:1 con `sensor_config` por `sensor_id`.

## Integridad Y Consistencia

La integridad referencial (FKs), unicidad y reglas de borrado mantienen la coherencia entre intencion (`pending`), resultado (`config`) y observabilidad (`errors/state`), reforzando el rol de la BD como nucleo transaccional del sistema.
