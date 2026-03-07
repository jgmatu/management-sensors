#pragma once

#include <iostream>
#include <memory>
#include <string>
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

    // Example: Fetch data (returns a result set)
    pqxx::result query(const std::string& sql);

    boost::json::object get_sanity_info();

private:
    std::string conn_str_;
    std::unique_ptr<pqxx::connection> connection_;
};