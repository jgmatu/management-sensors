/**
 * Botan post-quantum TLS test server
 *
 * (C) 2023 René Fischer, René Meusel
 *
 * This implementation is roughly based on this BSL-licensed boost beast example
 * by Klemens D. Morgenstern:
 *   www.boost.org/doc/libs/1_83_0/libs/beast/example/http/server/awaitable/http_server_awaitable.cpp
 */

#include <chrono>
#include <concepts>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <db/DatabaseManager.hpp>
#include <net/QuantumSafeTlsEngine.hpp>
#include <json/JsonUtils.hpp>

// Global pointer to the Database Manager
// Shared across the TLS Engine threads and the Main thread
std::shared_ptr<DatabaseManager> g_db;

struct SensorCommand {
    std::string cmd;
    int id = -1;
    std::string attr;
    std::string value;
    bool valid = false;
};

/**
 * @brief Improved parser for sensor CLI syntax.
 * Handles trailing spaces, malformed IDs, and stream fail states.
 * 
 * @note MEMORY SAFETY & PERFORMANCE:
 * This function returns the 'SensorCommand' struct by value. Thanks to 
 * **RVO (Return Value Optimization)** and **Copy Elision** (standard in C++20/GCC 14), 
 * the compiler constructs the object directly in the caller's stack frame. 
 * This avoids expensive copies and eliminates the risk of **Segmentation Faults** 
 * or "Dangling Pointers" typically associated with returning pointers to 
 * local stack variables.
 * 
 * @param request The raw string received from the TLS session.
 * @return A self-contained SensorCommand object.
 */
SensorCommand parse_sensor_command(const std::string& request) {
    SensorCommand sc;
    std::istringstream iss(request);
    
    // 1. Clear the struct and mark invalid by default
    sc.valid = false;

    // 2. Extract tokens one by one and check the stream state
    if (!(iss >> sc.cmd)) return sc;
    
    // Attempt to extract the ID as an integer
    if (!(iss >> sc.id)) {
        // If ID is not a valid int, the stream fails. 
        // We must clear it if we want to continue, but here we just return invalid.
        return sc;
    }

    if (!(iss >> sc.attr)) return sc;
    if (!(iss >> sc.value)) return sc;

    // 3. Final check: Ensure there isn't UNEXPECTED extra data
    std::string extra;
    if (iss >> extra) {
        // If there's more data (like a 5th word), the command is malformed
        return sc;
    }

    // 4. If we reached here, the basic structure is correct
    sc.valid = true;
    return sc;
}

/**
 * @brief Helper to print the SensorCommand state.
 * Useful for debugging race conditions or malformed CLI inputs.
 */
std::ostream& operator<<(std::ostream& os, const SensorCommand& sc) {
    os << "[SensorCommand] " 
       << (sc.valid ? "VALID" : "INVALID") << "\n"
       << "  Command: " << (sc.cmd.empty() ? "N/A" : sc.cmd) << "\n"
       << "  ID:      " << sc.id << "\n"
       << "  Attr:    " << (sc.attr.empty() ? "N/A" : sc.attr) << "\n"
       << "  Value:   " << (sc.value.empty() ? "N/A" : sc.value);
    return os;
}

/**
 * @brief Main logic processor for decrypted TLS traffic.
 * 
 * @warning CONCURRENCY ALERT: This function is executed by the Boost.Asio 
 * thread pool. Multiple instances of this function may run simultaneously 
 * on different threads for different client sessions. 
 * 
 * @note SHARED RESOURCES: Any access to global variables, static members, 
 * or shared objects (like DatabaseManager) MUST be protected by a 
 * std::mutex or use std::atomic types to prevent race conditions.
 *
 * @param input The raw decrypted bytes received from the TLS client.
 * @return std::vector<uint8_t> The data to be encrypted and sent back as a response.
 */
/**
 * @brief Logic processor for the CONFIG_SENSOR command.
 * Syntax: CONFIG_SENSOR <id> IP <address>
 * Example: CONFIG_SENSOR 1 IP 192.158.1.100
 */
