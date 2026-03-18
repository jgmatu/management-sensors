/**
 * Botan post-quantum TLS test server
 *
 * (C) 2023 René Fischer, René Meusel
 *
 * This implementation is roughly based on this BSL-licensed boost beast example
 * by Klemens D. Morgenstern:
 *   www.boost.org/doc/libs/1_83_0/libs/beast/example/http/server/awaitable/http_server_awaitable.cpp
 */

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <map>


#include <db/DatabaseManager.hpp>
#include <net/QuantumSafeTlsEngine.hpp>
#include <json/JsonUtils.hpp>
#include <dispatcher/Dispatcher.hpp>
#include <cli/SensorCommandCli.hpp>
#include <log/Log.hpp>

#define REQUEST_TIMEOUT_MS 2 * 1000

// Global database handle shared between TLS engine worker threads and main.
std::shared_ptr<DatabaseManager> g_db;
Dispatcher g_dispatcher;

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
        uint64_t request_id = g_dispatcher.generate_id();

        logging::Logger::instance().info(
            "server",
            "[CLI] New CONFIG_IP request created | sensor_id=" +
                std::to_string(sc.id) +
                " | request_id=" + std::to_string(request_id)
        );

        g_db->add_pending_config(sc.id, "sensor-" + std::to_string(sc.id), sc.value, true, request_id);

        logging::Logger::instance().info(
            "server",
            "[CLI] Waiting for DB confirmation | request_id=" +
                std::to_string(request_id) +
                " | timeout_ms=" + std::to_string(REQUEST_TIMEOUT_MS)
        );

        auto status = g_dispatcher.wait_for_response(request_id, REQUEST_TIMEOUT_MS);

        logging::Logger::instance().info(
            "server",
            "[CLI] DB wait finished | request_id=" +
                std::to_string(request_id) +
                " | status=" + status_to_string(status)
        );

        if (status == ResponseStatus::SUCCESS) {
            return "OK: Sensor " + std::to_string(sc.id) + " updated successfully.";
        }
        return "ERROR: " + status_to_string(status) + " (ID: " + std::to_string(request_id) + ")";
    }},
    {"REBOOT", [](const SensorCommand& sc) -> std::string {
        return "OK: Rebooting sensor " + std::to_string(sc.id);
    }}
};



