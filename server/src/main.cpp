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

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/http.hpp>
#include <boost/program_options.hpp>

#include <botan/asio_stream.h>
#include <botan/auto_rng.h>
#include <botan/pkcs8.h>
#include <botan/tls_session_manager_memory.h>
#include <botan/version.h>

#include <oscp/ocsp_cache.hpp>
#include <db/DatabaseManager.hpp>
#include <net/QuantumSafeTlsEngine.hpp>

#include <json/JsonUtils.hpp>

// #define SESSION_EXPIRED_TIMEOUT

namespace
{



}  // namespace

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
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, desc), vm);

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

    try
    {
        const auto port = vm["port"].as<uint16_t>();
        const auto policy = vm["policy"].as<std::string>();
        const auto certificate = vm["cert"].as<std::string>();
        const auto key = vm["key"].as<std::string>();
        const auto ocsp_cache_time = vm["ocsp-cache-time"].as<uint64_t>();
        const auto ocsp_request_timeout = vm["ocsp-request-timeout"].as<uint64_t>();

        QuantumSafeTlsEngine server(port, certificate, key, policy, ocsp_cache_time, ocsp_request_timeout);
        server.initialize();

        // Ejemplo con SSL requerido y Keep-Alive activo
        DatabaseManager db(
            "dbname=javi "
            "user=javi "
            "password=12345678 "
            "host=localhost "
            "port=5432 "
            "keepalives=1 "             // Activa Keep-Alive a nivel de TCP
            "keepalives_idle=60 "       // Segundos antes de enviar el primer keepalive
            "keepalives_interval=5 "    // Segundos entre reintentos si no hay respuesta
            "keepalives_count=3"        // Número de fallos antes de cerrar la conexión
        );
        db.connect();

        boost::json::object sanity_info = db.get_sanity_info();
        JsonUtils::print(std::cout, sanity_info);
        std::cout << std::endl;

        std::cout << "Starting async listener for PostgreSQL notifications..." << std::endl;
        db.listen_async("state_events", [](boost::json::object msg)
        {
            std::cout << "Received notification on channel: " << msg["channel"].as_string() << ": " << std::endl;
            JsonUtils::print(std::cout, msg);
            std::cout << std::endl;
        });

        // Esperamos a que el servidor termine (en este caso, se ejecutará indefinidamente hasta recibir una señal de interrupción)
        server.join();
        server.stop();

        // 1. Iniciamos el cierre físico de la conexión.
        // Al resetear el puntero bajo el mutex, cualquier hilo (como el listener) 
        // que esté bloqueado en wait_notification() recibirá inmediatamente 
        // una excepción pqxx::broken_connection, forzando su salida.
        db.disconnect();

        // 2. Sincronización y limpieza de hilos.
        // Esperamos a que todos los hilos de fondo (jthreads) terminen su ejecución 
        // tras haber capturado la desconexión o la señal de parada. 
        // Esto garantiza que no queden punteros colgantes ('dangling pointers') 
        // ni fugas de recursos al cerrar el proceso.
        db.join();

        std::cout << "[System] Thread pool joined and I/O context finalized." << std::endl;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
