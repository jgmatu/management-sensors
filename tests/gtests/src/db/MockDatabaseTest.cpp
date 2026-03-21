#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <thread>
#include <chrono>
#include <atomic>

#include <db/DatabaseManager.hpp>
#include <dispatcher/Dispatcher.hpp>

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::AtLeast;

class MockDatabaseManager : public DatabaseManager
{
public:
    MockDatabaseManager() : DatabaseManager("mock://not-a-real-db") {}

    MOCK_METHOD(void, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(boost::json::object, get_sanity_info, (), (override));
    MOCK_METHOD(void, register_listen_async,
        (const std::string&, std::function<void(boost::json::object)>), (override));
    MOCK_METHOD(void, run_listener_loop, (), (override));
    MOCK_METHOD(void, join, (), (override));

    MOCK_METHOD(void, init_request_id_sequence, (), (override));
    MOCK_METHOD(uint64_t, generate_request_id, (), (override));

    MOCK_METHOD(void, add_pending_config,
        (int, const std::string&, const std::string&, bool, uint64_t), (override));

    MOCK_METHOD(void, upsert_sensor_config,
        (int, const std::string&, const std::string&, bool, uint64_t), (override));

    MOCK_METHOD(void, upsert_sensor_state, (int, double), (override));
};

// --- Pipeline flow tests (generate_request_id -> add_pending -> dispatch) ---

TEST(MockDatabaseTest, GenerateRequestIdReturnsMockedValue)
{
    MockDatabaseManager db;

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(42))
        .WillOnce(Return(43))
        .WillOnce(Return(44));

    EXPECT_EQ(db.generate_request_id(), 42u);
    EXPECT_EQ(db.generate_request_id(), 43u);
    EXPECT_EQ(db.generate_request_id(), 44u);
}

TEST(MockDatabaseTest, InitSequenceCalledOnce)
{
    MockDatabaseManager db;

    EXPECT_CALL(db, connect()).Times(1);
    EXPECT_CALL(db, init_request_id_sequence()).Times(1);
    EXPECT_CALL(db, disconnect()).Times(1);

    db.connect();
    db.init_request_id_sequence();
    db.disconnect();
}

TEST(MockDatabaseTest, PipelineFlowConfigIpSuccess)
{
    MockDatabaseManager db;
    Dispatcher dispatcher;

    const uint64_t mock_request_id = 100;
    const int sensor_id = 1;
    const std::string ip = "10.0.0.1/24";

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(mock_request_id));

    EXPECT_CALL(db, add_pending_config(
        sensor_id, "sensor-1", ip, true, mock_request_id))
        .Times(1);

    uint64_t request_id = db.generate_request_id();
    EXPECT_EQ(request_id, mock_request_id);

    db.add_pending_config(sensor_id, "sensor-1", ip, true, request_id);

    std::thread pipeline([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        dispatcher.dispatch(request_id, ResponseStatus::SUCCESS);
    });

    auto status = dispatcher.wait_for_response(request_id, 500);
    pipeline.join();

    EXPECT_EQ(status, ResponseStatus::SUCCESS);
}

TEST(MockDatabaseTest, PipelineFlowConfigIpTimeout)
{
    MockDatabaseManager db;
    Dispatcher dispatcher;

    const uint64_t mock_request_id = 200;

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(mock_request_id));

    EXPECT_CALL(db, add_pending_config(_, _, _, _, mock_request_id))
        .Times(1);

    uint64_t request_id = db.generate_request_id();
    db.add_pending_config(1, "sensor-1", "10.0.0.1/24", true, request_id);

    auto status = dispatcher.wait_for_response(request_id, 50);
    EXPECT_EQ(status, ResponseStatus::TIMEOUT);
}

TEST(MockDatabaseTest, PipelineFlowConfigIpDbError)
{
    MockDatabaseManager db;
    Dispatcher dispatcher;

    const uint64_t mock_request_id = 300;

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(mock_request_id));

    EXPECT_CALL(db, add_pending_config(_, _, _, _, mock_request_id))
        .Times(1);

    uint64_t request_id = db.generate_request_id();
    db.add_pending_config(1, "sensor-1", "10.0.0.1/24", true, request_id);

    std::thread pipeline([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        dispatcher.dispatch(request_id, ResponseStatus::DB_ERROR);
    });

    auto status = dispatcher.wait_for_response(request_id, 500);
    pipeline.join();

    EXPECT_EQ(status, ResponseStatus::DB_ERROR);
}

