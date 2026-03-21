#include <iostream>
#include <fstream>
#include <memory>
#include <string>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/program_options.hpp>

#include <botan/asio_stream.h>
#include <botan/auto_rng.h>
#include <botan/certstor_flatfile.h>
#include <botan/pkcs8.h>
#include <botan/tls_session_manager_memory.h>
#include <botan/tls_policy.h>
#include <botan/x509cert.h>

#include <log/Log.hpp>

namespace net   = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace po    = boost::program_options;
using tcp       = net::ip::tcp;

using TlsStream = Botan::TLS::Stream<tcp::socket&>;

class Proxy_Credentials_Manager final : public Botan::Credentials_Manager
{
public:
    Proxy_Credentials_Manager(const std::string& ca_path,
                              const std::string& cert_path,
                              const std::string& key_path)
        : ca_store_(ca_path)
    {
        logging::Logger::instance().debug("proxy",
            "[Creds] Loading private key from " + key_path);
        Botan::DataSource_Stream key_in(key_path);
        key_ = Botan::PKCS8::load_key(key_in);
        logging::Logger::instance().debug("proxy",
            "[Creds] Key algo: " + key_->algo_name());

        logging::Logger::instance().debug("proxy",
            "[Creds] Loading certificate chain from " + cert_path);
        Botan::DataSource_Stream cert_in(cert_path);
        while (!cert_in.end_of_data())
        {
            try { certs_.push_back(Botan::X509_Certificate(cert_in)); }
            catch (std::exception&) {}
        }
        logging::Logger::instance().debug("proxy",
            "[Creds] Loaded " + std::to_string(certs_.size()) + " certificate(s)");
        for (size_t i = 0; i < certs_.size(); ++i)
        {
            logging::Logger::instance().debug("proxy",
                "[Creds] cert[" + std::to_string(i) + "] subject: "
                + certs_[i].subject_dn().to_string()
                + " | issuer: " + certs_[i].issuer_dn().to_string());
        }

        logging::Logger::instance().debug("proxy",
            "[Creds] CA store loaded from " + ca_path);
    }

    std::vector<Botan::Certificate_Store*>
    trusted_certificate_authorities(const std::string& type,
                                    const std::string& context) override
    {
        logging::Logger::instance().debug("proxy",
            "[Creds] trusted_certificate_authorities called | type="
            + type + " context=" + context);
        if (type == "tls-client")
            return { &ca_store_ };
        return {};
    }

    std::vector<Botan::X509_Certificate> find_cert_chain(
        const std::vector<std::string>& algos,
        const std::vector<Botan::AlgorithmIdentifier>& /*cert_sig_schemes*/,
        const std::vector<Botan::X509_DN>& /*acceptable_cas*/,
        const std::string& type,
        const std::string& hostname) override
    {
        std::string algo_list;
        for (const auto& a : algos) algo_list += a + " ";
        logging::Logger::instance().debug("proxy",
            "[Creds] find_cert_chain called | type=" + type
            + " hostname=" + hostname + " algos=[" + algo_list + "]");
        logging::Logger::instance().debug("proxy",
            "[Creds] Returning " + std::to_string(certs_.size()) + " cert(s)");
        return certs_;
    }

    std::shared_ptr<Botan::Private_Key> private_key_for(
        const Botan::X509_Certificate& cert,
        const std::string& type,
        const std::string& context) override
    {
        logging::Logger::instance().debug("proxy",
            "[Creds] private_key_for called | type=" + type
            + " context=" + context
            + " cert_subject=" + cert.subject_dn().to_string());
        bool match = !certs_.empty() && certs_.front() == cert;
        logging::Logger::instance().debug("proxy",
            "[Creds] Key match: " + std::string(match ? "YES" : "NO"));
        if (match) return key_;
        return nullptr;
    }

private:
    Botan::Flatfile_Certificate_Store ca_store_;
    std::vector<Botan::X509_Certificate> certs_;
    std::shared_ptr<Botan::Private_Key> key_;
};

std::shared_ptr<Botan::TLS::Policy>
load_tls_policy(const std::string& policy_path)
{
    if (policy_path == "default" || policy_path.empty())
        return std::make_shared<Botan::TLS::Policy>();

    std::ifstream in(policy_path, std::ios::in);
    return std::make_shared<Botan::TLS::Text_Policy>(in);
}

class Debug_TLS_Callbacks final : public Botan::TLS::StreamCallbacks
{
public:
    void tls_inspect_handshake_msg(
        const Botan::TLS::Handshake_Message& /*msg*/) override
    {
        logging::Logger::instance().debug("proxy",
            "[TLS-CB] Handshake message received");
    }

    void tls_log_error(const char* msg) override
    {
        logging::Logger::instance().error("proxy",
            "[TLS-CB] Error: " + std::string(msg));
    }

    void tls_log_debug(const char* msg) override
    {
        logging::Logger::instance().debug("proxy",
            "[TLS-CB] " + std::string(msg));
    }

