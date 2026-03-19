#include <db/DatabaseManager.hpp>
#include <libpq-fe.h> // Raw libpq header
#include <sys/epoll.h>
#include <iostream>
#include <log/Log.hpp>

DatabaseManager::DatabaseManager(const std::string& connection_listener_str) 
        : conn_str_(connection_listener_str), listener_thread_(nullptr) {}

DatabaseManager::~DatabaseManager()
{
    std::cout << "[DB] Destructor called: Starting graceful cleanup..." << std::endl;

    // 1. Cerrar la conexión física
    // Esto despertará a wait_notification() con una excepción broken_connection
    // permitiendo que el jthread salga de su bucle.
    disconnect();

    // 2. Sincronizar hilo de escucha (si fue arrancado)
    // Puede ser nullptr si nunca se llamó a run_listener_loop().
    if (listener_thread_ && listener_thread_->joinable())
    {
        listener_thread_->request_stop(); // Señal adicional de C++20
        listener_thread_->join();
    }

    // Aqui si puedo liberar el puntero y evitar cualquier acceso futuro a connection_listener_.
    connection_listener_.reset();
    connection_queries_.reset();
    std::cout << "[DB] Cleanup complete. Resources released." << std::endl;
}

// Establish connection
void DatabaseManager::connect()
{
    // Bloquea hasta que termine la función, asegurando que no haya operaciones concurrentes usando connection_listener_.
    std::lock_guard<std::mutex> lock(conn_mutex_); 

    try
    {
        connection_listener_ = std::make_unique<pqxx::connection>(conn_str_);
        connection_queries_ = std::make_unique<pqxx::connection>(conn_str_);

        if (connection_listener_->is_open() && connection_queries_->is_open()) {
            std::cout << "[DB] Database connection name: " << connection_listener_->dbname() << std::endl;
        }
    }
    catch (const pqxx::broken_connection& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Broken connection: " + std::string(e.what())
        );
        throw;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        throw;
    }
}

void DatabaseManager::disconnect()
{
    // Bloquea hasta que termine la función, asegurando que no haya operaciones concurrentes usando connection_listener_.
    std::lock_guard<std::mutex> lock(conn_mutex_); 

    try 
    {
        if (connection_listener_ && connection_listener_->is_open()) 
        {
            connection_listener_->close(); // Cierra la conexión física
        }
        if (connection_queries_ && connection_queries_->is_open()) 
        {
            connection_queries_->close(); // Cierra la conexión física
        }
    }
    catch (const pqxx::broken_connection& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Broken connection: " + std::string(e.what())
        );
    }
    catch (const pqxx::sql_error& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] SQL error: " + std::string(e.what())
        );
        throw;
    }
    catch (const std::exception& e) 
    {
        std::cerr << "Error during disconnect: " << e.what() << std::endl;
    }
}

void DatabaseManager::upsert_sensor_state(int sensor_id, double temp)
{
    // 1. Thread safety: Use the DML-specific mutex
    std::lock_guard<std::mutex> lock(conn_mutex_);

    if (!connection_queries_ || !connection_queries_->is_open()) {
        throw std::runtime_error("DML Connection not available for telemetry.");
    }

    try
    {
        pqxx::work txn(*connection_queries_);

        // 2. UPSERT Logic: 
        // If the sensor_id exists, UPDATE the temperature and timestamp.
        // If it doesn't exist, INSERT a new row.
        txn.exec(
            "INSERT INTO sensor_state (sensor_id, current_temp) "
            "VALUES ($1, $2) "
            "ON CONFLICT (sensor_id) DO UPDATE SET "
            "current_temp = EXCLUDED.current_temp, "
            "last_update = NOW();",
            pqxx::params{sensor_id, temp}
        );

        txn.commit();
        std::cout << "[DB] Telemetry updated for Sensor " << sensor_id << std::endl;
    }
    catch (const pqxx::sql_error& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] SQL error: " + std::string(e.what())
        );
        throw;
    }
    catch (const pqxx::broken_connection& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Broken connection: " + std::string(e.what())
        );
        // Do NOT try to call connection_listener_->... here.
        // Reset your local flags so the jthread exits cleanly.
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Error: " + std::string(e.what())
        );
    }
}

