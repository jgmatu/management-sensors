#include <fstream>

#include <net/QuantumSafeTlsEngine.hpp>
#include <log/Log.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/core/flat_buffer.hpp>

QuantumSafeTlsEngine::QuantumSafeTlsEngine(uint16_t port,
                                           const std::string& cert_path,
                                           const std::string& key_path,
                                           const std::string& policy_path,
                                           uint64_t ocsp_cache_time,
                                           uint64_t ocsp_timeout)
    : port_(port),
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
        boost::asio::co_spawn(
            *io_context_,
            do_listen(endpoint_, tls_context_, ocsp_cache_),
            make_final_completion_handler("Acceptor")
        );

        const auto num_threads = std::thread::hardware_concurrency();

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

    if (io_context_) {
        io_context_->stop();
    }

    thread_pool_.clear(); 
    logging::Logger::instance().info("tls-engine", "Orchestrated shutdown complete");
}

void QuantumSafeTlsEngine::set_session_handler(std::shared_ptr<ISessionHandler> handler)
{
    session_handler_ = std::move(handler);
}

boost::asio::awaitable<void> QuantumSafeTlsEngine::do_session(
    AwaitableTcpStream stream,
    std::shared_ptr<Botan::TLS::Context> ctx,
    std::shared_ptr<OCSP_Cache> ocsp_cache)
{
    auto callbacks = std::make_shared<TlsHttpCallbacks>(ocsp_cache);

    TlsStream tls_stream(stream, ctx, callbacks);

    try
    {
        co_await tls_stream.async_handshake(Botan::TLS::Connection_Side::Server);

        const auto connection_details = callbacks->collect_connection_details_as_json();
        {
            std::lock_guard<std::mutex> lock(connection_details_mutex_);
            latest_connection_details_ = connection_details;
        }
        logging::Logger::instance().info("tls-engine", "Connection details: " + connection_details);

        if (session_handler_)
        {
            co_await session_handler_->handle_session(tls_stream);
        }
    }
    catch (const std::exception&)
    {
    }

    co_await tls_stream.async_shutdown();
    tls_stream.next_layer().socket().shutdown(boost::asio::ip::tcp::socket::shutdown_send);
}

boost::asio::awaitable<void> QuantumSafeTlsEngine::do_listen(
        boost::asio::ip::tcp::endpoint endpoint,
        std::shared_ptr<Botan::TLS::Context> tls_ctx,
        std::shared_ptr<OCSP_Cache> ocsp_cache) 
{
    auto exec = co_await boost::asio::this_coro::executor;

    boost::asio::ip::tcp::acceptor acceptor(exec, endpoint);

    for (;;)
    {
        auto socket = co_await acceptor.async_accept();

        boost::asio::co_spawn(
            exec,
            do_session(
                AwaitableTcpStream(std::move(socket)),
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
