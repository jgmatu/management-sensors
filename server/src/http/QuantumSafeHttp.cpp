#include <http/QuantumSafeHttp.hpp>
#include <botan/version.h>
#include <db/DatabaseManager.hpp>
#include <dispatcher/Dispatcher.hpp>
#include <jwt/JwtManager.hpp>
#include <log/Log.hpp>

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/json.hpp>

namespace http = boost::beast::http;

QuantumSafeHttp::QuantumSafeHttp(
    std::shared_ptr<IConnDetailsProvider> connection_details_provider,
    std::string document_root,
    Dispatcher* dispatcher,
    std::shared_ptr<DatabaseManager> db,
    std::shared_ptr<JwtManager> jwt)
    : connection_details_provider_(std::move(connection_details_provider))
    , document_root_(std::move(document_root))
    , dispatcher_(dispatcher)
    , db_(std::move(db))
    , jwt_(std::move(jwt))
{
}

void QuantumSafeHttp::set_connection_details_provider(
    std::shared_ptr<IConnDetailsProvider> provider)
{
    connection_details_provider_ = std::move(provider);
}

void QuantumSafeHttp::set_document_root(std::string document_root)
{
    document_root_ = std::move(document_root);
}

boost::asio::awaitable<void> QuantumSafeHttp::handle_session(TlsStream& stream)
{
    boost::beast::flat_buffer buffer;
    for (;;)
    {
        Request req;
        co_await http::async_read(stream, buffer, req);

        auto response = req.target().starts_with("/api") ?
            handle_api_request(std::move(req)) :
            handle_request(std::move(req), document_root_);

        const auto keep_alive = response.keep_alive();
        co_await boost::beast::async_write(
            stream, std::move(response), boost::asio::use_awaitable);
        if (!keep_alive) break;
    }
}

http::message_generator QuantumSafeHttp::handle_request(
    Request&& req, boost::beast::string_view doc_root)
{
    logging::Logger::instance().info("http", 
        "Handling request: " + std::string(req.method_string().data(), req.method_string().size()));
    logging::Logger::instance().info("http", 
        "Method: " + std::string(req.method_string().data(), req.method_string().size  ()) + " " + std::to_string(static_cast<unsigned>(req.method())) + " " + std::to_string(static_cast<unsigned>(req.version())));
    logging::Logger::instance().info("http", 
        "Document root: " + std::string(doc_root.data(), doc_root.size()));
    logging::Logger::instance().info("http", 
        "Path: " + std::string(req.target().data(), req.target().size()));
    logging::Logger::instance().info("http", 
        "Keep alive: " + std::string(req.keep_alive() ? "true" : "false"));

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
    const auto target = req.target();

    // Public endpoint: login does not require a token
    if (target == "/api/auth/login" && req.method() == http::verb::post)
        return handle_login(req);

    // All other /api/* endpoints require a valid JWT
    std::string auth_error;
    if (!require_auth(req, auth_error))
        return unauthorized(req, std::move(auth_error));

    if (target == "/api/connection_details" && req.method() == http::verb::get)
    {
        logging::Logger::instance().info("http", "Connection details request received");
        if (!connection_details_provider_)
            return server_error(req, "Connection details provider is not configured");
        return success_text(
            req,
            connection_details_provider_->get_latest_connection_details(),
            "application/json");
    }

    if (target == "/api/config_ip" && req.method() == http::verb::post)
    {
        return config_ip(req);
    }

    return not_found(req);
}

// ── JWT Authentication ──────────────────────────────────────────────────────

