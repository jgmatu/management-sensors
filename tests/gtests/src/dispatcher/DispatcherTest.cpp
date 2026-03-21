#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include <dispatcher/Dispatcher.hpp>

// Helper: convert ResponseStatus to string for ASSERT messages
static const char* status_to_cstr(ResponseStatus s)
{
    switch (s)
    {
    case ResponseStatus::PENDING:     return "PENDING";
    case ResponseStatus::SUCCESS:     return "SUCCESS";
    case ResponseStatus::TIMEOUT:     return "TIMEOUT";
    case ResponseStatus::DB_ERROR:    return "DB_ERROR";
    case ResponseStatus::SYSTEM_FULL: return "SYSTEM_FULL";
    }
    return "UNKNOWN";
}

TEST(DispatcherTest, GenerateIdIsMonotonic)
{
    Dispatcher d;
    uint64_t id1 = d.generate_id();
    uint64_t id2 = d.generate_id();
    uint64_t id3 = d.generate_id();
    EXPECT_LT(id1, id2);
    EXPECT_LT(id2, id3);
    EXPECT_NE(id1, id2);
    EXPECT_NE(id2, id3);
}

TEST(DispatcherTest, SetNextIdReseedsCounter)
{
    Dispatcher d;
    d.set_next_id(42);

    uint64_t id1 = d.generate_id();
    uint64_t id2 = d.generate_id();

    EXPECT_EQ(id1, 42u);
    EXPECT_EQ(id2, 43u);
}

TEST(DispatcherTest, WaitForResponseTimesOutWhenNoDispatch)
{
    Dispatcher d;
    uint64_t id = d.generate_id();
    auto start = std::chrono::steady_clock::now();
    ResponseStatus s = d.wait_for_response(id, 50); // 50 ms timeout
    auto end = std::chrono::steady_clock::now();
    EXPECT_EQ(s, ResponseStatus::TIMEOUT) << status_to_cstr(s);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Debe ser al menos ~timeout, pero deja margen por scheduling
    EXPECT_GE(elapsed_ms, 40);
}

TEST(DispatcherTest, WaitForResponseIsWokenByDispatchSuccess)
{

    Dispatcher d;
    uint64_t id = d.generate_id();
    std::thread worker([&](){
        // espera un poco para simular trabajo asíncrono
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        d.dispatch(id, ResponseStatus::SUCCESS);
    });
    ResponseStatus s = d.wait_for_response(id, 200); // timeout suficientemente grande
    worker.join();
    EXPECT_EQ(s, ResponseStatus::SUCCESS) << status_to_cstr(s);
}

TEST(DispatcherTest, WaitForResponseIsWokenByDispatchDbError)
{
    Dispatcher d;
    uint64_t id = d.generate_id();
    std::thread worker([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        d.dispatch(id, ResponseStatus::DB_ERROR);
    });
    ResponseStatus s = d.wait_for_response(id, 200);
    worker.join();
    EXPECT_EQ(s, ResponseStatus::DB_ERROR) << status_to_cstr(s);
}

TEST(DispatcherTest, DispatchForUnknownIdDoesNothing)
{
    Dispatcher d;
    uint64_t id = d.generate_id();
    // No hay hilo esperando; esto no debe colgar ni lanzar
    d.dispatch(id, ResponseStatus::SUCCESS);
    // Asegura que una espera posterior sobre OTRO id sigue funcionando
    uint64_t another_id = d.generate_id();
    ResponseStatus s = d.wait_for_response(another_id, 10);
    EXPECT_EQ(s, ResponseStatus::TIMEOUT);
}

TEST(DispatcherTest, ReturnsSystemFullWhenTooManyPending)
{
    Dispatcher d;
    // MAX_PENDING en Dispatcher es 10000; usamos un margen amplio
    const int warmup = 10;
    for (int i = 0; i < warmup; ++i) {
        (void)d.generate_id();
    }
    const int max_pending_approx = 500;   // valor conocido de MAX_PENDING
    const int extra = 10;
    const int total_waiters = max_pending_approx + extra;
    std::vector<std::thread> waiters;
    waiters.reserve(total_waiters);
    std::atomic<int> system_full_count{0};
    for (int i = 0; i < total_waiters; ++i)
    {
        uint64_t id = d.generate_id();
        waiters.emplace_back([&, id]() {
            // Timeout suficientemente grande para que los hilos "extra" vean el mapa lleno
            auto status = d.wait_for_response(id, 500);
            if (status == ResponseStatus::SYSTEM_FULL) {
                system_full_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : waiters) {
        t.join();
    }
    // Con tantos hilos por encima del límite, al menos uno debe ver SYSTEM_FULL
    int count = system_full_count.load(std::memory_order_relaxed);
    EXPECT_GT(count, 0) << "No waiter observed SYSTEM_FULL, possible bug in capacity guard";
}