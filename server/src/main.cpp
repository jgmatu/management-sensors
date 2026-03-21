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

#include <db/DatabaseManager.hpp>
#include <http/QuantumSafeHttp.hpp>
#include <jwt/JwtManager.hpp>
#include <net/QuantumSafeTlsEngine.hpp>
#include <json/JsonUtils.hpp>
#include <dispatcher/Dispatcher.hpp>
#include <cli/QuantumCommandCli.hpp>
#include <log/Log.hpp>

// Global database handle shared between TLS engine worker threads and main.
std::shared_ptr<DatabaseManager> g_db;
Dispatcher g_dispatcher;

/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'config_events' channel.
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
 * Processes JSON payloads from the PostgreSQL 'error_events' channel.
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
    logging::Logger::instance().set_process_name("server");

    // clang-format off
    boost::program_options::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("port", boost::program_options::value<uint16_t>()->required(), "Port to use")
        ("policy", boost::program_options::value<std::string>()->default_value("default"), "Botan policy file (default: Botan's default policy)")
        ("cert", boost::program_options::value<std::string>()->required(), "Path to the server's certificate chain")
        ("key", boost::program_options::value<std::string>()->required(), "Path to the server's certificate private key file")
        ("mode", boost::program_options::value<std::string>()->default_value("raw"), "Session mode: raw | http")
        ("ocsp-request-timeout", boost::program_options::value<uint64_t>()->default_value(10), "OCSP request timeout in seconds")
        ("ocsp-cache-time", boost::program_options::value<uint64_t>()->default_value(6 * 60), "Cache validity time for OCSP responses in minutes")
        ("document-root", boost::program_options::value<std::string>()->default_value("webroot"), "Path to the server's static documents folder")
        ("jwt-key", boost::program_options::value<std::string>()->default_value(""), "Path to the JWT ES384 private key (enables API auth)")
        ("jwt-cert", boost::program_options::value<std::string>()->default_value(""), "Path to the JWT ES384 certificate (public key)");
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
    logging::Logger::instance().info("server", "Mode: " + vm["mode"].as<std::string>());
    logging::Logger::instance().info("server", "OCSP request timeout: " + std::to_string(vm["ocsp-request-timeout"].as<uint64_t>()));
    logging::Logger::instance().info("server", "OCSP cache time: " + std::to_string(vm["ocsp-cache-time"].as<uint64_t>()));
    logging::Logger::instance().info("server", "Document root: " + vm["document-root"].as<std::string>());

    try
    {
        const auto port = vm["port"].as<uint16_t>();
        const auto policy = vm["policy"].as<std::string>();
        const auto certificate = vm["cert"].as<std::string>();
        const auto key = vm["key"].as<std::string>();
        const auto mode = vm["mode"].as<std::string>();
        const auto ocsp_cache_time = vm["ocsp-cache-time"].as<uint64_t>();
        const auto ocsp_request_timeout = vm["ocsp-request-timeout"].as<uint64_t>();
        const auto document_root = vm["document-root"].as<std::string>();
        const auto jwt_key_path  = vm["jwt-key"].as<std::string>();
        const auto jwt_cert_path = vm["jwt-cert"].as<std::string>();

        auto server = std::make_shared<QuantumSafeTlsEngine>(
            port, certificate, key, policy, ocsp_cache_time, ocsp_request_timeout);

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
        g_db->init_request_id_sequence();
        logging::Logger::instance().info("server",
            "[MAIN] request_id_seq initialized from database");
        g_db->register_listen_async("config_events", on_db_config_event_received);
        g_db->register_listen_async("state_events", on_db_state_event_received);
        g_db->register_listen_async("error_events", on_db_error_event_received);
        g_db->run_listener_loop();

        boost::json::object sanity_info = g_db->get_sanity_info();
        logging::Logger::instance().info("server", JsonUtils::toString(sanity_info));

        // Create the protocol-specific session handler
        if (mode == "http")
        {
            std::shared_ptr<JwtManager> jwt;
            if (!jwt_key_path.empty() && !jwt_cert_path.empty())
            {
                jwt = std::make_shared<JwtManager>(jwt_key_path, jwt_cert_path);
                logging::Logger::instance().info("server",
                    "[MAIN] JWT authentication enabled (ES384)");
            }

            server->set_session_handler(
                std::make_shared<QuantumSafeHttp>(
                    std::static_pointer_cast<IConnDetailsProvider>(server),
                    document_root, &g_dispatcher, g_db, jwt));
        }
        else if (mode == "raw")
        {
            server->set_session_handler(
                std::make_shared<QuantumCommandCli>(g_dispatcher, g_db));
        }
        else
        {
            throw std::runtime_error("Invalid mode: " + mode + ". Use 'raw' or 'http'.");
        }

        server->initialize();
        server->join();
        server->stop();

        g_db.reset();
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
