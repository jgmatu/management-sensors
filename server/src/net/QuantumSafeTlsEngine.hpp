#pragma once

#include <memory>
#include <string>
#include <iostream>
#include <thread>
#include <mutex>

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

#include <log/Log.hpp>
#include <net/IConnDetailsProvider.hpp>
#include <net/ISessionHandler.hpp>

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

/**
 * @brief Motor TLS preparado para algoritmos post-cuánticos (PQC).
 *
 * Protocol-agnostic: delegates session handling to an ISessionHandler
 * implementation (HTTP, CLI, or any future protocol layer).
 */
class QuantumSafeTlsEngine : public IConnDetailsProvider {
public:
    /**
     * @brief Constructor for the QuantumSafeTlsEngine.
     * 
     * @param port The port number to listen on.
     * @param cert_path The path to the server's certificate chain.
     * @param key_path The path to the server's certificate private key file.
     * @param policy_path The path to the TLS policy file.
     * @param ocsp_cache_time The time to cache OCSP responses in minutes.
     * @param ocsp_timeout The timeout for OCSP requests in seconds.
     */
    explicit QuantumSafeTlsEngine(uint16_t port,
                                  const std::string& cert_path,
                                  const std::string& key_path,
                                  const std::string& policy_path,
                                  uint64_t ocsp_cache_time,
                                  uint64_t ocsp_timeout);

    virtual ~QuantumSafeTlsEngine() = default;

    void initialize();
    void stop();
    void join();

    /**
     * @brief Inject the protocol-specific session handler.
     *
     * The handler is shared across all concurrent sessions.  Its
     * handle_session() method is invoked once per TLS connection
     * after the handshake completes.
     */
    void set_session_handler(std::shared_ptr<ISessionHandler> handler);

    std::string get_latest_connection_details() const override;

private:
    std::shared_ptr<Botan::TLS::Policy> load_tls_policy(
        const std::string& policy_type);

    std::function<void(std::exception_ptr)> 
        make_final_completion_handler(const std::string& context);

    boost::asio::awaitable<void> do_session(
        AwaitableTcpStream stream,
        std::shared_ptr<Botan::TLS::Context> ctx,
        std::shared_ptr<OCSP_Cache> ocsp_cache);

    boost::asio::awaitable<void> do_listen(
            boost::asio::ip::tcp::endpoint endpoint,
            std::shared_ptr<Botan::TLS::Context> tls_ctx,
            std::shared_ptr<OCSP_Cache> ocsp_cache);

    std::shared_ptr<ISessionHandler> session_handler_;

    // Botan / PQC core configuration (captured from constructor)
    uint16_t port_{0};
    std::string cert_path_;
    std::string key_path_;
    std::string policy_path_;
    uint64_t ocsp_cache_time_{0};
    uint64_t ocsp_timeout_{0};

    // Botan Core runtime objects (materialized in initialize())
    std::shared_ptr<Botan::AutoSeeded_RNG> rng_;
    std::shared_ptr<Basic_Credentials_Manager> creds_;
    std::shared_ptr<Botan::TLS::Session_Manager> session_mgr_;
    std::shared_ptr<Botan::TLS::Policy> tls_policy_;
    std::shared_ptr<Botan::TLS::Context> tls_context_;
    std::shared_ptr<OCSP_Cache> ocsp_cache_;

    // Asio Core
    boost::asio::ip::tcp::endpoint endpoint_;
    std::unique_ptr<boost::asio::io_context> io_context_; 
    std::vector<std::jthread> thread_pool_;

    mutable std::mutex connection_details_mutex_;
    std::string latest_connection_details_ = "{}";
};
