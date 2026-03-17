#include <gtest/gtest.h>
#include <db/DatabaseManager.hpp>
#include <cstdlib>
#include <string>

namespace {

std::string get_test_conn_str()
{
    // Permite sobreescribir la conexión vía entorno: DB_TEST_CONN_STR
    if (const char* env = std::getenv("DB_TEST_CONN_STR")) {
        return std::string(env);
    }

    // Fallback: ajusta estos valores a tu entorno de Postgres de pruebas
    return
        "dbname=javi "
        "user=javi "
        "password=12345678 "
        "host=localhost "
        "port=5432 ";
}

} // namespace

TEST(DatabaseManagerTest, ConnectAndDisconnect)
{
    const std::string conn_str = get_test_conn_str();
    DatabaseManager db(conn_str);

    try {
        db.connect();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Cannot connect to test database: " << e.what();
    }

    // Si llegamos aquí, la conexión fue exitosa
    EXPECT_NO_THROW(db.disconnect());
}

TEST(DatabaseManagerTest, GetSanityInfoHasBasicFields)
{
    const std::string conn_str = get_test_conn_str();
    DatabaseManager db(conn_str);

    try {
        db.connect();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Cannot connect to test database: " << e.what();
    }

    auto info = db.get_sanity_info();

    // Los nombres concretos pueden variar, pero estos vienen de tu implementación
    EXPECT_TRUE(info.if_contains("postgres_version")) << "missing postgres_version";
    EXPECT_TRUE(info.if_contains("db_uptime"))        << "missing db_uptime";
    EXPECT_TRUE(info.if_contains("backend_pid"))      << "missing backend_pid";
    EXPECT_TRUE(info.if_contains("pqxx_version"))     << "missing pqxx_version";

    db.disconnect();
}

TEST(DatabaseManagerTest, ListenerReceivesNotifyEvent)
{
    const std::string conn_str = get_test_conn_str();
    DatabaseManager db(conn_str);
    try {
        db.connect();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Cannot connect to test database: " << e.what();
    }
    const std::string channel = "test_events";
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> received{false};
    boost::json::object last_msg;
    // Registrar el listener antes de arrancar el loop
    db.register_listen_async(channel, [&](boost::json::object msg) {
        {
            std::lock_guard<std::mutex> lock(m);
            last_msg = msg;
            received.store(true, std::memory_order_relaxed);
        }
        cv.notify_one();
    });

    // Arrancar el hilo de escucha interno
    db.run_listener_loop();

    // Desde otra conexión, enviar un NOTIFY al canal
    std::thread notifier([&] {
        try
        {
            pqxx::connection conn(conn_str);
            pqxx::work txn(conn);
            // Payload JSON sencillo
            auto res = txn.exec(
                "NOTIFY " + txn.quote_name(channel) +
                ", '{\"key\":\"value\"}'"
            );
            txn.commit();
        }
        catch (const std::exception& e)
        {
            // Si falla el NOTIFY, dejamos que el test falle por timeout
            std::cerr << "[Test-Notifier] " << e.what() << std::endl;
        }
    });

    // Esperar a que el callback marque "received"
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return received.load(std::memory_order_relaxed);
        });
    }
    notifier.join();

    db.disconnect();
    db.join();

    ASSERT_TRUE(received.load(std::memory_order_relaxed))
        << "Listener did not receive NOTIFY on channel '" << channel << "'";
    // Comprobar contenido básico del mensaje
    ASSERT_TRUE(last_msg.if_contains("channel"));
    EXPECT_EQ(last_msg.at("channel").as_string(), channel);
    ASSERT_TRUE(last_msg.if_contains("payload"));
    auto& payload = last_msg.at("payload").as_object();
    ASSERT_TRUE(payload.if_contains("key"));
    EXPECT_EQ(payload.at("key").as_string(), "value");
}

