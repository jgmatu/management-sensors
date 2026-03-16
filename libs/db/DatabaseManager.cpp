#include <db/DatabaseManager.hpp>
#include <libpq-fe.h> // Raw libpq header
#include <sys/epoll.h>

DatabaseManager::DatabaseManager(const std::string& connection_listener_str) 
        : conn_str_(connection_listener_str) {}

DatabaseManager::~DatabaseManager()
{
    std::cout << "[DB] Destructor called: Starting graceful cleanup..." << std::endl;

    // 1. Cerrar la conexión física
    // Esto despertará a wait_notification() con una excepción broken_connection
    // permitiendo que el jthread salga de su bucle.
    disconnect();

    // 2. Sincronizar hilos
    // jthread hace join automático, pero llamarlo aquí asegura 
    // que el objeto no termine de destruirse hasta que el hilo de escucha muera.
    if (listener_thread_.joinable())
    {
        listener_thread_.request_stop(); // Señal adicional de C++20
        listener_thread_.join();
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
        if (connection_listener_->is_open()) {
            std::cout << "Connected to: " << connection_listener_->dbname() << std::endl;
        }
        connection_queries_ = std::make_unique<pqxx::connection>(conn_str_);
        if (connection_queries_->is_open()) {
            std::cout << "Connected to: " << connection_queries_->dbname() << std::endl;
        }
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
            // Opcional: Notificar el cierre
            std::cout << "Disconnecting from: " << connection_listener_->dbname() << std::endl;
            
            connection_listener_->close(); // Cierra la conexión física
        }
        if (connection_queries_ && connection_queries_->is_open()) 
        {
            // Opcional: Notificar el cierre
            std::cout << "Disconnecting from: " << connection_queries_->dbname() << std::endl;
            
            connection_queries_->close(); // Cierra la conexión física
        }
    }
    catch (const std::exception& e) 
    {
        std::cerr << "Error during disconnect: " << e.what() << std::endl;
    }
}

void DatabaseManager::add_pending_config(int sensor_id, 
                                         const std::string& hostname, 
                                         const std::string& ip, 
                                         bool is_active)
{
    std::lock_guard<std::mutex> lock(conn_mutex_);

    std::cout << "[DB] Adding pending config for Sensor ID " << sensor_id << std::endl;

    if (!connection_queries_ || !connection_queries_->is_open()) {
        std::cout << "[DB] Connection not available. Cannot add pending config." << std::endl;
        throw std::runtime_error("Database connection lost.");
    }

    try
    {
        pqxx::work txn(*connection_queries_);

        std::cout << "[DB] Executing pending config insert for Sensor ID " << sensor_id << std::endl;

        // Force the DB to give up if it can't get a lock in 3 seconds
        // This prevents the TLS session from hanging indefinitely
        txn.exec("SET LOCAL lock_timeout = '3s';");

        // --- START TRACE ---
        std::cout << "\n[SQL-TRACE] ==========================================" << std::endl;
        std::cout << "QUERY: INSERT INTO sensor_config_pending (sensor_id, new_hostname, new_ip_address, new_is_active)" << std::endl;
        std::cout << "       VALUES ($1, $2, $3, $4) ON CONFLICT (sensor_id) DO UPDATE SET..." << std::endl;
        std::cout << "VALUES: $1=" << sensor_id 
                << ", $2='" << hostname 
                << "', $3='" << ip 
                << "', $4=" << (is_active ? "TRUE" : "FALSE") << std::endl;
        std::cout << "[SQL-TRACE] ==========================================\n" << std::endl;
        // --- END TRACE ---

        // CORRECT LIBPQXX 8.0 SYNTAX:
        // You must explicitly wrap your arguments in pqxx::params{}
        // Usamos ON CONFLICT para manejar el error de duplicado (UPSERT)
        txn.exec(
            "INSERT INTO sensor_config_pending "
            "(sensor_id, new_hostname, new_ip_address, new_is_active) "
            "VALUES ($1, $2, $3, $4) "
            "ON CONFLICT (sensor_id) DO UPDATE SET "
            "new_hostname = EXCLUDED.new_hostname, "
            "new_ip_address = EXCLUDED.new_ip_address, "
            "new_is_active = EXCLUDED.new_is_active, "
            "requested_at = NOW();", // Opcional: actualizar el timestamp
            pqxx::params{sensor_id, hostname, ip, is_active}
        );
        std::cout << "[DB] Pending config insert executed for Sensor ID " << sensor_id << std::endl;
        txn.commit();
        std::cout << "[DB] Pending config queued successfully." << std::endl;
    }
    catch (const pqxx::broken_connection& e)
    {
        std::cerr << "[DB] FATAL: Connection lost (Server terminated session). " << e.what() << std::endl;
        // Do NOT try to call connection_listener_->... here.
        // Reset your local flags so the jthread exits cleanly.
    }
    catch (const std::exception& e)
    {
        std::cerr << "[DB-Error] " << e.what() << std::endl;
        throw;
    }
}