    void tls_verify_cert_chain(
        const std::vector<Botan::X509_Certificate>& cert_chain,
        const std::vector<std::optional<Botan::OCSP::Response>>& ocsp_responses,
        const std::vector<Botan::Certificate_Store*>& trusted_roots,
        Botan::Usage_Type usage,
        std::string_view hostname,
        const Botan::TLS::Policy& policy) override
    {
        logging::Logger::instance().debug("proxy",
            "[TLS-CB] tls_verify_cert_chain called | hostname="
            + std::string(hostname) + " chain_len="
            + std::to_string(cert_chain.size())
            + " trusted_roots=" + std::to_string(trusted_roots.size())
            + " usage=" + std::to_string(static_cast<int>(usage)));

        for (size_t i = 0; i < cert_chain.size(); ++i)
        {
            logging::Logger::instance().debug("proxy",
                "[TLS-CB] server cert[" + std::to_string(i) + "] subject="
                + cert_chain[i].subject_dn().to_string()
                + " issuer=" + cert_chain[i].issuer_dn().to_string()
                + " not_before=" + cert_chain[i].not_before().to_string()
                + " not_after=" + cert_chain[i].not_after().to_string());
        }

        try
        {
            Botan::TLS::StreamCallbacks::tls_verify_cert_chain(
                cert_chain, ocsp_responses, trusted_roots, usage,
                hostname, policy);
            logging::Logger::instance().info("proxy",
                "[TLS-CB] Server certificate chain verification PASSED");
        }
        catch (const std::exception& e)
        {
            logging::Logger::instance().error("proxy",
                "[TLS-CB] Certificate verification FAILED: "
                + std::string(e.what()));
            throw;
        }
    }
};

// Establishes and maintains the persistent TLS connection to the backend.
net::awaitable<void> connect_backend(
    tcp::socket& backend_socket,
    TlsStream& tls_stream,
    const std::string& host,
    const std::string& port)
{
    auto exec = co_await net::this_coro::executor;

    tcp::resolver resolver(exec);
    logging::Logger::instance().debug("proxy",
        "[TLS] Resolving " + host + ":" + port + "...");

    auto endpoints = co_await resolver.async_resolve(
        host, port, net::use_awaitable);

    co_await net::async_connect(backend_socket, endpoints, net::use_awaitable);

    logging::Logger::instance().info("proxy",
        "[TLS] TCP connected to " + host + ":" + port);

    logging::Logger::instance().debug("proxy",
        "[TLS] Starting TLS handshake as CLIENT...");

    try
    {
        co_await tls_stream.async_handshake(
            Botan::TLS::Connection_Side::Client, net::use_awaitable);
    }
    catch (const Botan::TLS::TLS_Exception& e)
    {
        logging::Logger::instance().error("proxy",
            "[TLS] Botan TLS_Exception during handshake: "
            + std::string(e.what())
            + " | error_type=" + std::to_string(static_cast<int>(e.error_type())));
        throw;
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("proxy",
            "[TLS] Exception during handshake: " + std::string(e.what()));
        throw;
    }

    logging::Logger::instance().info("proxy",
        "[TLS] Handshake complete (persistent connection ready)");
}

// Handles one HTTP request/response cycle over the persistent TLS connection.
net::awaitable<void> do_session(
    tcp::socket frontend_socket,
    TlsStream& tls_stream)
{
    const auto remote = frontend_socket.remote_endpoint();
    const std::string peer = remote.address().to_string() + ":"
        + std::to_string(remote.port());

    logging::Logger::instance().info("proxy",
        "[TCP] Accepted " + peer);

    try
    {
        beast::flat_buffer fe_buf;
        http::request<http::string_body> req;

        co_await http::async_read(
            frontend_socket, fe_buf, req, net::use_awaitable);

        logging::Logger::instance().info("proxy",
            "[FE->BE] " + std::string(req.method_string()) + " "
            + std::string(req.target())
            + " (" + std::to_string(req.body().size()) + " bytes body)");

        req.keep_alive(true);

        co_await http::async_write(
            tls_stream, req, net::use_awaitable);

        logging::Logger::instance().debug("proxy",
            "[FE->BE] Request forwarded to backend");

        beast::flat_buffer be_buf;
        http::response<http::string_body> res;

        co_await http::async_read(
            tls_stream, be_buf, res, net::use_awaitable);

        logging::Logger::instance().info("proxy",
            "[BE->FE] " + std::to_string(static_cast<unsigned>(res.result()))
            + " " + std::string(res.reason())
            + " (" + std::to_string(res.body().size()) + " bytes body)");

        res.keep_alive(false);

        co_await http::async_write(
            frontend_socket, res, net::use_awaitable);

        logging::Logger::instance().debug("proxy",
            "[BE->FE] Response sent to " + peer);
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("proxy",
            "[Session] " + peer + " error: " + e.what());
    }

    boost::system::error_code ec;
    [[maybe_unused]] auto r = frontend_socket.shutdown(
        tcp::socket::shutdown_both, ec);
}