void DatabaseManager::upsert_sensor_config(int sensor_id, 
                                          const std::string& hostname, 
                                          const std::string& ip, 
                                          bool is_active, 
                                          uint64_t request_id)
{
    std::lock_guard<std::mutex> lock(conn_mutex_);

    try
    {
        pqxx::work txn(*connection_queries_); // Or your main connection object

        // UPSERT atómico sin pedir cert_id externamente:
        // - Si el sensor ya existe, mantiene su cert_id actual.
        // - Si es alta inicial, intenta resolver cert_id desde sensor_certs (cert_id == sensor_id).
        auto res = txn.exec(
            "INSERT INTO sensor_config "
                "(sensor_id, hostname, ip_address, is_active, request_id, cert_id) "
                "VALUES ("
                "$1, $2, $3, $4, $5, "
                "COALESCE("
                    "(SELECT cert_id FROM sensor_config WHERE sensor_id = $1), "
                    "(SELECT cert_id FROM sensor_certs WHERE cert_id = $1)"
                ")"
                ") "
                "ON CONFLICT (sensor_id) DO UPDATE SET "
                "hostname   = EXCLUDED.hostname, "
                "ip_address = EXCLUDED.ip_address, "
                "is_active  = EXCLUDED.is_active, "
                "request_id = EXCLUDED.request_id "
                "RETURNING cert_id;",
            pqxx::params{sensor_id, hostname, ip, is_active, request_id}
        );

        if (res.empty() || res[0][0].is_null()) {
            throw std::runtime_error(
                "No certificate associated with sensor " + std::to_string(sensor_id) +
                ". Create/associate sensor_certs first."
            );
        }

        txn.commit();
        logging::Logger::instance().debug(
            "db",
            "[DB] Master config synced for Sensor " + std::to_string(sensor_id)
        );
    }
    catch (const pqxx::sql_error& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] SQL error: " + std::string(e.what())
        );
        throw;
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Error: " + std::string(e.what())
        );
        // Logic: You might want to throw or return a ResponseStatus::DB_ERROR here
    }
}

void DatabaseManager::add_pending_config(int sensor_id, 
                                         const std::string& hostname, 
                                         const std::string& ip, 
                                         bool is_active,
                                         u_int64_t request_id)
{
    std::lock_guard<std::mutex> lock(conn_mutex_);

    if (!connection_queries_ || !connection_queries_->is_open()) {
        std::cout << "[DB] Connection not available. Cannot add pending config." << std::endl;
        throw std::runtime_error("Database connection lost.");
    }

    try
    {
        pqxx::work txn(*connection_queries_);

        txn.exec(
            "INSERT INTO sensor_config_pending ("
                "request_id, "
                "sensor_id, "
                "requested_hostname, "
                "requested_ip, "
                "requested_is_active, "
                "status, "
                "requested_at"
            ") VALUES ($1, $2, $3, $4, $5, 'PENDING', NOW());",
            pqxx::params{
                request_id, // $1: request_id (BIGINT)
                sensor_id,  // $2: sensor_id (INT)
                hostname,   // $3: requested_hostname (VARCHAR)
                ip,         // $4: requested_ip (INET)
                is_active   // $5: requested_is_active (BOOLEAN)
            }
        );      

        txn.commit();
        logging::Logger::instance().debug(
            "db",
            "[DB] Pending config created for Request " + std::to_string(request_id) +
            " (Sensor " + std::to_string(sensor_id) + ")"
        );
        
    }
    catch (const pqxx::sql_error& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] SQL error: " + std::string(e.what())
        );
        throw;
    }
    catch (const pqxx::broken_connection& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Broken connection: " + std::string(e.what())
        );
        // Do NOT try to call connection_listener_->... here.
        // Reset your local flags so the jthread exits cleanly.
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Error: " + std::string(e.what())
        );
        throw;
    }
}

boost::json::object DatabaseManager::get_sanity_info()
{
    std::lock_guard<std::mutex> lock(conn_mutex_);

    boost::json::object info;

    if (!connection_queries_ || !connection_queries_->is_open()) {
        info["error"] = "Database connection not available";
        return info;
    }

    try
    {
        // Use a nontransaction for read-only sanity checks
        pqxx::nontransaction ntxn(*connection_queries_);

        // 1. Get PostgreSQL Server Version
        pqxx::row pg_ver = ntxn.exec("SELECT version();").one_row();
        info["postgres_version"] = pg_ver[0].c_str();

        // 2. Get Database Uptime (useful for health checks)
        pqxx::row uptime = ntxn.exec("SELECT now() - pg_postmaster_start_time();").one_row();
        info["db_uptime"] = uptime[0].c_str();

        // 3. Current connection backend PID
        info["backend_pid"] = connection_queries_->backendpid();

        // 4. pqxx Library Info
        info["pqxx_version"] = PQXX_VERSION;

        return info;
    } 
    catch (const std::exception& e) 
    {
        logging::Logger::instance().error(
            "db",
            "[DB] Error: " + std::string(e.what())
        );
        info["error"] = e.what();
        return info;
    }
}

