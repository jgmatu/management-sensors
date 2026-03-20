#include <http/QuantumSafeHttp.hpp>
#include <botan/version.h>

namespace http = boost::beast::http;

QuantumSafeHttp::QuantumSafeHttp(
    std::shared_ptr<IQuantumConnDetailsProvider> connection_details_provider)
    : connection_details_provider_(std::move(connection_details_provider))
{
}

void QuantumSafeHttp::set_connection_details_provider(
    std::shared_ptr<IQuantumConnDetailsProvider> provider)
{
    connection_details_provider_ = std::move(provider);
}

inline boost::beast::string_view mime_type(boost::beast::string_view path)
{
    using boost::beast::iequals;
    const auto ext = [&path] {
        const auto pos = path.rfind(".");
        if (pos == boost::beast::string_view::npos) return boost::beast::string_view{};
        return path.substr(pos);
    }();

    if (iequals(ext, ".htm") || iequals(ext, ".html")) return "text/html";
    if (iequals(ext, ".css")) return "text/css";
    if (iequals(ext, ".txt")) return "text/plain";
    if (iequals(ext, ".js")) return "application/javascript";
    if (iequals(ext, ".json")) return "application/json";
    if (iequals(ext, ".png")) return "image/png";
    if (iequals(ext, ".jpe") || iequals(ext, ".jpeg") || iequals(ext, ".jpg")) return "image/jpeg";
    if (iequals(ext, ".gif")) return "image/gif";
    if (iequals(ext, ".ico")) return "image/vnd.microsoft.icon";
    if (iequals(ext, ".svg") || iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

inline std::string path_cat(boost::beast::string_view base, boost::beast::string_view path)
{
    if (base.empty()) return std::string(path);
    std::string result(base);
    if (result.back() == '/') result.pop_back();
    result.append(path.data(), path.size());
    return result;
}

inline http::message_generator bad_request(const QuantumSafeHttp::Request& req, std::string why)
{
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(why);
    res.prepare_payload();
    return res;
}

inline http::message_generator not_found(const QuantumSafeHttp::Request& req)
{
    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(req.target());
    res.prepare_payload();
    return res;
}

inline http::message_generator server_error(const QuantumSafeHttp::Request& req, const std::string& what)
{
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::string("An error occurred: ") + what;
    res.prepare_payload();
    return res;
}

inline http::message_generator success_text(const QuantumSafeHttp::Request& req, std::string body, std::string_view content_type)
{
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

inline http::message_generator success_head(const QuantumSafeHttp::Request& req, std::uint64_t content_length, std::string_view content_type)
{
    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.content_length(content_length);
    return res;
}

inline http::message_generator success_file(const QuantumSafeHttp::Request& req, http::file_body::value_type file, std::string_view content_type)
{
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(file)),
        std::make_tuple(http::status::ok, req.version())};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::handle_request(
    Request&& req, boost::beast::string_view doc_root)
{
    if (req.method() != http::verb::get && req.method() != http::verb::head)
        return bad_request(req, "Unknown HTTP-method");

    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != boost::beast::string_view::npos)
        return bad_request(req, "Illegal request-target");

    std::string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/') path.append("index.html");

    boost::beast::error_code ec;
    http::file_body::value_type file;
    file.open(path.c_str(), boost::beast::file_mode::scan, ec);

    if (ec == boost::beast::errc::no_such_file_or_directory) return not_found(req);
    if (ec) return server_error(req, ec.message());

    if (req.method() == http::verb::head) return success_head(req, file.size(), mime_type(path));
    return success_file(req, std::move(file), mime_type(path));
}

http::message_generator QuantumSafeHttp::handle_api_request(Request&& req)
{
    if (req.method() != http::verb::get) return bad_request(req, "Unknown API method");
    if (req.target() != "/api/connection_details") return not_found(req);
    if (!connection_details_provider_)
        return server_error(req, "Connection details provider is not configured");

    return success_text(
        req,
        connection_details_provider_->get_latest_connection_details(),
        "application/json");
}