bool QuantumSafeHttp::require_auth(const Request& req,
                                   std::string& out_error) const
{
    if (!jwt_)
    {
        out_error = "JWT authentication not configured";
        return false;
    }

    auto it = req.find(http::field::authorization);
    if (it == req.end())
    {
        out_error = "Missing Authorization header";
        return false;
    }

    const auto auth_value = it->value();
    constexpr std::string_view bearer_prefix = "Bearer ";
    if (auth_value.size() <= bearer_prefix.size() ||
        auth_value.substr(0, bearer_prefix.size()) != bearer_prefix)
    {
        out_error = "Authorization header must use Bearer scheme";
        return false;
    }

    const std::string token(
        auth_value.data() + bearer_prefix.size(),
        auth_value.size() - bearer_prefix.size());

    auto claims = jwt_->verify(token);
    if (!claims)
    {
        out_error = "Invalid or expired token";
        return false;
    }

    logging::Logger::instance().info("http",
        "[AUTH] Authenticated sub=" + claims->sub +
        " role=" + claims->role);
    return true;
}

http::message_generator QuantumSafeHttp::handle_login(const Request& req)
{
    if (!jwt_)
        return server_error(req, "JWT authentication not configured");

    std::string username;
    std::string password;

    try
    {
        auto body = boost::json::parse(req.body());
        const auto& obj = body.as_object();

        if (!obj.contains("username") || !obj.contains("password"))
            return bad_request(req,
                R"({"status":"error","message":"Missing username or password"})");

        username = boost::json::value_to<std::string>(obj.at("username"));
        password = boost::json::value_to<std::string>(obj.at("password"));
    }
    catch (const std::exception& e)
    {
        return bad_request(req,
            R"({"status":"error","message":"Invalid JSON body"})");
    }

    // TODO: validate credentials against the database
    // For now accept admin/admin as bootstrap credential
    if (username != "admin" || password != "admin")
    {
        logging::Logger::instance().warn("http",
            "[AUTH] Failed login attempt for user=" + username);

        boost::json::object err;
        err["status"] = "error";
        err["message"] = "Invalid credentials";
        return unauthorized(req, boost::json::serialize(err));
    }

    auto token = jwt_->generate(username, "admin", 3600);

    logging::Logger::instance().info("http",
        "[AUTH] Token issued for user=" + username);

    boost::json::object resp;
    resp["status"] = "success";
    resp["token"] = token;
    resp["expires_in"] = 3600;
    return success_text(req, boost::json::serialize(resp), "application/json");
}

http::message_generator QuantumSafeHttp::config_ip(const Request& req)
{
    if (!dispatcher_ || !db_)
        return server_error(req, "Dispatcher or database not configured");

    int sensor_id = 0;
    std::string ip;

    try
    {
        const auto& raw_body = req.body();
        logging::Logger::instance().debug("http",
            "[API] config_ip raw body (" + std::to_string(raw_body.size()) +
            " bytes): " + raw_body);

        auto body = boost::json::parse(raw_body);
        auto const& obj = body.as_object();

        if (!obj.contains("sensor_id"))
            return bad_request(req,
                R"({"status":"error","message":"Missing required field: sensor_id"})");
        if (!obj.contains("ip"))
            return bad_request(req,
                R"({"status":"error","message":"Missing required field: ip"})");

        sensor_id = static_cast<int>(obj.at("sensor_id").as_int64());
        ip = obj.at("ip").as_string().c_str();
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("http",
            "[API] JSON parsing error: " + std::string(e.what()));  
        return bad_request(req, "Invalid JSON body: " + std::string(e.what()));
    }

    uint64_t request_id = 0;
    try
    {
        request_id = db_->generate_request_id();
        logging::Logger::instance().info("http",
            "[API] Generating request_id | sensor_id=" + std::to_string(sensor_id) +
            " | ip=" + ip + " | request_id=" + std::to_string(request_id));

        db_->add_pending_config(
            sensor_id,
            "sensor-" + std::to_string(sensor_id),
            ip, true, request_id);
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("http",
            "[API] DB error: " + std::string(e.what()));

        boost::json::object err_body;
        err_body["status"]     = "error";
        err_body["message"]    = std::string("Database error: ") + e.what();
        err_body["request_id"] = static_cast<int64_t>(request_id);
        return server_error(req, boost::json::serialize(err_body));
    }

    logging::Logger::instance().info("http",
        "[API] Waiting for pipeline response | request_id=" +
        std::to_string(request_id) +
        " | timeout_ms=" + std::to_string(REQUEST_TIMEOUT_MS));

    auto status = dispatcher_->wait_for_response(request_id, REQUEST_TIMEOUT_MS);

    logging::Logger::instance().info("http",
        "[API] Pipeline finished | request_id=" + std::to_string(request_id) +
        " | status=" + status_to_string(status));

    if (status == ResponseStatus::SUCCESS)
    {
        boost::json::object ok_body;
        ok_body["status"]     = "success";
        ok_body["sensor_id"]  = sensor_id;
        ok_body["request_id"] = static_cast<int64_t>(request_id);
        ok_body["message"]    = "Sensor " + std::to_string(sensor_id) + " updated successfully";
        return success_text(req, boost::json::serialize(ok_body), "application/json");
    }

    boost::json::object fail_body;
    fail_body["status"]     = "error";
    fail_body["sensor_id"]  = sensor_id;
    fail_body["request_id"] = static_cast<int64_t>(request_id);
    fail_body["message"]    = status_to_string(status);

    http::response<http::string_body> res{
        http::status::service_unavailable, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "application/json");
    res.keep_alive(req.keep_alive());
    res.body() = boost::json::serialize(fail_body);
    res.prepare_payload();
    return res;
}