/**
 * @brief Process one decrypted TLS request and build a plaintext reply.
 *
 * This function runs in the TLS server worker threads. It parses the
 * incoming CLI-like command, dispatches it to the corresponding handler
 * in `command_registry`, and returns the resulting message as bytes.
 */
 std::vector<uint8_t> on_tls_message_process(const std::vector<uint8_t>& input) {
    if (input.empty()) return {};
    const std::string request(input.begin(), input.end());
    SensorCommandCli cli(request);
    // Señal de salida del cliente de pruebas:
    if (!cli.command_.valid && cli.command_.cmd == "quit") {
        return {};
    }
    std::string response;
    logging::Logger::instance().info(
        "server",
        "[CLI] Command received: " + JsonUtils::toString(
            boost::json::value{
                { "cmd",    cli.command_.cmd },
                { "id",     cli.command_.id },
                { "attr",   cli.command_.attr },
                { "value",  cli.command_.value },
                { "valid",  cli.command_.valid }
            }
        )
    );
    auto it = command_registry.find(cli.command_.cmd);
    if (it != command_registry.end()) {
        response = it->second(cli.command_);
    } else {
        response = "Command '" + cli.command_.cmd + "' not found in registry.";
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
 
     logging::Logger::instance().info("server", "************ COMMITTED CONFIG EVENT *************");
 
     std::string_view channel = msg.at("channel").as_string();
     logging::Logger::instance().info(
         "server",
         "[DB-Handler] Event on channel: " + std::string(channel)
     );
 
     logging::Logger::instance().info(
         "server",
         "[DB-Handler] Full JSON: " + JsonUtils::toString(msg)
     );
 
     if (msg.contains("payload"))
     {
         try
         {
             auto const& payload = msg.at("payload").as_object();
 
             int sensor_id       = static_cast<int>(payload.at("sensor_id").as_int64());
             uint64_t request_id = static_cast<uint64_t>(payload.at("request_id").as_int64());
 
             std::string action   = boost::json::value_to<std::string>(payload.at("action"));
             std::string hostname = boost::json::value_to<std::string>(payload.at("hostname"));
             std::string new_ip   = boost::json::value_to<std::string>(payload.at("new_ip"));
 
             logging::Logger::instance().info(
                 "server",
                 "[CONFIG-EVENT] Received Request ID: " + std::to_string(request_id)
             );
             logging::Logger::instance().info(
                 "server",
                 " > Action: " + action +
                     " | Sensor: " + std::to_string(sensor_id)
             );
             logging::Logger::instance().info(
                 "server",
                 " > New IP: " + new_ip + " | Hostname: " + hostname
             );
 
             logging::Logger::instance().info(
                 "server",
                 "[MAIN] Dispatching SUCCESS for Request ID: " + std::to_string(request_id)
             );
             g_dispatcher.dispatch(request_id, ResponseStatus::SUCCESS);
         }
         catch (const std::exception& e)
         {
             logging::Logger::instance().error(
                 "server",
                 std::string("[JSON-Error] Failed to parse config payload: ") + e.what()
             );
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
     logging::Logger::instance().info(
         "server",
         "************ STATE TELEMETRY EVENT *************"
     );
     std::string_view channel = msg.at("channel").as_string();
     logging::Logger::instance().info(
         "server",
         "[DB-Handler] Event on channel: " + std::string(channel)
     );
     logging::Logger::instance().info(
         "server",
         "[DB-Handler] State JSON: " + JsonUtils::toString(msg)
     );
 }
/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'state_events' channel.
 */
 void on_db_error_event_received(boost::json::object msg)
 {
     if (msg.empty()) return;
     logging::Logger::instance().error(
         "server",
         "************ ERROR DATABASE EVENT *************"
     );
     std::string_view channel = msg.at("channel").as_string();
     logging::Logger::instance().error(
         "server",
         "[DB-Handler] Event on channel: " + std::string(channel)
     );
     logging::Logger::instance().error(
         "server",
         "[DB-Handler] Error JSON: " + JsonUtils::toString(msg)
     );
     if (msg.contains("type") && msg.at("type").as_string() == "alarm") {
         logging::Logger::instance().error(
             "server",
             "!!! SYSTEM ALARM RECEIVED FROM DATABASE !!!"
         );
     }
 }

int main(int argc, char* argv[])
{
    logging::Logger::instance().info("server", "Creating logs directory");

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

    logging::Logger::instance().info("server", "Port: " + std::to_string(vm["port"].as<uint16_t>()));
    logging::Logger::instance().info("server", "Policy: " + vm["policy"].as<std::string>());
    logging::Logger::instance().info("server", "Certificate: " + vm["cert"].as<std::string>());
    logging::Logger::instance().info("server", "Key: " + vm["key"].as<std::string>());
    logging::Logger::instance().info("server", "OCSP request timeout: " + std::to_string(vm["ocsp-request-timeout"].as<uint64_t>()));
    logging::Logger::instance().info("server", "OCSP cache time: " + std::to_string(vm["ocsp-cache-time"].as<uint64_t>()));
    logging::Logger::instance().info("server", "Document root: " + vm["document-root"].as<std::string>());

    // ... Initialize and start your QuantumSafeTlsEngine ...
    try
    {
        const auto port = vm["port"].as<uint16_t>();
        const auto policy = vm["policy"].as<std::string>();
        const auto certificate = vm["cert"].as<std::string>();
        const auto key = vm["key"].as<std::string>();
        const auto ocsp_cache_time = vm["ocsp-cache-time"].as<uint64_t>();
        const auto ocsp_request_timeout = vm["ocsp-request-timeout"].as<uint64_t>();
        QuantumSafeTlsEngine server(port, certificate, key, policy,
            ocsp_cache_time,ocsp_request_timeout);

        server.set_processor(on_tls_message_process);
        server.initialize();

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

        g_db = std::make_shared<DatabaseManager>(conn_str);
        g_db->connect();
        g_db->register_listen_async("config_events", on_db_config_event_received);
        g_db->register_listen_async("state_events", on_db_state_event_received);
        g_db->register_listen_async("error_events", on_db_error_event_received);
        g_db->run_listener_loop();

        boost::json::object sanity_info = g_db->get_sanity_info();
        logging::Logger::instance().info("server", JsonUtils::toString(sanity_info));

        server.join();
        server.stop();
        g_db.reset();
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
