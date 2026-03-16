#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

#include <pqxx/pqxx>
#include <boost/json.hpp> // For JSON handling in get_sanity_info()

class DatabaseManager {
public:

    DatabaseManager(const std::string& connection_str);

    virtual ~DatabaseManager();

    // Establish connection
    void connect();
    void disconnect();

    // Example: Execute a simple query
    void execute(const std::string& sql);

    // Example: Fetch data (returns a result set)
    pqxx::result query(const std::string& sql);

    boost::json::object get_sanity_info();

    void parser_notify(const pqxx::notification& n, boost::json::object& msg);

    void listen_async(const std::string& channel, std::function<void(boost::json::object)> callback);

    void join();

private:
    std::string conn_str_;
    std::jthread listener_thread_;
    std::function<void(boost::json::object)> notification_callback_;

    // Protege el ciclo de vida de connection_.
    // Dado que cualquier clase externa puede invocar disconnect(), 
    // todas las operaciones internas (especialmente las asíncronas) 
    // DEBEN adquirir este mutex antes de desreferenciar el puntero.
    std::unique_ptr<pqxx::connection> connection_;
    mutable std::mutex conn_mutex_; 
};