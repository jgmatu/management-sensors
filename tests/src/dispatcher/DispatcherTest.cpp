#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include <dispatcher/Dispatcher.hpp>
#include "../TestUtils.hpp"

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