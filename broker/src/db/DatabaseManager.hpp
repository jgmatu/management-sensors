#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <thread>

#include <pqxx/pqxx>
#include <boost/json.hpp> // For JSON handling in get_sanity_info()

class DatabaseManager {
public:

    DatabaseManager(const std::string& connection_str);

    virtual ~DatabaseManager() = default;

    // Establish connection
    void connect();

    // Example: Execute a simple query
    void execute(const std::string& sql);

    template <typename... Args> bool execute_dml(std::string_view query, Args&&... args);

    // Example: Fetch data (returns a result set)
    pqxx::result query(const std::string& sql);

    boost::json::object get_sanity_info();

    void parser_notify(const pqxx::notification& n, boost::json::object& msg);

    void listen_async(const std::string& channel, std::function<void(boost::json::object)> callback);

private:
    std::string conn_str_;
    std::unique_ptr<pqxx::connection> connection_;
    std::thread listener_thread_;
    bool stop_listener_ = false;
    std::function<void(boost::json::object)> notification_callback_;
};