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
    DatabaseManager(const std::string& connection_str);

    virtual ~DatabaseManager();

    // Establish connection
    void connect();
    void disconnect();

    // Example: Execute a simple query
    void execute(const std::string& sql);

    // Example: Fetch data (returns a result set)
    pqxx::result query(const std::string& sql);

    boost::json::object get_sanity_info();

    void parser_notify(const pqxx::notification& n, boost::json::object& msg);

        /**
     * @brief Registra un canal de escucha (LISTEN) y su manejador de eventos.
     * 
     * Esta función vincula un canal de PostgreSQL con una función callback personalizada.
     * Utiliza la API moderna de libpqxx para gestionar las suscripciones de forma interna.
     * 
     * @param channel Nombre del canal de notificación (ej. "config_updates").
     * @param handler Función callback (lambda) que procesará el JSON recibido.
     * 
     * @important FASE DE CONFIGURACIÓN: Este método debe ser invocado únicamente de forma 
     * SECUENCIAL antes de arrancar el hilo de escucha mediante 'run_listener_loop()'. 
     * Registrar canales mientras el listener está activo provocará condiciones de carrera, 
     * ya que libpqxx no sincroniza internamente el mapa de manejadores.
     * 
     * @throw std::runtime_error si se intenta registrar un canal cuando el listener ya está en ejecución.
     */
    void register_listen_async(const std::string& channel, std::function<void(boost::json::object)> callback);

    /**
     * @brief Gestión del Listener Asíncrono de PostgreSQL (NOTIFY/LISTEN).
     * 
     * Esta sección permite la monitorización en tiempo real de cambios en la base de datos 
     * mediante un hilo dedicado (std::jthread).
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