TEST(MockDatabaseTest, AddPendingConfigThrowsPropagatesError)
{
    MockDatabaseManager db;

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(400));

    EXPECT_CALL(db, add_pending_config(_, _, _, _, _))
        .WillOnce(::testing::Throw(std::runtime_error("Connection lost")));

    uint64_t request_id = db.generate_request_id();

    EXPECT_THROW(
        db.add_pending_config(1, "sensor-1", "10.0.0.1/24", true, request_id),
        std::runtime_error
    );
}

TEST(MockDatabaseTest, UpsertSensorConfigCalledAfterSuccess)
{
    MockDatabaseManager db;
    Dispatcher dispatcher;

    const uint64_t mock_request_id = 500;
    const int sensor_id = 3;
    const std::string ip = "192.168.1.1/32";

    EXPECT_CALL(db, generate_request_id())
        .WillOnce(Return(mock_request_id));
    EXPECT_CALL(db, add_pending_config(sensor_id, _, ip, true, mock_request_id))
        .Times(1);
    EXPECT_CALL(db, upsert_sensor_config(sensor_id, _, ip, true, mock_request_id))
        .Times(1);

    uint64_t request_id = db.generate_request_id();
    db.add_pending_config(sensor_id, "sensor-3", ip, true, request_id);

    std::thread pipeline([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        dispatcher.dispatch(request_id, ResponseStatus::SUCCESS);
    });

    auto status = dispatcher.wait_for_response(request_id, 500);
    pipeline.join();

    ASSERT_EQ(status, ResponseStatus::SUCCESS);
    db.upsert_sensor_config(sensor_id, "sensor-3", ip, true, request_id);
}

TEST(MockDatabaseTest, UpsertSensorStateUpdatesTemperature)
{
    MockDatabaseManager db;

    EXPECT_CALL(db, upsert_sensor_state(1, 23.5)).Times(1);
    EXPECT_CALL(db, upsert_sensor_state(1, 24.1)).Times(1);
    EXPECT_CALL(db, upsert_sensor_state(2, 19.0)).Times(1);

    db.upsert_sensor_state(1, 23.5);
    db.upsert_sensor_state(1, 24.1);
    db.upsert_sensor_state(2, 19.0);
}

TEST(MockDatabaseTest, ConcurrentRequestIdsAreUnique)
{
    MockDatabaseManager db;
    Dispatcher dispatcher;

    std::atomic<uint64_t> counter{1000};
    EXPECT_CALL(db, generate_request_id())
        .WillRepeatedly(Invoke([&]() -> uint64_t {
            return counter.fetch_add(1);
        }));

    EXPECT_CALL(db, add_pending_config(_, _, _, _, _))
        .Times(AtLeast(1));

    const int num_requests = 10;
    std::vector<uint64_t> ids(num_requests);

    for (int i = 0; i < num_requests; ++i)
    {
        ids[i] = db.generate_request_id();
        db.add_pending_config(i + 1, "sensor-" + std::to_string(i + 1),
                              "10.0.0." + std::to_string(i + 1) + "/24",
                              true, ids[i]);
    }

    std::set<uint64_t> unique_ids(ids.begin(), ids.end());
    EXPECT_EQ(unique_ids.size(), static_cast<size_t>(num_requests))
        << "All generated request IDs must be unique";
}

TEST(MockDatabaseTest, GetSanityInfoReturnsMockedData)
{
    MockDatabaseManager db;

    boost::json::object mock_info;
    mock_info["postgres_version"] = "PostgreSQL 18.3 (mock)";
    mock_info["db_uptime"] = "00:00:01";
    mock_info["backend_pid"] = 12345;
    mock_info["pqxx_version"] = "mock-7.0";

    EXPECT_CALL(db, get_sanity_info())
        .WillOnce(Return(mock_info));

    auto info = db.get_sanity_info();
    EXPECT_TRUE(info.if_contains("postgres_version"));
    EXPECT_TRUE(info.if_contains("db_uptime"));
    EXPECT_TRUE(info.if_contains("backend_pid"));
    EXPECT_TRUE(info.if_contains("pqxx_version"));
    EXPECT_EQ(info.at("backend_pid").as_int64(), 12345);
}
