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
#include <net/QuantumSafeHttpServer.hpp>

#include <json/JsonUtils.hpp>

//#define HTTPS

namespace
{

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

using tcp_stream = typename beast::tcp_stream::rebind_executor<
    net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

inline std::shared_ptr<Botan::TLS::Policy> load_tls_policy(
    const std::string& policy_type)
{
    if (policy_type == "default" || policy_type.empty())
    {
        return std::make_shared<Botan::TLS::Policy>();
    }

    // if something we don't recognize, assume it's a file
    std::ifstream policy_stream(policy_type);
    if (!policy_stream.good())
    {
        throw std::runtime_error(
            "Unknown TLS policy: not a file or known short name");
    }

    return std::make_shared<Botan::TLS::Text_Policy>(policy_stream);
}

std::function<void(std::exception_ptr)> make_final_completion_handler(const std::string& context)
{
    return [=](std::exception_ptr e)
    {
        if (e)
        {
            try
            {
                std::rethrow_exception(std::move(e));
            }
            catch (const std::exception& ex)
            {
                const auto now = std::chrono::system_clock::now();
                const std::time_t t_c = std::chrono::system_clock::to_time_t(now);
                
                // Nota: std::ctime añade un salto de línea al final
                std::cerr << std::ctime(&t_c) << " " << context << ": "
                          << ex.what() << std::endl;
            }
        }
    };
}

net::awaitable<void> do_session(
    tcp_stream stream,
    std::shared_ptr<Botan::TLS::Context> ctx,
    std::shared_ptr<OCSP_Cache> ocsp_cache)
{
    // TlsHttpCallbacks is still needed for OCSP and logging handshake details
    auto callbacks = std::make_shared<TlsHttpCallbacks>(ocsp_cache);

    // Botan::Stream has a constructor that takes the Context directly
    Botan::TLS::Stream<tcp_stream&> tls_stream(stream, ctx, callbacks);

    try
    {
        co_await tls_stream.async_handshake(Botan::TLS::Connection_Side::Server);


        // Log connection details once (optional)
        std::cout << callbacks->collect_connection_details_as_json() << std::endl;

        // TCP LAYER: Read/Write Loop
        std::vector<uint8_t> buffer(16);
        for (;;)
        {
            // Set the timeout.
            tls_stream.next_layer().expires_after(std::chrono::seconds(30));

            // Read raw decrypted bytes
            size_t n = co_await tls_stream.async_read_some(net::buffer(buffer));

            std::copy(buffer.begin(), buffer.end(), std::ostream_iterator< char>(std::cout, ""));

            // Echo back to client using the TLS stream's native async send
            size_t bytes_sent = co_await tls_stream.async_write_some(net::buffer(buffer.data(), n));

            std::copy(buffer.begin(), buffer.end(), std::ostream_iterator< char>(std::cout, ""));
        }
    }
    catch (const std::exception& e)
    {
        std::cout << e.what() << std::endl;
        // Handle EOF or handshake failures gracefully
    }

    // Shut down the connection gracefully
    co_await tls_stream.async_shutdown();
    tls_stream.next_layer().socket().shutdown(tcp::socket::shutdown_send);

    // At this point the connection is closed gracefully
    // we ignore the error because the client might have
    // dropped the connection already.
}

net::awaitable<void> do_listen(
        tcp::endpoint endpoint,
        std::shared_ptr<Botan::TLS::Context> tls_ctx,
        std::shared_ptr<OCSP_Cache> ocsp_cache) 
    {
        // 1. Get the current executor from the coroutine context
        auto exec = co_await net::this_coro::executor;

        // 2. Use the executor to create the acceptor
        tcp::acceptor acceptor(exec, endpoint);

        for (;;)
        {
            // 3. Accept the new connection
            auto socket = co_await acceptor.async_accept();

            // 4. Spawn the session using the retrieved executor 'exec'
            std::cout << "Spawn async task and wait again to accept connection again!" << std::endl;
            net::co_spawn(
                exec,
                do_session(tcp_stream(std::move(socket)), tls_ctx, ocsp_cache),
                make_final_completion_handler("session")
            );
        }
    }

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

        auto creds =
            std::make_shared<Basic_Credentials_Manager>(certificate, key);
        auto rng = std::make_shared<Botan::AutoSeeded_RNG>();
        auto session_mgr =
            std::make_shared<Botan::TLS::Session_Manager_In_Memory>(rng);
        auto tls_policy = load_tls_policy(policy);

        const auto num_threads = std::thread::hardware_concurrency();
        net::io_context io{static_cast<int>(num_threads + 1)};
        auto address = net::ip::make_address("0.0.0.0");

        std::cout << "Spawn bind connection in async task and go to next code!" << std::endl;

        boost::asio::co_spawn(
            io,
            do_listen(
                tcp::endpoint{address, port},
                std::make_shared<Botan::TLS::Context>(creds, rng, session_mgr, tls_policy),
                std::make_shared<OCSP_Cache>(
                    std::chrono::minutes(vm["ocsp-cache-time"].as<uint64_t>()),
                    std::chrono::seconds(vm["ocsp-request-timeout"].as<uint64_t>()))
            ),
            make_final_completion_handler("Acceptor")
        );

        // Add thread pool to IO context asio scheduler.
        std::vector<std::jthread> threads;
        for (size_t i = 0; i < num_threads; ++i)
        {
            threads.emplace_back([&io]() { io.run(); });
        }

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

        std::cout << "SERVER READY!" << std::endl;
        io.run();
        std::cout << "SERVER SHUTDOWN!" << std::endl;

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

        // --- ORCHESTRATED THREAD POOL SHUTDOWN ---

        // 1. Terminate the Boost.Asio event loop.
        // This prevents any new tasks from being accepted and cancels
        // any pending timers or async operations immediately.
        io.stop();

        // 2. Synchronize and clean up the worker threads.
        // We iterate through the pool to ensure every worker thread
        // finishes its current execution frame. This prevents "zombie"
        // threads and ensures that all stack-allocated resources within
        // the lambda functions are safely destroyed before the main process exits.
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
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
