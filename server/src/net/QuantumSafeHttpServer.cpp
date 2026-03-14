#include <net/QuantumSafeHttpServer.hpp>

TlsHttpCallbacks::TlsHttpCallbacks(std::shared_ptr<OCSP_Cache> ocsp_cache)
    : m_ocsp_cache(std::move(ocsp_cache))
{
}

Botan::KEM_Encapsulation TlsHttpCallbacks::tls_kem_encapsulate(
    Botan::TLS::Group_Params group,
    const std::vector<uint8_t>& encoded_public_key,
    Botan::RandomNumberGenerator& rng,
    const Botan::TLS::Policy& policy)
{
    m_group = group;
    return Botan::TLS::StreamCallbacks::tls_kem_encapsulate(
        group, encoded_public_key, rng, policy);
}

std::vector<uint8_t> TlsHttpCallbacks::tls_provide_cert_status(
    const std::vector<Botan::X509_Certificate>& chain,
    const Botan::TLS::Certificate_Status_Request& /*csr*/)
{
    if (chain.size() < 2)
    {
        return {};
    }

    return m_ocsp_cache->getOCSPResponse(chain[1], chain[0]);
}

std::string TlsHttpCallbacks::collect_connection_details_as_json() const
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


std::shared_ptr<Botan::TLS::Policy> QuantumSafeHttpServer::load_tls_policy(
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

std::function<void(std::exception_ptr)> QuantumSafeHttpServer::make_final_completion_handler(const std::string& context)
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
                const std::time_t t_c =
                std::chrono::system_clock::to_time_t(now);
                std::cerr << std::ctime(&t_c) << " " << context << ": "
                          << ex.what() << std::endl;
            }
        }
    };
}

beast::string_view QuantumSafeHttpServer::mime_type(beast::string_view path)
{
    using beast::iequals;
    const auto ext = [&path]
    {
        auto const pos = path.rfind(".");
        if (pos == beast::string_view::npos)
            return beast::string_view{};
        return path.substr(pos);
    }();

    if (iequals(ext, ".htm"))
        return "text/html";
    if (iequals(ext, ".html"))
        return "text/html";
    if (iequals(ext, ".css"))
        return "text/css";
    if (iequals(ext, ".txt"))
        return "text/plain";
    if (iequals(ext, ".js"))
        return "application/javascript";
    if (iequals(ext, ".json"))
        return "application/json";
    if (iequals(ext, ".png"))
        return "image/png";
    if (iequals(ext, ".ico"))
        return "image/png";
    if (iequals(ext, ".jpe"))
        return "image/jpeg";
    if (iequals(ext, ".jpeg"))
        return "image/jpeg";
    if (iequals(ext, ".jpg"))
        return "image/jpeg";
    if (iequals(ext, ".gif"))
        return "image/gif";
    if (iequals(ext, ".ico"))
        return "image/vnd.microsoft.icon";
    if (iequals(ext, ".svg"))
        return "image/svg+xml";
    if (iequals(ext, ".svgz"))
        return "image/svg+xml";

    return "application/text";
}

std::string QuantumSafeHttpServer::path_cat(beast::string_view base, beast::string_view path)
{
    if (base.empty())
    {
        return std::string(path);
    }

    std::string result(base);

    char constexpr path_separator = '/';
    if (result.back() == path_separator)
    {
        result.resize(result.size() - 1);
    }
    result.append(path.data(), path.size());

    return result;
}