void DatabaseManager::register_listen_async(const std::string& channel, std::function<void(boost::json::object)> callback) 
{
    std::lock_guard<std::mutex> lock(conn_mutex_);

    logging::Logger::instance().debug(
        "db",
        "[DB-Listener] Register channel: " + std::string(channel)
    );

    try 
    {
        if (!connection_listener_ || !connection_listener_->is_open()) {
            throw std::runtime_error("Database connection is not established.");
        }

        pqxx::nontransaction nt(*connection_listener_);

        // Use quote_name to avoid syntax errors with channel names
        nt.exec("LISTEN " + nt.quote_name(channel));

        nt.commit();
        // 1. Register the high-level handler in our map
        callbacks_[channel] = std::move(callback);

        connection_listener_->listen(channel, [channel, this](const pqxx::notification& n) {
            std::function<void(boost::json::object)> handler;

            logging::Logger::instance().debug(
                "db",
                "[DB-Listener] Received listen event from channel: " + std::string(channel)
            );

            if (callbacks_.contains(channel))
            {
                handler = callbacks_[channel];
            }

            // Process and dispatch
            if (handler)
            {
                boost::json::object msg;
                parser_notify(n, msg); // Your custom parser
                handler(std::move(msg));
            }
        });
    }
    catch (const pqxx::broken_connection& e) {
        /*  
            * EXPECTED DISCONNECTION SIGNAL:
            * This occurs when disconnect() is called or the DB server drops the socket.
            * We treat this as a signal to finalize the thread gracefully.
            */
        logging::Logger::instance().error(
            "db",
            "[DB-Listener] Connection closed or lost: " + std::string(e.what())
        );
    }
    catch (const std::exception& e) {
        /* 
            * UNEXPECTED CRITICAL ERROR:
            * Handle other logic errors (JSON parsing, bad SQL, etc.)
            */
        logging::Logger::instance().error(
            "db",
            "[DB-Listener] Unexpected error: " + std::string(e.what())
        );
    }
}

void DatabaseManager::run_listener_loop()
{
    if (listener_thread_) return;

    std::cout << "[DB-Listener] Running wait notifications. " << std::endl;

    listener_thread_ = std::make_unique<std::jthread>([this](std::stop_token st) {
        try 
        {
            for (;;)
            {
                /*
                    Hacking note: libpqxx's wait_notification() is designed to be
                    efficient and will internally use select() or poll() on the PostgreSQL socket.

                    * Note: wait_notification() is a blocking call that internally uses select() or poll()
                    * on the PostgreSQL socket. It will return when a notification is received or if the
                    * connection is lost. If the connection is lost, it will throw an exception which we catch
                    * to handle reconnection logic if needed.
                */
                connection_listener_->wait_notification();
            }
        }
        catch (const pqxx::broken_connection& e) {
            /*  
             * EXPECTED DISCONNECTION SIGNAL:
             * This occurs when disconnect() is called or the DB server drops the socket.
             * We treat this as a signal to finalize the thread gracefully.
             */
            logging::Logger::instance().error(
                "db",
                "[DB-Listener] Connection closed or lost: " + std::string(e.what())
            );
        }
        catch (const std::exception& e) {
            /* 
             * UNEXPECTED CRITICAL ERROR:
             * Handle other logic errors (JSON parsing, bad SQL, etc.)
             */
             logging::Logger::instance().error(
                "db",
                "[DB-Listener] Unexpected error: " + std::string(e.what())
            );
        }
    });
}

void DatabaseManager::join()
{
    if (listener_thread_ && listener_thread_->joinable()) {
        listener_thread_->join();
    }
}

void DatabaseManager::parser_notify(const pqxx::notification& n, boost::json::object& msg)
{
    msg["channel"] = n.channel.c_str();
    try
    {
        // Parse the stringified payload into #include <sys/epoll.h>a JSON value
        msg["payload"] = boost::json::parse(n.payload.c_str());
    }
    catch (const std::exception& e)
    {
        // Fallback: if it's not valid JSON, store it as a raw string
        msg["payload"] = n.payload.c_str();
    }
}