boost::json::object DatabaseManager::get_sanity_info()
{
    boost::json::object info;
    
    // Acquire lock to ensure connection_listener_ isn't being reset by another thread
    std::lock_guard<std::mutex> lock(conn_mutex_);
    
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
        std::cerr << "[DB-Sanity] Check Failed: " << e.what() << std::endl;
        info["error"] = e.what();
        return info;
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

void DatabaseManager::listen_async(const std::string& channel, std::function<void(boost::json::object)> callback) 
{
    // Assign the thread to the member variable
    listener_thread_ = std::jthread([this, channel, callback](std::stop_token st) 
    {
        try 
        {
            // 1. Setup the LISTEN command
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                if (!connection_listener_ || !connection_listener_->is_open()) {
                    throw std::runtime_error("Database connection is not established.");
                }

                pqxx::nontransaction nt(*connection_listener_);
                // Use quote_name to avoid syntax errors with channel names
                nt.exec("LISTEN " + nt.quote_name(channel));

                // nt is destroyed here when the scope ends, 
                // ensuring no transaction is active for the next step.
                nt.commit();
            }

            // 2. Register the handler (after nt is gone)
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                if (!connection_listener_ || !connection_listener_->is_open()) {
                    throw std::runtime_error("Database connection is not established.");
                }

                connection_listener_->listen(channel, [this, callback](pqxx::notification n) {
                    boost::json::object msg;
                    parser_notify(n, msg);
                    callback(msg);
                });
            }

            for (;;)
            {
               // Data available: let libpqxx process it
                // If the connection is broken, this will throw pqxx::broken_connection
                std::cout << "Waiting for notification..." << std::endl;
                /*
                    Hacking note: libpqxx's wait_notification() is designed to be
                    efficient and will internally use select() or poll() on the PostgreSQL socket.

                    * Note: wait_notification() is a blocking call that internally uses select() or poll()
                    * on the PostgreSQL socket. It will return when a notification is received or if the
                    * connection is lost. If the connection is lost, it will throw an exception which we catch
                    * to handle reconnection logic if needed.
                */
                connection_listener_->wait_notification(); // Timeout of 1 second to allow periodic stop checks
            }
            std::cout << "Listener thread stopping gracefully." << std::endl;
        }
        catch (const pqxx::broken_connection& e) {
            /*  
             * EXPECTED DISCONNECTION SIGNAL:
             * This occurs when disconnect() is called or the DB server drops the socket.
             * We treat this as a signal to finalize the thread gracefully.
             */
            std::cerr << "[DB-Listener] Connection closed or lost: " << e.what() << std::endl;
        }
        catch (const std::exception& e) {
            /* 
             * UNEXPECTED CRITICAL ERROR:
             * Handle other logic errors (JSON parsing, bad SQL, etc.)
             */
            std::cerr << "[DB-Listener] Unexpected error: " << e.what() << std::endl;
        }
        std::cout << "Listener database thread exiting." << std::endl;
    });
}

void DatabaseManager::join()
{
    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}
