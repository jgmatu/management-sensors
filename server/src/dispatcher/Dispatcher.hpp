#pragma once

#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <string>

enum class ResponseStatus { PENDING, SUCCESS, TIMEOUT, DB_ERROR, SYSTEM_FULL };

class Dispatcher {

public:
    Dispatcher();
    virtual ~Dispatcher();

    ResponseStatus wait_for_response(uint64_t id, int timeout_ms);

    void dispatch(uint64_t id, ResponseStatus status);

private:
    struct RequestContext;

    static std::string status_to_string(ResponseStatus status);

    std::mutex map_mtx;
    std::unordered_map<uint64_t, RequestContext*> pending_requests;
    const size_t MAX_PENDING = 500;
};

