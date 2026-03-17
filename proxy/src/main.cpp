#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <botan/asio_stream.h>
#include <botan/auto_rng.h>
#include <botan/pkcs8.h>
#include <botan/tls_session_manager_memory.h>
#include <botan/tls_policy.h>

namespace net = boost::asio;
using tcp = net::ip::tcp;

using tcp_stream = typename boost::beast::tcp_stream::rebind_executor<
    net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

// Minimal credentials manager: loads a single certificate + key pair.
class Basic_Credentials_Manager final : public Botan::Credentials_Manager
{
   public:
    Basic_Credentials_Manager(const std::string& server_crt, const std::string& server_key)
    {
        Certificate_Info cert;

        Botan::DataSource_Stream key_in(server_key);
        cert.key = Botan::PKCS8::load_key(key_in);

        Botan::DataSource_Stream in(server_crt);
        while (!in.end_of_data())
        {
            try
            {
                cert.certs.push_back(Botan::X509_Certificate(in));
            }
            catch (std::exception&)
            {
                ;
            }
        }

        m_creds.push_back(cert);
    }

    std::vector<Botan::X509_Certificate> find_cert_chain(
        const std::vector<std::string>& algos,
        const std::vector<Botan::AlgorithmIdentifier>& /*cert_sig_schemes*/,
        const std::vector<Botan::X509_DN>& /*acceptable_cas*/,
        const std::string& /*type*/,
        const std::string& /*hostname*/) override
    {
        const auto cred = std::ranges::find_if(
            m_creds,
            [&](const auto& c)
            {
                return std::ranges::any_of(
                    algos,
                    [&](const auto& algo)
                    { return algo == c.key->algo_name(); });
            });

        return (cred != m_creds.end())
                   ? cred->certs
                   : std::vector<Botan::X509_Certificate>{};
    }

    std::shared_ptr<Botan::Private_Key> private_key_for(
        const Botan::X509_Certificate& cert,
        const std::string& /*type*/,
        const std::string& /*context*/) override
    {
        const auto cred = std::ranges::find_if(
            m_creds,
            [&](const auto& c) { return c.certs.front() == cert; });
        return (cred != m_creds.end()) ? cred->key : nullptr;
    }

   private:
    struct Certificate_Info
    {
        std::vector<Botan::X509_Certificate> certs;
        std::shared_ptr<Botan::Private_Key> key;
    };

    std::vector<Certificate_Info> m_creds;
};

std::shared_ptr<Botan::TLS::Policy> load_tls_policy()
{
    // For the proxy template we use Botan's default TLS 1.3-capable policy.
    return std::make_shared<Botan::TLS::Policy>();
}

net::awaitable<void> do_session(tcp_stream stream, std::shared_ptr<Botan::TLS::Context> tls_ctx)
{
    constexpr std::size_t buffer_size = 8192;

    auto callbacks = std::make_shared<Botan::TLS::StreamCallbacks>();
    Botan::TLS::Stream<tcp_stream&> tls_stream(stream, tls_ctx, callbacks);

    try
    {
        co_await tls_stream.async_handshake(Botan::TLS::Connection_Side::Client);

        std::vector<uint8_t> buffer(buffer_size);
        for (;;)
        {
            std::size_t n = co_await tls_stream.async_read_some(net::buffer(buffer));

            if (n == 0)
            {
                break;
            }

            std::string_view sv(
                reinterpret_cast<const char*>(buffer.data()), n);
            std::cout << "[Proxy] Received from client: " << sv << '\n';

            co_await tls_stream.async_write_some(net::buffer(buffer.data(), n));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Proxy] Session error: " << e.what() << '\n';
    }

    co_await tls_stream.async_shutdown();
    tls_stream.next_layer().socket().shutdown(tcp::socket::shutdown_both);
}

net::awaitable<void> do_listen(
    tcp::endpoint endpoint,
    std::shared_ptr<Botan::TLS::Context> tls_ctx)
{
    auto exec = co_await net::this_coro::executor;
    tcp::acceptor acceptor(exec, endpoint);

    for (;;)
    {
        tcp::socket socket = co_await acceptor.async_accept();

        net::co_spawn(exec,
            do_session(
                tcp_stream(std::move(socket)),
                tls_ctx),
            net::detached);
    }
}

int main(int argc, char* argv[])
{
    if (argc != 5)
    {
        std::cerr
            << "Usage: proxy <listen_port> <server_cert.pem> <server_key.pem> "
               "<backend_host:port>\n";
        return 1;
    }

    const auto listen_port =
        static_cast<uint16_t>(std::stoi(argv[1]));
    const std::string cert_path = argv[2];
    const std::string key_path = argv[3];
    const std::string backend = argv[4]; // not yet used in this template

    try
    {
        auto rng = std::make_shared<Botan::AutoSeeded_RNG>();
        auto creds =
            std::make_shared<Basic_Credentials_Manager>(cert_path, key_path);
        auto session_mgr =
            std::make_shared<Botan::TLS::Session_Manager_In_Memory>(rng);
        auto policy = load_tls_policy();

        auto tls_ctx = std::make_shared<Botan::TLS::Context>(
            creds, rng, session_mgr, policy);

        net::io_context ioc;
        tcp::endpoint endpoint{tcp::v4(), listen_port};

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait(
            [&](const boost::system::error_code&, int)
            {
                ioc.stop();
            });

        net::co_spawn(ioc, do_listen(endpoint, tls_ctx), net::detached);

        std::cout << "[Proxy] Listening on port " << listen_port
                  << " (TLS 1.3, client side only)\n";

        ioc.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "[Proxy] Fatal error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