TEST(DatabaseManagerTest, ListenerStopsCleanlyOnDisconnect)
{
    const std::string conn_str = get_test_conn_str();
    DatabaseManager db(conn_str);
    try {
        db.connect();
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Cannot connect to test database: " << e.what();
    }
    const std::string channel = "shutdown_test_events";
    std::mutex m;
    std::condition_variable cv;
    std::atomic<bool> first_event_received{false};
    // 1) Registrar un listener sencillo
    db.register_listen_async(channel, [&](boost::json::object msg) {
        (void)msg;
        {
            std::lock_guard<std::mutex> lock(m);
            first_event_received.store(true, std::memory_order_relaxed);
        }
        cv.notify_one();
    });
    // 2) Arrancar el hilo de escucha
    db.run_listener_loop();
    // 3) Enviar un NOTIFY para asegurarnos de que el listener está activo
    std::thread notifier([&] {
        try {
            pqxx::connection conn(conn_str);
            pqxx::work txn(conn);
            auto res = txn.exec(
                "NOTIFY " + txn.quote_name(channel) +
                ", '{\"key\":\"value\"}'"
            );
            (void)res.no_rows(); // evitar warning de resultado sin usar
            txn.commit();
        } catch (const std::exception& e) {
            std::cerr << "[Test-Notifier] " << e.what() << std::endl;
        }
    });
    {
        std::unique_lock<std::mutex> lock(m);
        cv.wait_for(lock, std::chrono::seconds(2), [&] {
            return first_event_received.load(std::memory_order_relaxed);
        });
    }
    notifier.join();
    // 4) Medir que disconnect() + join() no se bloquean
    auto start = std::chrono::steady_clock::now();
    db.disconnect();
    db.join();   // método que sincroniza con listener_thread_
    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Si el listener se cierra de forma ordenada, esto debería ser rápido
    EXPECT_LT(elapsed_ms, 1000) << "Listener shutdown took too long (possible deadlock)";
    SUCCEED(); // Si llegamos aquí sin colgar ni segfault, el cierre es correcto
}

TEST(DatabaseManagerTest, DestructorStopsListenerAndCleansUp)
{
    const std::string conn_str = get_test_conn_str();
    // Usamos un bloque para controlar claramente el lifetime del objeto
    auto start = std::chrono::steady_clock::now();
    {
        DatabaseManager db(conn_str);
        try {
            db.connect();
        } catch (const std::exception& e) {
            GTEST_SKIP() << "Cannot connect to test database: " << e.what();
        }
        const std::string channel = "dtor_shutdown_test";
        std::atomic<bool> event_seen{false};
        std::mutex m;
        std::condition_variable cv;
        db.register_listen_async(channel, [&](boost::json::object msg) {
            (void)msg;
            {
                std::lock_guard<std::mutex> lock(m);
                event_seen.store(true, std::memory_order_relaxed);
            }
            cv.notify_one();
        });
        db.run_listener_loop();
        // Enviar un NOTIFY para asegurarnos de que el listener está realmente activo
        std::thread notifier([&] {
            try {
                pqxx::connection conn(conn_str);
                pqxx::work txn(conn);
                auto res = txn.exec(
                    "NOTIFY " + txn.quote_name(channel) +
                    ", '{\"event\":\"before_dtor\"}'"
                );
                (void)res.no_rows();
                txn.commit();
            } catch (const std::exception& e) {
                std::cerr << "[Test-Notifier] " << e.what() << std::endl;
            }
        });
        {
            std::unique_lock<std::mutex> lock(m);
            cv.wait_for(lock, std::chrono::seconds(2), [&] {
                return event_seen.load(std::memory_order_relaxed);
            });
        }
        notifier.join();
        // No llamamos a disconnect() ni join(): al salir del scope se invoca ~DatabaseManager()
        // y debe encargarse de cerrar conexiones y parar el listener sin colgarse.
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    // Si el destructor se queda bloqueado esperando al listener o a la conexión,
    // este tiempo se dispararía.
    EXPECT_LT(elapsed_ms, 1500) << "Destructor took too long (possible deadlock in listener shutdown)";
}