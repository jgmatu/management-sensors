#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <cstdint>

/**
 * @note TEST CONCEPT: 1:N Thread-Specific Response Dispatcher
 * 
 * This test demonstrates the "Response Demultiplexer" pattern, where a 
 * single Dispatcher thread routes incoming events to N specific waiting threads.
 *
 * KEY MECHANISM:
 * 1. Targeted Notification: Unlike a general "notify_all" which wakes every 
 *    thread in a pool, this design uses a unique ID to identify the exact 
 *    thread waiting for a specific response.
 * 
 * 2. Map-Based Routing: The `pending_requests` map acts as a lookup table. 
 *    The Dispatch thread receives an ID, finds the associated RequestContext, 
 *    and signals only that thread's private condition_variable.
 * 
 * 3. Thread Isolation: 
 *    - Even Threads: Simulated as successful operations. The main thread 
 *      identifies them via the map and triggers their specific wakeup.
 *    - Odd Threads: Simulated as timeouts. No dispatch is called for these 
 *      IDs, demonstrating that threads remain blocked and isolated unless 
 *      their specific ID is addressed.
 * 
 * 4. Concurrency: The std::mutex protects the map integrity, while the 
 *    individual condition_variables allow multiple threads to wait 
 *    simultaneously without interfering with one another.
 */

 enum class ResponseStatus { PENDING, SUCCESS, TIMEOUT, DB_ERROR, SYSTEM_FULL };

struct RequestContext {
    std::condition_variable cv;
    std::chrono::steady_clock::time_point start_time;
    ResponseStatus status = ResponseStatus::PENDING;
    bool ready = false;
    RequestContext() : start_time(std::chrono::steady_clock::now()) {}
};

class Dispatcher {
private:
    std::mutex map_mtx;
    std::unordered_map<uint64_t, RequestContext*> pending_requests;
    std::atomic<uint64_t> id_counter{1};
    const size_t MAX_PENDING = 10000;

public:
    uint64_t generate_id()
    {
        // fetch_add returns the value BEFORE the increment.
        // If id was UINT64_MAX, it becomes 0 automatically.
        return id_counter.fetch_add(1, std::memory_order_relaxed);
    }

    ResponseStatus wait_for_response(uint64_t id, int timeout_ms)
    {
        // Nota de seguridad: Es seguro usar la pila del hilo (stack) para 'ctx' porque su 
        // tiempo de vida está ligado al hilo que espera. Dado que el hilo que despierta 
        // (notify) siempre accede a esta referencia mientras el hilo de espera está 
        // bloqueado en el 'cv.wait_for', y la entrada se elimina del mapa antes de salir 
        // de este ámbito, no hay riesgo de acceso a memoria liberada.
        RequestContext ctx;
        std::unique_lock<std::mutex> lock(map_mtx);
        if (pending_requests.size() >= MAX_PENDING) return ResponseStatus::SYSTEM_FULL;

        pending_requests[id] = &ctx;
        bool triggered = ctx.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] { return ctx.ready; });

        ResponseStatus final_status = triggered ? ctx.status : ResponseStatus::TIMEOUT;
        pending_requests.erase(id); // Erase bajo el mismo lock del wait
        return final_status;
    }

    void dispatch(uint64_t id, ResponseStatus status)
    {
        std::lock_guard<std::mutex> lock(map_mtx);
        auto it = pending_requests.find(id);

        if (it != pending_requests.end())
        {
            it->second->status = status;
            it->second->ready = true;
            it->second->cv.notify_one();
        }
    }
};

std::mutex out_mtx;

// Function for N waiter threads
void waiter_thread(Dispatcher& dispatcher, int thread_idx, uint64_t id)
{
    for (;;)
    {
        {
            std::unique_lock<std::mutex> lock(out_mtx);
            std::cout << "[Waiter " << thread_idx << "] Waiting for ID: " << id << "...\n";
        }

        // Wait for up to 2 seconds
        ResponseStatus result = dispatcher.wait_for_response(id, 4000);

        {
            std::unique_lock<std::mutex> lock(out_mtx);

            if (result == ResponseStatus::SUCCESS)
            {
                std::cout << "[Waiter " << thread_idx << "] SUCCESS for ID: " << id << "\n";
                // Invalid test of dispatcher even id are not dispatcher it is time out behaivour.
                if (id % 2 == 0) throw;
            }
            else
            {
                std::cout << "[Waiter " << thread_idx << "] TIMEOUT/OTHER for ID: " << id  << " (Status: " << (int)result << ")\n";
            }
        }
    }
}

int main()
{
    Dispatcher dispatcher;
    const int N = 10;
    std::vector<std::thread> workers;
    std::vector<uint64_t> ids;

    // 1. Create N waiter threads
    for (int i = 0; i < N; ++i)
    {
        uint64_t id = dispatcher.generate_id();
        ids.push_back(id);
        workers.emplace_back(waiter_thread, std::ref(dispatcher), i, id);
    }

    // Small delay to ensure all threads are in wait_for_response
    std::this_thread::sleep_for(std::chrono::milliseconds(2 * 1000));

    // 2. Main thread dispatches ONLY even threads (1:N mode)
    for (;;)
    {
        for (int i = 0; i < N; ++i)
        {
            if (i % 2 == 0)
            {
                std::cout << "[Main] Dispatching SUCCESS to ID: " << ids[i] << "\n";
                dispatcher.dispatch(ids[i], ResponseStatus::SUCCESS);
            }
            // Odd IDs are never dispatched, so they will eventually timeout
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "[Main] Starting selective dispatch...\n";
    }

    // 3. Join all threads
    for (auto& t : workers) {
        t.join();
    }

    std::cout << "[Main] All threads finished.\n";
    return 0;
}