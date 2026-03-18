#pragma once

#include <string>
#include <functional>
#include <boost/json.hpp>

class IDatabaseConnection {
public:
    virtual ~IDatabaseConnection() = default;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    virtual boost::json::object get_sanity_info() = 0;
    virtual void register_listen_async(const std::string& channel, std::function<void(boost::json::object)> handler) = 0;
    virtual void run_listener_loop() = 0;
    virtual void join() = 0;
};
