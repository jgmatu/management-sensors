#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#include <pqxx/pqxx>
#include <boost/json.hpp> // For JSON handling in get_sanity_info()

class DatabaseManager {

public:

    /**
     * @brief Constructor: Inicializa el DatabaseManager con la cadena de conexión.
     *
     * El constructor recibe una cadena de conexión a PostgreSQL y la almacena internamente.
     * No abre la conexión de inmediato: simplemente prepara el objeto para cuando se llame connect().
     * También inicializa el puntero al hilo de escucha (listener_thread_) como nulo.
     */
    DatabaseManager(const std::string& connection_str);

    /**
     * @brief Destructor: Limpieza y desconexión completa.
     * 
     * El destructor de DatabaseManager garantiza una finalización segura y ordenada del objeto:
     *   - Llama internamente a disconnect() para cerrar las conexiones físicas a la base de datos, 
     *     despertando cualquier hilo bloqueado en espera de notificaciones.
     *   - Sincroniza y detiene el hilo de escucha si está activo, asegurando que todos los recursos 
     *     y trabajos pendientes hayan finalizado antes de destruir el objeto.
     *   - Libera (reset) los smart pointers asociados a las conexiones para evitar cualquier acceso 
     *     futuro accidental y garantizar el borrado de los objetos gestionados.
     *   - Proporciona trazas std::cout para depuración y verificación de una limpieza adecuada.
     *
     * No se debe acceder a ninguna conexión ni a recursos asociados después de que el destructor haya terminado.
     */
    virtual ~DatabaseManager();

    /**
     * @brief Establece una conexión a la base de datos.
     * 
     * Inicializa las conexiones de escucha y de consulta a PostgreSQL.
     * Esta función es segura ante múltiples llamadas y libera los recursos asociados.
     * 
     * @throws std::runtime_error si la conexión falla.
     */
    void connect();

    /**
     * @brief Desconecta las conexiones activas a la base de datos.
     * 
     * Cierra de manera segura las conexiones abiertas de escucha y de consulta a PostgreSQL.
     * Esta función es segura ante múltiples llamadas y libera los recursos asociados.
     * 
     * @throws No lanza excepciones si las conexiones ya están cerradas.
     */
    void disconnect();

    // Example: Execute a simple query
    void execute(const std::string& sql);

    // Example: Fetch data (returns a result set)
    pqxx::result query(const std::string& sql);

    /**
     * @brief Obtiene información de salud y estado de la base de datos.
     * 
     * Realiza una consulta simple a la base de datos para obtener información sobre el servidor PostgreSQL,
     * como la versión del servidor, el tiempo de actividad, el PID del backend y la versión de pqxx.
     * 
     * @return Un objeto boost::json::object con la información obtenida.
     * 
     * @throws std::runtime_error si la consulta falla o no hay conexión disponible.
     */
    boost::json::object get_sanity_info();

    /**
     * @brief Registra un canal de escucha (LISTEN) y su manejador de eventos.
     * 
     * Este método asocia un canal de notificación de PostgreSQL con un callback (lambda) que
     * recibirá el mensaje JSON cuando se produzca una notificación NOTIFY en dicho canal.
     * 
     * # Ciclo de Vida y Seguridad del Registro
     * 
     * - **FASE DE CONFIGURACIÓN**: Todos los registros de canales y sus handlers deben realizarse 
     *   EXCLUSIVAMENTE en la fase de configuración inicial, antes de arrancar el hilo de escucha
     *   mediante 'run_listener_loop()'.
     * - No está permitido (ni es seguro) invocar este método una vez que el listener asíncrono 
     *   esté en ejecución, debido a la falta de sincronización interna en el manejo de callbacks
     *   de libpqxx (no es thread-safe).
     * - El registro dinámico de nuevos canales mientras el hilo de escucha está activo puede provocar
     *   race conditions, deadlocks o corrupción de memoria.
     * 
     * # Parámetros
     * @param channel Nombre del canal de notificación de PostgreSQL (ej. "config_updates").
     * @param handler Callback a invocar cuando se reciba una notificación en el canal, recibe
     *        un objeto boost::json::object con el payload decodificado.
     * 
     * # Excepciones
     * @throw std::runtime_error Si se intenta registrar un canal cuando el listener ya está en ejecución.
     * 
     * # Uso recomendado
     * Este método solo debe llamarse durante el arranque, en un flujo de creación SECUENCIAL, 
     * antes de invocar 'run_listener_loop'. Una vez iniciado el hilo de escucha, el registro 
     * de nuevos canales no será aceptado y lanzará una excepción.
     */
    void register_listen_async(const std::string& channel, std::function<void(boost::json::object)> callback);

    /**
     * @brief Inicia el bucle de escucha asíncrono para recibir notificaciones de PostgreSQL.
     * 
     * Este método arranca un hilo de fondo que permanece en espera de notificaciones de PostgreSQL
     * mediante 'wait_notification()'. Cuando se recibe una notificación, invoca el callback
     * asociado (si existe) para procesar el mensaje JSON.
     * 
     * @note SEGURIDAD DE HILOS Y FLUJO CRÍTICO:
     * Debido a que la implementación interna de libpqxx para el manejo de canales (listen) 
     * no es segura para hilos (thread-safe), se ha definido un flujo de ejecución estricto:
     * 
     * 1. FASE DE REGISTRO (Secuencial): Se deben registrar todos los callbacks (handlers) 
     *    mediante 'register_listen_async' ANTES de iniciar el hilo de escucha.
     * 
     * 2. FASE DE ACTIVACIÓN: Una vez configurados todos los canales necesarios, se invoca 
     *    'run_listener_loop' para arrancar el hilo de fondo.
     * 
     * @warning No se deben registrar nuevos handlers una vez que el listener está en ejecución. 
     * Intentar modificar la tabla de callbacks mientras el hilo de escucha está bloqueado 
     * en 'wait_notification' provocará una condición de carrera, deadlock o corrupción de memoria.
     */
    void run_listener_loop();

