#pragma once

#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

// Botan TLS y Criptografía
#include <botan/tls_callbacks.h>
#include <botan/tls_policy.h>
#include <botan/x509cert.h>
#include <botan/rng.h>
#include <botan/credentials_manager.h> // Define Botan::Credentials_Manager

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/http.hpp>
#include <boost/program_options.hpp>

// Cabeceras propias del proyecto
#include <oscp/ocsp_cache.hpp>

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

using tcp_stream = typename beast::tcp_stream::rebind_executor<
    net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other;

class Basic_Credentials_Manager final : public Botan::Credentials_Manager
{
   public:
    Basic_Credentials_Manager(const std::string& server_crt,
                              const std::string& server_key)
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
            }
        }

        m_creds.push_back(cert);
    }

    std::vector<Botan::X509_Certificate> find_cert_chain(
        const std::vector<std::string>& algos,
        const std::vector<Botan::AlgorithmIdentifier>& cert_signature_schemes,
        const std::vector<Botan::X509_DN>& acceptable_cas,
        const std::string& type, const std::string& hostname) override
    {

        const auto cred = std::ranges::find_if(
            m_creds,
            [&](const auto& cred)
            {
                return std::ranges::any_of(
                           algos, [&](const auto& algo)
                           { return algo == cred.key->algo_name(); }) &&
                       (hostname.empty() ||
                        cred.certs.front().matches_dns_name(hostname));
            });

        return (cred != m_creds.end()) ? cred->certs
                                       : std::vector<Botan::X509_Certificate>{};
    }

    std::shared_ptr<Botan::Private_Key> private_key_for(
        const Botan::X509_Certificate& cert, const std::string& /*type*/,
        const std::string& /*context*/) override
    {
        const auto cred =
            std::ranges::find_if(m_creds, [&](const auto& cred)
                                 { return cred.certs.front() == cert; });
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

class TlsHttpCallbacks final : public Botan::TLS::StreamCallbacks
{
   public:
    TlsHttpCallbacks(std::shared_ptr<OCSP_Cache> ocsp_cache)
        : m_ocsp_cache(std::move(ocsp_cache))
    {
    }

    Botan::KEM_Encapsulation tls_kem_encapsulate(
        Botan::TLS::Group_Params group,
        const std::vector<uint8_t>& encoded_public_key,
        Botan::RandomNumberGenerator& rng,
        const Botan::TLS::Policy& policy) override
    {
        m_group = group;
        return Botan::TLS::StreamCallbacks::tls_kem_encapsulate(
            group, encoded_public_key, rng, policy);
    }

    std::vector<uint8_t> tls_provide_cert_status(
        const std::vector<Botan::X509_Certificate>& chain,
        const Botan::TLS::Certificate_Status_Request& csr) override
    {
        if (chain.size() < 2)
        {
            return {};
        }

        return m_ocsp_cache->getOCSPResponse(chain[1], chain[0]);
    }

    std::string collect_connection_details_as_json() const
    {
        std::stringstream ss;
        ss << "{" << "\"kex_algo\": \""
           << m_group.to_string().value_or("unknown") << "\","
           << "\"is_quantum_safe\": "
           << (m_group.is_post_quantum() ? "true" : "false") << ","
           << "\"wire_code\": \"0x" << std::hex << std::setw(4)
           << std::setfill('0') << m_group.wire_code() << "\"" << "}";
        return ss.str();
    }

   private:
    Botan::TLS::Group_Params m_group = Botan::TLS::Group_Params::NONE;
    std::shared_ptr<OCSP_Cache> m_ocsp_cache;
};

// --- Helper para std::visit (C++20) ---
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

class QuantumSafeHttpServer
{
public:

    // Constructor actualizado para recibir la configuración completa
    QuantumSafeHttpServer(const std::string& address, uint16_t port,
                          const std::string& cert, const std::string& key,
                          const std::string& doc_root, const std::string& policy_file,
                          uint64_t ocsp_timeout, uint64_t ocsp_cache_min);

    void run();

private:
    // --- Internal Methods (Declarations) ---
    
    std::shared_ptr<Botan::TLS::Policy> load_tls_policy(const std::string& policy_type);
    
    std::function<void(std::exception_ptr)> make_final_completion_handler(const std::string& context);

    beast::string_view mime_type(beast::string_view path);

    std::string path_cat(beast::string_view base, beast::string_view path);

    template <class ResponseBody, class Body, class Allocator>
    http::message_generator prepare_response(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status_code, ResponseBody body,
        std::string_view content_type = "text/html");

    template <class Body, class Allocator>
    http::message_generator bad_request(const http::request<Body, http::basic_fields<Allocator>>& req,
                     std::string why);

    template <class Body, class Allocator>
    http::message_generator not_found(const http::request<Body, http::basic_fields<Allocator>>& req);

    template <class Body, class Allocator>
    http::message_generator server_error(const http::request<Body, http::basic_fields<Allocator>>& req,
                      const std::string& what);

    template <class ResponseBody, class Body, class Allocator>
    http::message_generator success(const http::request<Body, http::basic_fields<Allocator>>& req,
                 ResponseBody&& body, std::string_view content_type = "text/html");

    template <class Body, class Allocator>
    http::message_generator handle_request(
        http::request<Body, http::basic_fields<Allocator>>&& req,
        beast::string_view doc_root);

    // Added: API Request dispatcher
    template <class Body, class Allocator>
    http::message_generator handle_api_request(
        http::request<Body, http::basic_fields<Allocator>>&& req,
        std::shared_ptr<TlsHttpCallbacks> tls_callbacks);

    net::awaitable<void> do_session(tcp_stream stream,
                            std::shared_ptr<Botan::TLS::Context> tls_ctx,
                            std::shared_ptr<OCSP_Cache> ocsp_cache,
                            std::string_view document_root);

    net::awaitable<void> do_listen(tcp::endpoint endpoint,
                            std::shared_ptr<Botan::TLS::Context> tls_ctx,
                            std::shared_ptr<OCSP_Cache> ocsp_cache,
                            std::string_view document_root);

    // --- Miembros de Clase ---
    std::string m_address;
    uint16_t m_port;

    // --- Componentes Botan (Ciclo de Vida) ---
    std::shared_ptr<Botan::AutoSeeded_RNG> m_rng;
    std::shared_ptr<Botan::TLS::Session_Manager_In_Memory> m_session_mgr;
    std::shared_ptr<Botan::TLS::Policy> m_tls_policy;
    std::shared_ptr<Basic_Credentials_Manager> m_creds;

    // --- Handlers & Callbacks ---
    std::shared_ptr<TlsHttpCallbacks> m_tls_callbacks;
    std::shared_ptr<OCSP_Cache> m_ocsp_cache;
};