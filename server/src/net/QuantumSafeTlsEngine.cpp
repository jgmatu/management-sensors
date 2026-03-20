#include <fstream>

#include <net/QuantumSafeTlsEngine.hpp>
#include <http/QuantumSafeHttp.hpp>
#include <log/Log.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/flat_buffer.hpp>

using beast_tcp_stream_with_default_awaitable_executor =
    typename boost::beast::tcp_stream::rebind_executor<
        boost::asio::use_awaitable_t<>::executor_with_default<
            boost::asio::any_io_executor>>::other;

/**
 * @brief Constructor inicializando el puerto por defecto.
 */
QuantumSafeTlsEngine::QuantumSafeTlsEngine(uint16_t port,
                                           const std::string& cert_path,
                                           const std::string& key_path,
                                           const std::string& policy_path,
                                           uint64_t ocsp_cache_time,
                                           uint64_t ocsp_timeout)
    : processor_(nullptr),
      port_(port),
      cert_path_(cert_path),
      key_path_(key_path),
      policy_path_(policy_path),
      ocsp_cache_time_(ocsp_cache_time),
      ocsp_timeout_(ocsp_timeout)
{
    creds_ = std::make_shared<Basic_Credentials_Manager>(cert_path, key_path);
    rng_ = std::make_shared<Botan::AutoSeeded_RNG>();
    session_mgr_ = std::make_shared<Botan::TLS::Session_Manager_In_Memory>(rng_);
    tls_policy_ = load_tls_policy(policy_path);
    tls_context_ = std::make_shared<Botan::TLS::Context>(creds_, rng_, session_mgr_, tls_policy_);
    ocsp_cache_ = std::make_shared<OCSP_Cache>(std::chrono::minutes(ocsp_cache_time),
        std::chrono::seconds(ocsp_timeout));
    endpoint_ = boost::asio::ip::tcp::endpoint(boost::asio::ip::make_address("0.0.0.0"), port);

    const auto num_threads = std::thread::hardware_concurrency();
    io_context_ = std::make_unique<boost::asio::io_context>(static_cast<int>(num_threads));
}

void QuantumSafeTlsEngine::initialize()
{
    try 
    {
        // 1. Priming the Acceptor (Non-blocking)
        boost::asio::co_spawn(
            *io_context_,
            do_listen(endpoint_, tls_context_, ocsp_cache_),
            make_final_completion_handler("Acceptor")
        );

        const auto num_threads = std::thread::hardware_concurrency();

        // 2. Launch background threads. 
        // Since thread_pool_ is a class attribute, they won't die here.
        for (size_t i = 0; i < num_threads; ++i)
        {
            thread_pool_.emplace_back([this]() { 
                io_context_->run();
            });
        }

        logging::Logger::instance().info("tls-engine", "Server ready (Running in background)");
    }
    catch (const std::exception& e) 
    {
        logging::Logger::instance().error("tls-engine", "Initialization failed: " + std::string(e.what()));
        throw;
    }
}

void QuantumSafeTlsEngine::join()
{
    logging::Logger::instance().info("tls-engine", "Main thread now waiting for server threads...");
    
    for (auto& thread : thread_pool_) 
    {
        if (thread.joinable()) 
        {
            thread.join();
        }
    }

    logging::Logger::instance().info("tls-engine", "All threads joined. Process can now exit.");
}

void QuantumSafeTlsEngine::stop()
{
    logging::Logger::instance().info("tls-engine", "Server shutdown requested");

    // 1. Terminate the event loop
    if (io_context_) {
        io_context_->stop();
    }

    // 2. Cleanup threads
    // jthreads join automatically, but clear() ensures order
    thread_pool_.clear(); 
    logging::Logger::instance().info("tls-engine", "Orchestrated shutdown complete");
}