    /**
     * @brief Sincroniza y espera la finalización del hilo de escucha.
     * 
     * Este método bloquea el hilo llamante hasta que el hilo de fondo (listener_thread_) 
     * termine su ejecución de forma segura. 
     * 
     * @note FLUJO DE CIERRE:
     * Al utilizar 'std::jthread', el hilo recibe una señal de parada (stop_token). 
     * Este método asegura que todos los recursos de red y descriptores de socket 
     * asociados a la conexión de PostgreSQL se liberen correctamente antes de 
     * que el objeto 'DatabaseManager' sea destruido.
     * 
     * @warning Debe invocarse únicamente durante la fase de apagado (shutdown) del sistema.
     */
    void join();

    /**
     * @brief Performs an UPSERT (Insert or Update) of the sensor's master configuration.
     * 
     * This method synchronizes the 'sensor_config' table with the latest state. 
     * If the 'sensor_id' already exists, it updates all fields (hostname, IP, status, 
     * and request_id) with the provided values. If it doesn't exist, a new record is created.
     * 
     * @param sensor_id Unique identifier for the sensor (Primary Key).
     * @param hostname The current/new hostname string for the sensor.
     * @param ip The INET-compatible IP address string.
     * @param is_active Boolean flag indicating if the sensor is currently enabled.
     * @param request_id The uint64_t application-level ID used for tracking this state change.
     * 
     * @note This method should be called after a configuration change is successfully 
     * acknowledged by the agent to ensure the master table reflects the real-world state.
     * 
     * @throw pqxx::sql_error If the database transaction fails or constraints are violated.
     */
    void upsert_sensor_config(int sensor_id, const std::string& hostname, const std::string& ip,
        bool is_active, uint64_t request_id);

    /**
     * @brief Queues a pending configuration change for a specific sensor.
     * 
     * @param sensor_id Unique identifier of the sensor.
     * @param hostname New hostname to be assigned.
     * @param ip New IP address (v4 or v6).
     * @param is_active Desired operational state.
     * @param request_id The uint64_t application-level ID used for tracking this state change.
     * 
     * @note This method should be called after a configuration change is successfully 
     * acknowledged by the agent to ensure the master table reflects the real-world state.
     * 
     * @throw pqxx::sql_error If the database transaction fails or constraints are violated.
     */
    void add_pending_config(int sensor_id,  const std::string& hostname,  const std::string& ip,
        bool is_active, u_int64_t request_id);

    /**
     * @brief Performs an atomic UPSERT of the sensor's real-time telemetry data.
     * 
     * This method synchronizes the incoming MQTT telemetry with the PostgreSQL 
     * 'sensor_state' table. If the sensor_id exists, it updates the 'current_temp' 
     * and refreshes the 'last_update' timestamp. If the sensor_id is new, it 
     * creates a new state record.
     * 
     * @param sensor_id Unique identifier of the sensor (Foreign Key to sensor_config).
     * @param temp The current temperature value in Celsius.
     * 
     * @note **Thread-Safety**: Uses the internal DML-specific mutex to prevent 
     *       contention with concurrent TLS-driven configuration writes.
     * @note **Triggers**: Successful completion of this transaction fires the 
     *       PostgreSQL 'state_events' trigger for real-time notification.
     */
    void upsert_sensor_state(int sensor_id, double temp);

private:

    /**
     * @brief Procesa y parsea las notificaciones recibidas desde PostgreSQL.
     * 
     * Esta función es llamada internamente cuando se recibe una notificación en uno de los canales
     * registrados (por ejemplo, a través de LISTEN/NOTIFY). Se encarga de interpretar los datos
     * recibidos (payload) y transformar el contenido del mensaje, generalmente en formato JSON,
     * para su posterior manejo por los callbacks registrados.
     * 
     * @param n Referencia a la notificación recibida (pqxx::notification).
     * @param msg Objeto JSON de Boost donde se almacena el resultado parseado.
     * 
     * @details 
     * - Realiza las validaciones necesarias para garantizar la integridad del mensaje recibido.
     * - Es utilizada normalmente junto con el mecanismo de escucha asincrónica de PostgreSQL (LISTEN/NOTIFY).
     * - Si el payload no es un JSON válido, puede poblar 'msg' con información de error o advertencia.
     * - La función no lanza excepciones pero puede dejar el objeto JSON vacío o con un campo "error".
     * 
     * @note Esta función es privada y su uso está restringido a la infraestructura interna de manejo
     *       de eventos provenientes de la base de datos.
     */
    void parser_notify(const pqxx::notification& n, boost::json::object& msg);

    std::string conn_str_;
    std::unique_ptr<std::jthread> listener_thread_;

    /**
     * @brief Map to store callbacks per channel.
     * Key: Channel name (e.g., "config_events")
     * Value: Function to process the JSON payload.
     */
    std::unordered_map<std::string, std::function<void(boost::json::object)>> callbacks_;

    // Protege el ciclo de vida de connection_.
    // Dado que cualquier clase externa puede invocar disconnect(), 
    // todas las operaciones internas (especialmente las asíncronas) 
    // DEBEN adquirir este mutex antes de desreferenciar el puntero.
    std::unique_ptr<pqxx::connection> connection_queries_;
    std::unique_ptr<pqxx::connection> connection_listener_;
    mutable std::mutex conn_mutex_; 
};