#include <http/QuantumSafeHttp.hpp>
#include <botan/version.h>
#include <db/DatabaseManager.hpp>
#include <dispatcher/Dispatcher.hpp>
#include <log/Log.hpp>

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/json.hpp>

namespace http = boost::beast::http;

QuantumSafeHttp::QuantumSafeHttp(
    std::shared_ptr<IConnDetailsProvider> connection_details_provider,
    std::string document_root,
    Dispatcher* dispatcher,
    std::shared_ptr<DatabaseManager> db)
    : connection_details_provider_(std::move(connection_details_provider))
    , document_root_(std::move(document_root))
    , dispatcher_(dispatcher)
    , db_(std::move(db))
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

        auto response = req.target().starts_with("/api")
                            ? handle_api_request(std::move(req))
                            : handle_request(std::move(req), document_root_);

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
        return handle_config_ip(req);
    }

    return not_found(req);
}

http::message_generator QuantumSafeHttp::handle_config_ip(const Request& req)
{
    if (!dispatcher_ || !db_)
        return server_error(req, "Dispatcher or database not configured");

    int sensor_id = 0;
    std::string ip;

    try
    {
        auto body = boost::json::parse(req.body());
        auto const& obj = body.as_object();
        sensor_id = static_cast<int>(obj.at("sensor_id").as_int64());
        ip = boost::json::value_to<std::string>(obj.at("ip"));
    }
    catch (const std::exception& e)
    {
        return bad_request(req,
            std::string(R"({"status":"error","message":"Invalid JSON body: )") +
            e.what() + "\"}");
    }

    uint64_t request_id = dispatcher_->generate_id();

    logging::Logger::instance().info("http",
        "[API] CONFIG_IP request | sensor_id=" + std::to_string(sensor_id) +
        " | ip=" + ip + " | request_id=" + std::to_string(request_id));

    try
    {
        db_->add_pending_config(
            sensor_id,
            "sensor-" + std::to_string(sensor_id),
            ip, true, request_id);
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error("http",
            "[API] DB error: " + std::string(e.what()));

        boost::json::object err_body = {
            {"status",     "error"},
            {"message",    std::string("Database error: ") + e.what()},
            {"request_id", request_id}
        };
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
        boost::json::object ok_body = {
            {"status",     "success"},
            {"sensor_id",  sensor_id},
            {"request_id", request_id},
            {"message",    "Sensor " + std::to_string(sensor_id) + " updated successfully"}
        };
        return success_text(req, boost::json::serialize(ok_body), "application/json");
    }

    boost::json::object fail_body = {
        {"status",     "error"},
        {"sensor_id",  sensor_id},
        {"request_id", request_id},
        {"message",    status_to_string(status)}
    };

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