net::awaitable<void> do_listen(
    tcp::endpoint endpoint,
    TlsStream& tls_stream)
{
    auto exec = co_await net::this_coro::executor;
    tcp::acceptor acceptor(exec, endpoint);

    logging::Logger::instance().info("proxy",
        "Listening on 127.0.0.1:" + std::to_string(endpoint.port()));

    for (;;)
    {
        tcp::socket socket = co_await acceptor.async_accept();

        net::co_spawn(exec,
            do_session(std::move(socket), tls_stream),
            net::detached);
    }
}

int main(int argc, char* argv[])
{
    logging::Logger::instance().set_process_name("proxy");

    po::options_description desc("TLS PQC Proxy options");
    desc.add_options()
        ("help",         "Show help")
        ("listen-port",  po::value<uint16_t>()->required(),
                         "Port to listen on for plain HTTP")
        ("backend-host", po::value<std::string>()->default_value("localhost"),
                         "Backend TLS server host")
        ("backend-port", po::value<std::string>()->default_value("50443"),
                         "Backend TLS server port")
        ("ca-cert",      po::value<std::string>()->required(),
                         "CA certificate to validate the backend")
        ("cert",         po::value<std::string>()->required(),
                         "Client certificate (same as the server)")
        ("key",          po::value<std::string>()->required(),
                         "Client private key (same as the server)")
        ("policy",       po::value<std::string>()->default_value("default"),
                         "TLS policy file (or 'default')");

    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        if (vm.count("help"))
        {
            std::cout << desc << '\n';
            return 0;
        }
        po::notify(vm);
    }
    catch (const po::error& e)
    {
        std::cerr << "Error: " << e.what() << "\n\n" << desc << '\n';
        return 1;
    }

    const auto listen_port  = vm["listen-port"].as<uint16_t>();
    const auto backend_host = vm["backend-host"].as<std::string>();
    const auto backend_port = vm["backend-port"].as<std::string>();
    const auto ca_cert      = vm["ca-cert"].as<std::string>();
    const auto cert_path    = vm["cert"].as<std::string>();
    const auto key_path     = vm["key"].as<std::string>();
    const auto policy_path  = vm["policy"].as<std::string>();

    logging::Logger::instance().info("proxy",
        "Listen port: " + std::to_string(listen_port));
    logging::Logger::instance().info("proxy",
        "Backend: " + backend_host + ":" + backend_port);

    try
    {
        logging::Logger::instance().info("proxy", "[Init] Creating RNG...");
        auto rng = std::make_shared<Botan::AutoSeeded_RNG>();

        logging::Logger::instance().info("proxy",
            "[Init] Loading credentials: CA=" + ca_cert
            + " cert=" + cert_path + " key=" + key_path);
        auto creds = std::make_shared<Proxy_Credentials_Manager>(
            ca_cert, cert_path, key_path);

        logging::Logger::instance().info("proxy", "[Init] Creating TLS session manager...");
        auto session_mgr =
            std::make_shared<Botan::TLS::Session_Manager_In_Memory>(rng);

        logging::Logger::instance().info("proxy",
            "[Init] Loading TLS policy: " + policy_path);
        auto policy = load_tls_policy(policy_path);

        logging::Logger::instance().info("proxy", "[Init] Building TLS context...");
        auto tls_ctx = std::make_shared<Botan::TLS::Context>(
            creds, rng, session_mgr, policy);

        net::io_context ioc;

        logging::Logger::instance().info("proxy",
            "[Init] Preparing persistent TLS connection to "
            + backend_host + ":" + backend_port);
        tls_ctx->set_server_info(
            Botan::TLS::Server_Information(backend_host,
                static_cast<uint16_t>(std::stoi(backend_port))));
        logging::Logger::instance().debug("proxy",
            "[Init] SNI set to " + backend_host + ":" + backend_port);

        tcp::socket backend_socket(ioc);
        auto callbacks = std::make_shared<Debug_TLS_Callbacks>();
        TlsStream tls_stream(backend_socket, tls_ctx, callbacks);

        tcp::endpoint listen_ep{
            net::ip::make_address("127.0.0.1"), listen_port};

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait(
            [&](const boost::system::error_code&, int)
            { ioc.stop(); });

        net::co_spawn(ioc,
            [&]() -> net::awaitable<void>
            {
                co_await connect_backend(
                    backend_socket, tls_stream,
                    backend_host, backend_port);

                co_await do_listen(listen_ep, tls_stream);
            }(),
            [&](std::exception_ptr ep)
            {
                if (ep)
                {
                    try { std::rethrow_exception(ep); }
                    catch (const std::exception& e)
                    {
                        logging::Logger::instance().error("proxy",
                            "[Init] " + std::string(e.what()));
                    }
                    ioc.stop();
                }
            });

        ioc.run();
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("proxy",
            "Fatal: " + std::string(e.what()));
        return 1;
    }

    return 0;
}
