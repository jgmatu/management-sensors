#include <db/DatabaseManager.hpp>

#include <libpq-fe.h> // Raw libpq header
#include <sys/epoll.h>

DatabaseManager::DatabaseManager(const std::string& connection_str) 
        : conn_str_(connection_str) {}

    // Establish connection
void DatabaseManager::connect() 
{
    try
    {
        connection_ = std::make_unique<pqxx::connection>(conn_str_);
        if (connection_->is_open()) {
            std::cout << "Connected to: " << connection_->dbname() << std::endl;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Connection failed: " << e.what() << std::endl;
        throw;
    }
}

    // Example: Execute a simple query
void DatabaseManager::execute(const std::string& sql)
{
    pqxx::work txn(*connection_); // Starts a transaction

    txn.exec(sql);
    txn.commit(); // Explicitly commit
}

// Example: Fetch data (returns a result set)
pqxx::result DatabaseManager::query(const std::string& sql)
{
    pqxx::nontransaction ntxn(*connection_); // Read-only, no transaction overhead
    return ntxn.exec(sql);
}

boost::json::object DatabaseManager::get_sanity_info()
{
    boost::json::object info;
    try
    {
        pqxx::nontransaction ntxn(*connection_);

        // 1. Get PostgreSQL Server Version
        pqxx::row pg_ver = ntxn.exec("SELECT version();").one_row();
        info["postgres_version"] = pg_ver[0].c_str();

        // 2. Get SSL Status & Cipher (Fixed function name to ssl_version)
        pqxx::result ssl_info = ntxn.exec(
            "SELECT ssl_is_used(), ssl_cipher(), ssl_version();"
        );

        if (!ssl_info.empty()) {
            bool is_used = ssl_info[0][0].as<bool>();
            info["ssl_active"] = is_used;
            
            // Safe check for NULL values before calling .c_str()
            info["ssl_cipher"] = !ssl_info[0][1].is_null() ? ssl_info[0][1].c_str() : "none";
            info["ssl_protocol"] = !ssl_info[0][2].is_null() ? ssl_info[0][2].c_str() : "none";
        }

        // 3. Client Library Info
        info["libpq_compile_version"] = PQXX_VERSION;

        return info;
    } catch (const std::exception& e) {
        // Log locally and return the error in the JSON object
        std::cerr << "Sanity Check Failed: " << e.what() << std::endl;
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

void DatabaseManager::listen_async(const std::string& channel, 
                                std::function<void(boost::json::object)> callback)
{
    std::cout << "Starting async listener for channel: " << channel << std::endl;

    std::jthread([this, channel, callback](std::stop_token st)
    {
        try
        {
            if (!connection_ || !connection_->is_open()) {
                throw std::runtime_error("Database connection is not established.");
            }
            int sock = connection_->sock(); // Get the underlying Postgres socket

            // 1. Force the LISTEN command immediately
            pqxx::nontransaction nt(*connection_);
            nt.exec("LISTEN " + channel + ";");
            nt.commit(); // Nontransactions don't strictly need this, but it ensures execution

            // 2. Register the handler
            // The lambda receives a pqxx::notification object containing:
            // .channel, .payload, and .backend_pid
            connection_->listen(channel, [this, callback] (pqxx::notification n) {
                boost::json::object msg;
                parser_notify(n, msg); // Parse payload into JSON if possible
                callback(msg);
            });

            // 4. Enter a loop to wait for notifications
            // await_notification() blocks until a notification arrives or a timeout occurs

            for (;;)
            {
                // Data available: let libpqxx process it
                // If the connection is broken, this will throw pqxx::broken_connection
                std::cout << "Waiting for notification..." << std::endl;
                connection_->await_notification();
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            // throw;
        }
    }).detach(); // Detach the thread to run independently
}

/**
 * @brief Ejecuta cualquier operación DML (INSERT, UPDATE, DELETE).
 * @tparam Args Tipos de los parámetros para la query.
 * @param query La sentencia SQL con placeholders ($1, $2, etc).
 * @param args Los valores a insertar/actualizar.
 * @return true si la transacción se completó con éxito.
 */
template <typename... Args>
bool DatabaseManager::execute_dml(std::string_view query, Args&&... args) {
    try {
        // Creamos una transacción (W)ork
        pqxx::work tx(*connection_);

        // Usamos la sintaxis moderna de C++20 que libpqxx prefiere
        // Nota: exec() con parámetros es seguro contra SQL Injection
        tx.exec(query, pqxx::params{std::forward<Args>(args)...});

        tx.commit();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[DB-DML-ERROR] " << e.what() << " | Query: " << query << std::endl;
        return false;
    }
}
