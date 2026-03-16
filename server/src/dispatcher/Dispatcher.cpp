#include <dispatcher/Dispatcher.hpp>
#include <iostream>

struct Dispatcher::RequestContext {
    std::condition_variable cv;
    std::chrono::steady_clock::time_point start_time;
    ResponseStatus status = ResponseStatus::PENDING;
    bool ready = false;
    std::chrono::steady_clock::time_point ts;
};

Dispatcher::Dispatcher() :
    id_counter(1)
{}

Dispatcher::~Dispatcher() {}

uint64_t Dispatcher::generate_id()
{
    // fetch_add returns the value BEFORE the increment.
    // If id was UINT64_MAX, it becomes 0 automatically.
    return id_counter.fetch_add(1, std::memory_order_relaxed);
}

ResponseStatus Dispatcher::wait_for_response(uint64_t id, int timeout_ms)
{
    RequestContext ctx;
    std::unique_lock<std::mutex> lock(map_mtx);

    if (pending_requests.size() >= MAX_PENDING)
    {
        std::cout << "System Full" << std::endl;
        return ResponseStatus::SYSTEM_FULL;
    }

    std::cout << "Wait for pending request..." << std::endl;
    pending_requests[id] = &ctx;
    bool triggered = ctx.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return ctx.ready; });

    ResponseStatus final_status = triggered ? ctx.status : ResponseStatus::TIMEOUT;
    pending_requests.erase(id);

    std::cout << "Final request response..." << std::endl;

    return final_status;
}

void Dispatcher::dispatch(uint64_t id, ResponseStatus status)
{
    std::lock_guard<std::mutex> lock(map_mtx);

    std::unordered_map<uint64_t, RequestContext*>::iterator it = pending_requests.find(id);

    if (it != pending_requests.end())
    {
        it->second->status = status;
        it->second->ready = true;
        it->second->cv.notify_one();
    }
}