#include <dispatcher/Dispatcher.hpp>
#include <iostream>
#include <condition_variable>
#include <chrono>

struct Dispatcher::RequestContext {
    std::condition_variable cv;
    std::chrono::steady_clock::time_point start_time;
    ResponseStatus status = ResponseStatus::PENDING;
    bool ready = false;
};

Dispatcher::Dispatcher() :
    id_counter(1)
{}

Dispatcher::~Dispatcher() {}

uint64_t Dispatcher::generate_id()
{
    return id_counter.fetch_add(1, std::memory_order_relaxed);
}

void Dispatcher::set_next_id(uint64_t next_id)
{
    if (next_id < 1) {
        next_id = 1;
    }
    id_counter.store(next_id, std::memory_order_relaxed);
}

ResponseStatus Dispatcher::wait_for_response(uint64_t id, int timeout_ms)
{
    // Nota de seguridad: Es seguro usar la pila del hilo (stack) para 'ctx' porque su 
    // tiempo de vida está ligado al hilo que espera. Dado que el hilo que despierta 
    // (notify) siempre accede a esta referencia mientras el hilo de espera está 
    // bloqueado en el 'cv.wait_for', y la entrada se elimina del mapa antes de salir 
    // de este ámbito, no hay riesgo de acceso a memoria liberada.
    RequestContext ctx;
    std::unique_lock<std::mutex> lock(map_mtx);

    if (pending_requests.size() >= MAX_PENDING)
    {
        std::cout << "System Full" << std::endl;
        return ResponseStatus::SYSTEM_FULL;
    }

    pending_requests[id] = &ctx;
    bool triggered = ctx.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return ctx.ready; });

    ResponseStatus final_status = triggered ? ctx.status : ResponseStatus::TIMEOUT;
    pending_requests.erase(id);

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