template <class ResponseBody, class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::prepare_response(
    const http::request<Body, http::basic_fields<Allocator>>& req,
    http::status status_code, ResponseBody body,
    std::string_view content_type = "text/html")
{
    using resp_body_t = std::decay_t<ResponseBody>;

    auto res = [&]
    {
        if constexpr (std::convertible_to<resp_body_t, std::string>)
        {
            http::response<http::string_body> r{status_code, req.version()};
            r.body() = std::string(body);
            return r;
        }
        else if constexpr (std::integral<resp_body_t>)
        {
            http::response<http::empty_body> r{status_code, req.version()};
            r.content_length(body);
            return r;
        }
        else if constexpr (std::same_as<http::file_body::value_type,
                                        resp_body_t>)
        {
            return http::response<http::file_body>{
                std::piecewise_construct, std::make_tuple(std::move(body)),
                std::make_tuple(status_code, req.version())};
        }
        else
        {
            static_assert(std::integral<resp_body_t>,
                          "Cannot handle the given response body");
        }
    }();

    res.set(http::field::server,
            std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());

    if constexpr (!std::integral<resp_body_t>)
    {
        res.prepare_payload();
    }

    return res;
}

// Returns a bad request response
template <class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::bad_request(const http::request<Body, http::basic_fields<Allocator>>& req,
                 std::string why)
{
    return prepare_response(req, http::status::bad_request, std::move(why));
};

// Returns a not found response
template <class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::not_found(const http::request<Body, http::basic_fields<Allocator>>& req)
{
    return prepare_response(req, http::status::not_found, req.target());
};

// Returns a server error response
template <class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::server_error(const http::request<Body, http::basic_fields<Allocator>>& req,
                  const std::string& what)
{
    return prepare_response(req, http::status::internal_server_error,
                            std::string("An error occured: ") + what);
};

// Returns a success response
template <class ResponseBody, class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::success(const http::request<Body, http::basic_fields<Allocator>>& req,
             ResponseBody&& body, std::string_view content_type = "text/html")
{
    return prepare_response(req, http::status::ok,
                            std::forward<ResponseBody>(body), content_type);
}

template <class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::handle_request(
    http::request<Body, http::basic_fields<Allocator>>&& req,
    beast::string_view doc_root)
{
    // Make sure we can handle the method
    if (req.method() != http::verb::get && req.method() != http::verb::head)
    {
        return bad_request(req, "Unknown HTTP-method");
    }

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
    {
        return bad_request(req, "Illegal request-target");
    }

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/')
    {
        path.append("index.html");
    }

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == beast::errc::no_such_file_or_directory)
    {
        return not_found(req);
    }

    // Handle an unknown error
    if (ec)
    {
        return server_error(req, ec.message());
    }

    if (req.method() == http::verb::head)
    {
        // Respond to HEAD request
        return success(req, body.size(), mime_type(path));
    }
    else
    {
        // Respond to GET request
        return success(req, std::move(body), mime_type(path));
    }
}

template <class Body, class Allocator>
http::message_generator QuantumSafeHttpServer::handle_api_request(
    http::request<Body, http::basic_fields<Allocator>>&& req,
    std::shared_ptr<TlsHttpCallbacks> tls_callbacks)
{
    // Make sure we can handle the method
    if (req.method() != http::verb::get)
    {
        return bad_request(req, "Unknown API method");
    }

    if (req.target() == "/api/connect_postgre")
    {
        ;
    }

    if (req.target() == "/api/connection_details")
    {
        return success(req, tls_callbacks->collect_connection_details_as_json(),
                       "application/json");
    }

    return not_found(req);
}

QuantumSafeHttpServer::QuantumSafeHttpServer(const std::string& address, 
                                             uint16_t port,
                                             const std::string& server_crt,
                                             const std::string& server_key)
    : m_tls_callbacks(std::make_shared<TlsHttpCallbacks>(std::make_shared<OCSP_Cache>()))
{
        m_creds =
            std::make_shared<Basic_Credentials_Manager>(certificate, key);
        m_rng = std::make_shared<Botan::AutoSeeded_RNG>();
        m_session_mgr =
            std::make_shared<Botan::TLS::Session_Manager_In_Memory>(rng);
        m_tls_policy = load_tls_policy(policy);

        const auto num_threads = std::thread::hardware_concurrency();
        net::io_context io{static_cast<int>(num_threads + 1)};

        m_address = net::ip::make_address("0.0.0.0");
}

void QuantumSafeHttpServer::run()
{
    // Main execution loop for the Post-Quantum Secure Server
}