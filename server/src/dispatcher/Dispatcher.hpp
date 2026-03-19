#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

enum class ResponseStatus { PENDING, SUCCESS, TIMEOUT, DB_ERROR, SYSTEM_FULL };

class Dispatcher {

public:
    Dispatcher();
    virtual ~Dispatcher();

    uint64_t generate_id();
    void set_next_id(uint64_t next_id);

    ResponseStatus wait_for_response(uint64_t id, int timeout_ms);

    void dispatch(uint64_t id, ResponseStatus status);

private:
    struct RequestContext;

    std::mutex map_mtx;
    std::unordered_map<uint64_t, RequestContext*> pending_requests;
    std::atomic<uint64_t> id_counter{1};
    const size_t MAX_PENDING = 500;
};

