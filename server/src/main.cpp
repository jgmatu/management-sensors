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
#include <map>

#include <db/DatabaseManager.hpp>
#include <net/QuantumSafeTlsEngine.hpp>
#include <json/JsonUtils.hpp>
#include <dispatcher/Dispatcher.hpp>

#define REQUEST_TIMEOUT_MS 2 * 1000

// Global pointer to the Database Manager
// Shared across the TLS Engine threads and the Main thread
std::shared_ptr<DatabaseManager> g_db;
Dispatcher g_dispatcher;

struct SensorCommand {
    std::string cmd;
    int id = -1;
    std::string attr;
    std::string value;
    bool valid = false;
};

// Helper to convert status to human-readable string
std::string status_to_string(ResponseStatus status) {
    switch (status) {
        case ResponseStatus::SUCCESS:      return "SUCCESS";
        case ResponseStatus::TIMEOUT:      return "ERROR: Request Timed Out";
        case ResponseStatus::DB_ERROR:     return "ERROR: Database Failure";
        case ResponseStatus::SYSTEM_FULL:  return "ERROR: Maximum Pending Requests Reached";
        case ResponseStatus::PENDING:      return "PENDING";
        default:                           return "UNKNOWN_ERROR";
    }
}

const std::map<std::string, std::function<std::string(const SensorCommand&)>> command_registry = {
    {"CONFIG_IP", [](const SensorCommand& sc) -> std::string {
        // 1. Generate unique uint64_t ID
        uint64_t request_id = g_dispatcher.generate_id();

        std::cout << "Request ID: " << request_id << std::endl;
        // 2. Persist to DB (using the INSERT we updated earlier)
        // hostname: "sensor-X", ip: sc.value, active: true
        g_db->add_pending_config(sc.id, "sensor-" + std::to_string(sc.id), sc.value, true, request_id);

        // --- FASE DE ESPAERA: SINCRONIZACIÓN DE PETICIÓN CLI ---
        // Se inicia el bloqueo del hilo actual (CLI) para esperar la confirmación 
        // asíncrona (MQTT/DB-Notify) mediante el request_id generado.
        std::cout << "[DEBUG] CLI Thread: Waiting for response [ReqID: " << request_id  << "] (Timeout: " << REQUEST_TIMEOUT_MS << "ms)..." << std::endl;
        auto status = g_dispatcher.wait_for_response(request_id, REQUEST_TIMEOUT_MS);

        // --- FASE DE ACTIVACIÓN: RESPUESTA RECIBIDA ---
        // El hilo ha sido despertado. Procedemos a registrar el resultado en los logs.
        std::cout << "[DEBUG] CLI Thread: Awake! [ReqID: " << request_id  << "] | Final Status: " << status_to_string(status) << std::endl;

        if (status == ResponseStatus::SUCCESS) {
            return "OK: Sensor " + std::to_string(sc.id) + " updated successfully.";
        }
        return "ERROR: " + status_to_string(status) + " (ID: " + std::to_string(request_id) + ")";
    }},
    {"REBOOT", [](const SensorCommand& sc) -> std::string {
        // Example of adding another command easily
        return "OK: Rebooting sensor " + std::to_string(sc.id);
    }}
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
    os << "[SensorCommand] " << "\n"
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
 * @section Synchronicity & Response Handling:
 * To receive the final configuration response, a message queue must be 
 * implemented to hold the thread until the final configuration resolution 
 * is achieved. The callback will remain in a blocking state, waiting 
 * for a message to arrive in the queue containing the final response. 
 * This final response will be received through the Database Listener, 
 * triggered by the Controller Process once the sensor update is finalized.
 *
 * @param input The raw decrypted bytes received from the TLS client.
 * @return std::vector<uint8_t> The data to be encrypted and sent back as a response.
 */
std::vector<uint8_t> on_tls_message_process(const std::vector<uint8_t>& input) {
    if (input.empty()) return {};

    const std::string request(input.begin(), input.end());
    const SensorCommand sc = parse_sensor_command(request);
    std::string response;
    u_int64_t request_id = 0;

    std::cout << sc << std::endl;

    // Look up the 'cmd' field in the map
    auto it = command_registry.find(sc.cmd);

    if (it != command_registry.end())
    {
        response = it->second(sc); // Execute the lambda passing the whole struct
    }
    else
    {
        response = "Command '" + sc.cmd + "' not found in registry.";
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

    if (msg.contains("payload"))
    {
        try
        {
            auto const& payload = msg.at("payload").as_object();

            // 1. Extract Numeric Data (using int64 for uint64_t compatibility)
            int sensor_id       = static_cast<int>(payload.at("sensor_id").as_int64());
            uint64_t request_id = static_cast<uint64_t>(payload.at("request_id").as_int64());

            // 2. Extract Strings using value_to for safety
            std::string action   = boost::json::value_to<std::string>(payload.at("action"));
            std::string hostname = boost::json::value_to<std::string>(payload.at("hostname"));
            std::string new_ip   = boost::json::value_to<std::string>(payload.at("new_ip"));

            // 3. Trace Data
            std::cout << "[CONFIG-EVENT] Received Request ID: " << request_id << std::endl;
            std::cout << " > Action: " << action << " | Sensor: " << sensor_id << std::endl;
            std::cout << " > New IP: " << new_ip << " | Hostname: " << hostname << std::endl;

            // --- EVENTO: FINALIZACIÓN DE PETICIÓN DE CONFIGURACIÓN ---
            // Despierta el hilo que originó la petición desde la línea de comandos (CLI).
            // Se notifica el estatus (SUCCESS) para liberar el bloqueo 'wait_for_response'
            // y permitir que el usuario reciba la confirmación del cambio en tiempo real.
            std::cout << "[MAIN] Dispatching SUCCESS for Request ID: " << request_id << std::endl;
            g_dispatcher.dispatch(request_id, ResponseStatus::SUCCESS);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[JSON-Error] Failed to parse config payload: " << e.what() << std::endl;
        }
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
}

/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'state_events' channel.
 */
void on_db_error_event_received(boost::json::object msg)
{
    if (msg.empty()) return;

    std::cout << "************ ERROR DATABASE EVENT! *************" << std::endl;

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
        std::cout << "[MAIN] Connection established with Keep-Alive (60s/5s/3)." << std::endl;

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

        std::cout << "[MAIN] Starting async listener for PostgreSQL notifications..." << std::endl;

        g_db->register_listen_async("config_events", on_db_config_event_received);
        g_db->register_listen_async("config_errors", on_db_error_event_received);
        g_db->register_listen_async("state_events", on_db_state_event_received);
        g_db->run_listener_loop();

        // Esperamos a que el servidor termine (en este caso, se ejecutará indefinidamente hasta recibir una señal de interrupción)
        std::cout << "[MAIN] WAIT UNTIL SERVER IO FINALIZE" << std::endl;

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
