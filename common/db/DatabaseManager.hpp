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

    void register_listen_async(const std::string& channel, std::function<void(boost::json::object)> callback);

    void join();

    /**
     * @brief Queues a pending configuration change for a specific sensor.
     * 
     * @param sensor_id Unique identifier of the sensor.
     * @param hostname New hostname to be assigned.
     * @param ip New IP address (v4 or v6).
     * @param is_active Desired operational state.
     */
    void add_pending_config(int sensor_id,  const std::string& hostname,  const std::string& ip, bool is_active);

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

    void run_listener_loop();

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