std::string QuantumSafeHttp::status_to_string(ResponseStatus status)
{
    switch (status) {
        case ResponseStatus::SUCCESS:     return "SUCCESS";
        case ResponseStatus::TIMEOUT:     return "Request Timed Out";
        case ResponseStatus::DB_ERROR:    return "Database Failure";
        case ResponseStatus::SYSTEM_FULL: return "Maximum Pending Requests Reached";
        case ResponseStatus::PENDING:     return "PENDING";
        default:                          return "UNKNOWN_ERROR";
    }
}

boost::beast::string_view QuantumSafeHttp::mime_type(boost::beast::string_view path)
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

std::string QuantumSafeHttp::path_cat(
    boost::beast::string_view base, boost::beast::string_view path)
{
    if (base.empty()) return std::string(path);
    std::string result(base);
    if (result.back() == '/') result.pop_back();
    result.append(path.data(), path.size());
    return result;
}

http::message_generator QuantumSafeHttp::unauthorized(
    const QuantumSafeHttp::Request& req, std::string why)
{
    http::response<http::string_body> res{http::status::unauthorized, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "application/json");
    res.set(http::field::www_authenticate, "Bearer");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(why);
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::bad_request(
    const QuantumSafeHttp::Request& req, std::string why)
{
    http::response<http::string_body> res{http::status::bad_request, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::move(why);
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::not_found(
    const QuantumSafeHttp::Request& req)
{
    http::response<http::string_body> res{http::status::not_found, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::string(req.target());
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::server_error(
    const QuantumSafeHttp::Request& req, const std::string& what)
{
    http::response<http::string_body> res{http::status::internal_server_error, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, "text/plain");
    res.keep_alive(req.keep_alive());
    res.body() = std::string("An error occurred: ") + what;
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::success_text(
    const QuantumSafeHttp::Request& req,
    std::string body,
    std::string_view content_type)
{
    http::response<http::string_body> res{http::status::ok, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.body() = std::move(body);
    res.prepare_payload();
    return res;
}

http::message_generator QuantumSafeHttp::success_head(
    const QuantumSafeHttp::Request& req,
    std::uint64_t content_length,
    std::string_view content_type)
{
    http::response<http::empty_body> res{http::status::ok, req.version()};
    res.set(http::field::server, std::string("Botan ") + Botan::short_version_string());
    res.set(http::field::content_type, content_type);
    res.keep_alive(req.keep_alive());
    res.content_length(content_length);
    return res;
}

http::message_generator QuantumSafeHttp::success_file(
    const QuantumSafeHttp::Request& req,
    http::file_body::value_type file,
    std::string_view content_type)
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