std::vector<uint8_t> on_tls_message_process(const std::vector<uint8_t>& input) {
    if (input.empty()) return {};

    const std::string request(input.begin(), input.end());
    const SensorCommand sc = parse_sensor_command(request);
    std::string response;

    std::cout << sc << std::endl;

    // 1. Validation Logic
    if (!sc.valid || sc.cmd != "CONFIG_SENSOR")
    {
        response = "ERROR: Invalid Syntax. Use: CONFIG_SENSOR <id> IP <address>";
    }
    else if (sc.attr != "IP")
    {
        response = "ERROR: Unknown attribute '" + sc.attr + "'.";
    }
    else
    {
        // 2. Execution Logic
        try
        {
            std::cout << "[PQC-Logic] Configured Sensor " << sc.id << " with IP " << sc.value << std::endl;
            g_db->add_pending_config(sc.id, "sensor-" + std::to_string(sc.id), sc.value, true);
            response = "OK: Sensor " + std::to_string(sc.id) + " updated.";
        } 
        catch (const std::exception& e)
        {
            response = "ERROR: DB Failure - " + std::string(e.what());
        }
    }

    return std::vector<uint8_t>(response.begin(), response.end());
}

/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'state_events' channel.
 */
void on_db_config_event_received(boost::json::object msg)
{
    if (msg.empty()) return;

    std::cout << "************ COMMITED CONFIG EVENT! *************" << std::endl;

    // 1. Extract the channel for logging
    std::string_view channel = msg.at("channel").as_string();
    std::cout << "[DB-Handler] Event on channel: " << channel << std::endl;

    // 2. Use your existing JsonUtils to print the payload
    JsonUtils::print(std::cout, msg);
    std::cout << std::endl;

    // 3. Example Logic: If it's an 'alarm' type, log it specifically
    if (msg.contains("type") && msg.at("type").as_string() == "alarm") {
        std::cerr << "!!! SYSTEM ALARM RECEIVED FROM DATABASE !!!" << std::endl;
    }
}

/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'state_events' channel.
 */
