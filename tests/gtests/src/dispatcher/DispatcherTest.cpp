#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

#include <dispatcher/Dispatcher.hpp>

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

TEST(DispatcherTest, WaitForResponseTimesOutWhenNoDispatch)
{
    Dispatcher d;
    uint64_t id = 1;
    auto start = std::chrono::steady_clock::now();
    ResponseStatus s = d.wait_for_response(id, 50);
    auto end = std::chrono::steady_clock::now();
    EXPECT_EQ(s, ResponseStatus::TIMEOUT) << status_to_cstr(s);
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    EXPECT_GE(elapsed_ms, 40);
}

TEST(DispatcherTest, WaitForResponseIsWokenByDispatchSuccess)
{
    Dispatcher d;
    uint64_t id = 10;
    std::thread worker([&](){
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        d.dispatch(id, ResponseStatus::SUCCESS);
    });
    ResponseStatus s = d.wait_for_response(id, 200);
    worker.join();
    EXPECT_EQ(s, ResponseStatus::SUCCESS) << status_to_cstr(s);
}

TEST(DispatcherTest, WaitForResponseIsWokenByDispatchDbError)
{
    Dispatcher d;
    uint64_t id = 20;
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
    d.dispatch(999, ResponseStatus::SUCCESS);
    ResponseStatus s = d.wait_for_response(1000, 10);
    EXPECT_EQ(s, ResponseStatus::TIMEOUT);
}

TEST(DispatcherTest, ReturnsSystemFullWhenTooManyPending)
{
    Dispatcher d;
    const int max_pending_approx = 500;
    const int extra = 10;
    const int total_waiters = max_pending_approx + extra;
    std::vector<std::thread> waiters;
    waiters.reserve(total_waiters);
    std::atomic<int> system_full_count{0};
    for (int i = 0; i < total_waiters; ++i)
    {
        uint64_t id = static_cast<uint64_t>(i + 1);
        waiters.emplace_back([&, id]() {
            auto status = d.wait_for_response(id, 500);
            if (status == ResponseStatus::SYSTEM_FULL) {
                system_full_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& t : waiters) {
        t.join();
    }
    int count = system_full_count.load(std::memory_order_relaxed);
    EXPECT_GT(count, 0) << "No waiter observed SYSTEM_FULL, possible bug in capacity guard";
}
