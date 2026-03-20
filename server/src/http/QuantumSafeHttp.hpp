#pragma once

#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <string>

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

    std::shared_ptr<IQuantumConnDetailsProvider> connection_details_provider_;
    std::string doc_root_{};
};
