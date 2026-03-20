#pragma once

#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <net/IQuantumConnDetailsProvider.hpp>

class QuantumSafeHttp
{
public:
    explicit QuantumSafeHttp(
        std::shared_ptr<IQuantumConnDetailsProvider> connection_details_provider =
            nullptr);

    using Request = boost::beast::http::request<
            boost::beast::http::string_body,
            boost::beast::http::basic_fields<std::allocator<char>>>;

    boost::beast::http::message_generator handle_request(
        Request&& req,
        boost::beast::string_view doc_root);

    boost::beast::http::message_generator handle_api_request(Request&& req);

    void set_connection_details_provider(
        std::shared_ptr<IQuantumConnDetailsProvider> provider);

private:
    static boost::beast::string_view mime_type(boost::beast::string_view path);
    static std::string path_cat(
        boost::beast::string_view base,
        boost::beast::string_view path);

    static boost::beast::http::message_generator bad_request(
        const Request& req,
        std::string why);
    static boost::beast::http::message_generator not_found(const Request& req);
    static boost::beast::http::message_generator server_error(
        const Request& req,
        const std::string& what);
    static boost::beast::http::message_generator success_text(
        const Request& req,
        std::string body,
        std::string_view content_type);
    static boost::beast::http::message_generator success_head(
        const Request& req,
        std::uint64_t content_length,
        std::string_view content_type);
    static boost::beast::http::message_generator success_file(
        const Request& req,
        boost::beast::http::file_body::value_type file,
        std::string_view content_type);

    std::shared_ptr<IQuantumConnDetailsProvider> connection_details_provider_;
    std::string doc_root_{};
};