boost::asio::awaitable<void> QuantumSafeTlsEngine::do_session(
    beast_tcp_stream_with_default_awaitable_executor stream,
    std::shared_ptr<Botan::TLS::Context> ctx,
    std::shared_ptr<OCSP_Cache> ocsp_cache)
{
    const size_t BUFFER_SIZE = 8192;

    // TlsHttpCallbacks is still needed for OCSP and logging handshake details
    auto callbacks = std::make_shared<TlsHttpCallbacks>(ocsp_cache);

    // Botan::Stream has a constructor that takes the Context directly
    Botan::TLS::Stream<beast_tcp_stream_with_default_awaitable_executor&> tls_stream(
        stream, ctx, callbacks);

    try
    {
        co_await tls_stream.async_handshake(Botan::TLS::Connection_Side::Server);

        // Log connection details once (optional)
        const auto connection_details = callbacks->collect_connection_details_as_json();
        {
            std::lock_guard<std::mutex> lock(connection_details_mutex_);
            latest_connection_details_ = connection_details;
        }
        logging::Logger::instance().info("tls-engine", "Connection details: " + connection_details);

        boost::beast::flat_buffer http_buffer;
        std::vector<uint8_t> raw_buffer(BUFFER_SIZE);
        const bool use_http_mode = (session_mode_ == SessionMode::Http && http_handler_);
        for (;;)
        {
            // Set the timeout.
#ifdef SESSION_EXPIRED_TIMEOUT
            tls_stream.next_layer().expires_after(std::chrono::seconds(30));
#endif

            if (use_http_mode)
            {
                boost::beast::http::request<boost::beast::http::string_body> req;
                co_await boost::beast::http::async_read(tls_stream, http_buffer, req);

                auto response = req.target().starts_with("/api")
                                    ? http_handler_->handle_api_request(std::move(req))
                                    : http_handler_->handle_request(
                                          std::move(req), document_root_);

                const auto keep_alive = response.keep_alive();
                co_await boost::beast::async_write(
                    tls_stream, std::move(response), boost::asio::use_awaitable);
                if (!keep_alive)
                {
                    break;
                }
            }
            else
            {
                // Read raw decrypted bytes
                size_t n =
                    co_await tls_stream.async_read_some(boost::asio::buffer(raw_buffer));

                std::vector<uint8_t> response;
                if (processor_)
                {
                    std::vector<uint8_t> incoming(
                        raw_buffer.begin(), raw_buffer.begin() + n);
                    response = processor_(incoming);
                }

                if (!response.empty())
                {
                    response.push_back('\n');
                    co_await tls_stream.async_write_some(
                        boost::asio::buffer(response));
                }
                std::copy(
                    raw_buffer.begin(),
                    raw_buffer.end(),
                    std::ostream_iterator<char>(std::cout, ""));
            }
        }
    }
    catch (const std::exception& e)
    {
        // std::cout << e.what() << std::endl;
        // Handle EOF or handshake failures gracefully
    }

    // Shut down the connection gracefully
    co_await tls_stream.async_shutdown();
    tls_stream.next_layer().socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send);

    // At this point the connection is closed gracefully
    // we ignore the error because the client might have
    // dropped the connection already.
}

boost::asio::awaitable<void> QuantumSafeTlsEngine::do_listen(
        boost::asio::ip::tcp::endpoint endpoint,
        std::shared_ptr<Botan::TLS::Context> tls_ctx,
        std::shared_ptr<OCSP_Cache> ocsp_cache) 
{
    // 1. Get the current executor from the coroutine context
    auto exec = co_await boost::asio::this_coro::executor;

    // 2. Use the executor to create the acceptor
    boost::asio::ip::tcp::acceptor acceptor(exec, endpoint);

    for (;;)
    {
        // 3. Accept the new connection
        auto socket = co_await acceptor.async_accept();

        // 4. Spawn the session using the retrieved executor 'exec'
        boost::asio::co_spawn(
            exec,
            do_session(
                beast_tcp_stream_with_default_awaitable_executor(std::move(socket)),
                tls_ctx,
                ocsp_cache),
            make_final_completion_handler("session")
        );
    }
}

std::shared_ptr<Botan::TLS::Policy> QuantumSafeTlsEngine::load_tls_policy(
    const std::string& policy_type)
{
    if (policy_type == "default" || policy_type.empty())
    {
        return std::make_shared<Botan::TLS::Policy>();
    }

    // if something we don't recognize, assume it's a file
    std::ifstream policy_stream(policy_type.c_str(), std::ios::in);
    return std::make_shared<Botan::TLS::Text_Policy>(policy_stream);
}

std::function<void(std::exception_ptr)> QuantumSafeTlsEngine::make_final_completion_handler(const std::string& context)
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
                logging::Logger::instance().error("tls-engine", "Error in " + context + ": " + ex.what());
            }
        }
    };
}

std::string QuantumSafeTlsEngine::get_latest_connection_details() const
{
    std::lock_guard<std::mutex> lock(connection_details_mutex_);
    return latest_connection_details_;
}

void QuantumSafeTlsEngine::set_session_mode(SessionMode mode)
{
    session_mode_ = mode;
}

void QuantumSafeTlsEngine::set_http_handler(
    std::shared_ptr<QuantumSafeHttp> http_handler, std::string document_root)
{
    http_handler_ = std::move(http_handler);
    document_root_ = std::move(document_root);
}