void on_db_state_event_received(boost::json::object msg)
{
    if (msg.empty()) return;

    std::cout << "************ STATE TELEMETRY EVENT! *************" << std::endl;

    // 1. Extract the channel for logging
    std::string_view channel = msg.at("channel").as_string();
    std::cout << "[DB-Handler] Event on channel: " << channel << std::endl;

    // 2. Use your existing JsonUtils to print the payload
    JsonUtils::print(std::cout, msg);
    std::cout << std::endl;

    // 3. Example Logic: If it's an 'alarm' type, log it specifically
    if (msg.contains("type") && msg.at("type").as_string() == "alarm") {
        std::cerr << "!!! SYSTEM ALARM RECEIVED FROM DATABASE !!!" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    // clang-format off
    boost::program_options::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("port", boost::program_options::value<uint16_t>()->required(), "Port to use")
        ("policy", boost::program_options::value<std::string>()->default_value("default"), "Botan policy file (default: Botan's default policy)")
        ("cert", boost::program_options::value<std::string>()->required(), "Path to the server's certificate chain")
        ("key", boost::program_options::value<std::string>()->required(), "Path to the server's certificate private key file")
        ("ocsp-request-timeout", boost::program_options::value<uint64_t>()->default_value(10), "OCSP request timeout in seconds")
        ("ocsp-cache-time", boost::program_options::value<uint64_t>()->default_value(6 * 60), "Cache validity time for OCSP responses in minutes")
        ("document-root", boost::program_options::value<std::string>()->default_value("webroot"), "Path to the server's static documents folder");
    // clang-format on

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(boost::program_options::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help") != 0 || argc < 2)
        {
            std::cerr << "Usage: \n" << desc << std::endl;
            return 0;
        }

        boost::program_options::notify(vm);
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    // ... Initialize and start your QuantumSafeTlsEngine ...
    try
    {
        // Build the connection string with TCP Keep-Alive parameters
        std::string conn_str = 
            "dbname=javi "
            "user=javi "
            "password=12345678 "
            "host=localhost "
            "port=5432 "
            "keepalives=1 "          // Enable TCP Keep-Alive
            "keepalives_idle=60 "    // 60s idle before first probe
            "keepalives_interval=5 " // 5s between probes
            "keepalives_count=3";    // Drop after 3 failed probes

        // Initialize the global shared_ptr
        g_db = std::make_shared<DatabaseManager>(conn_str);
        g_db->connect();
        std::cout << "[DB] Connection established with Keep-Alive (60s/5s/3)." << std::endl;

        const auto port = vm["port"].as<uint16_t>();
        const auto policy = vm["policy"].as<std::string>();
        const auto certificate = vm["cert"].as<std::string>();
        const auto key = vm["key"].as<std::string>();
        const auto ocsp_cache_time = vm["ocsp-cache-time"].as<uint64_t>();
        const auto ocsp_request_timeout = vm["ocsp-request-timeout"].as<uint64_t>();
        QuantumSafeTlsEngine server(port, certificate, key, policy, ocsp_cache_time, ocsp_request_timeout);

        server.set_processor(on_tls_message_process);

        server.initialize();

        boost::json::object sanity_info = g_db->get_sanity_info();
        JsonUtils::print(std::cout, sanity_info);
        std::cout << std::endl;

        std::cout << "Starting async listener for PostgreSQL notifications..." << std::endl;

        /**
         * @bug DEADLOCK / RACE CONDITION:
         * The `txn.commit()` in `add_pending_config` hangs when the `DatabaseManager` 
         * listener thread is blocked in `wait_notification()`.
         * 
         * ROOT CAUSE ANALYSIS: 
         * Potential shared-lock contention on the PostgreSQL socket. While the 
         * listener is waiting for server-side events, it may be holding an implicit 
         * transaction state that prevents the UPSERT (INSERT ... ON CONFLICT) from 
         * finalizing its commit.
         * 
         * VERIFICATION:
         * - External sanity scripts (independent DML) succeed, confirming the DB 
         *   schema and triggers are healthy.
         * - The issue is localized to the internal thread orchestration of this class.
         * 
         * TODO: Isolate the listener into a `pqxx::nontransaction` or a dedicated 
         * connection to prevent cross-thread blocking during DML commits.
         */
        /**
         * @fix RESOLVED - ARCHITECTURAL LOCK CONTENTION:
         * Se ha corregido el Deadlock/Race Condition que bloqueaba el `txn.commit()` 
         * en `add_pending_config`.
         * 
         * SOLUCIÓN TÉCNICA: 
         * Implementación de arquitectura de **Doble Conexión Física**. 
         * No es posible (ni seguro) compartir un único socket de PostgreSQL/libpq 
         * para operaciones DML (INSERT/UPDATE) y escucha de eventos (LISTEN/NOTIFY) 
         * de forma simultánea.
         * 
         * CAUSA RAÍZ:
         * Mientras el hilo del Listener está bloqueado en `wait_notification()`, el 
         * socket queda en estado "Busy" a nivel de protocolo de red de Postgres. 
         * Cualquier intento de `COMMIT` desde otro hilo por el mismo socket resultaba 
         * en un bloqueo indefinido o un `unexpected EOF` al intentar reentrar en 
         * una sesión ocupada.
         * 
         * NUEVA ESTRUCTURA:
         * 1. `connection_queries_`: Socket dedicado exclusivamente a transacciones de escritura/lectura o consulta.
         * 2. `connection_listener_`: Socket persistente dedicado al bucle de eventos.
         * 
         * RESULTADO:
         * El pipeline de sanidad y telemetría funciona ahora de forma asíncrona y 
         * paralela sin colisiones de bloqueos (locks) ni corrupción del flujo de red.
         */
        g_db->register_listen_async("config_events", on_db_config_event_received);
        g_db->register_listen_async("state_events", on_db_state_event_received);

        // Esperamos a que el servidor termine (en este caso, se ejecutará indefinidamente hasta recibir una señal de interrupción)
        server.join();
        server.stop();

        if (g_db)
        {
            std::cout << "[DB] Liberando conexión global..." << std::endl;
            g_db.reset(); // El contador de referencias baja a 0 y se cierra la conexión
        }
        std::cout << "[System] Thread pool joined and I/O context finalized." << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
