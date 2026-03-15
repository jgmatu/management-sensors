#include <db/DatabaseManager.hpp>
#include <libpq-fe.h> // Raw libpq header
#include <sys/epoll.h>

DatabaseManager::DatabaseManager(const std::string& connection_str) 
        : conn_str_(connection_str) {}

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

    // Aqui si puedo liberar el puntero y evitar cualquier acceso futuro a connection_.
    connection_.reset();
    std::cout << "[DB] Cleanup complete. Resources released." << std::endl;
}

// Establish connection
void DatabaseManager::connect()
{
    // Bloquea hasta que termine la función, asegurando que no haya operaciones concurrentes usando connection_.
    std::lock_guard<std::mutex> lock(conn_mutex_); 

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

void DatabaseManager::disconnect() 
{
    // Bloquea hasta que termine la función, asegurando que no haya operaciones concurrentes usando connection_.
    std::lock_guard<std::mutex> lock(conn_mutex_); 

    try 
    {
        if (connection_ && connection_->is_open()) 
        {
            // Opcional: Notificar el cierre
            std::cout << "Disconnecting from: " << connection_->dbname() << std::endl;
            
            connection_->close(); // Cierra la conexión física
        }
    }
    catch (const std::exception& e) 
    {
        std::cerr << "Error during disconnect: " << e.what() << std::endl;
    }
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
                if (!connection_ || !connection_->is_open()) {
                    throw std::runtime_error("Database connection is not established.");
                }

                pqxx::nontransaction nt(*connection_);
                // Use quote_name to avoid syntax errors with channel names
                nt.exec("LISTEN " + channel + ";");

                // nt is destroyed here when the scope ends, 
                // ensuring no transaction is active for the next step.
            }

            // 2. Register the handler (after nt is gone)
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                if (!connection_ || !connection_->is_open()) {
                    throw std::runtime_error("Database connection is not established.");
                }

                connection_->listen(channel, [this, callback](pqxx::notification n) {
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
                connection_->wait_notification();
            }
            std::cout << "Listener thread stopping gracefully." << std::endl;
        }
        catch (const std::exception& e) {
            /* 
             * SEÑALIZACIÓN DE PARADA POR DESCONEXIÓN:
             * Se captura broken_connection como un evento esperado de finalización.
             * Esto ocurre cuando otra clase invoca disconnect() o el socket se cierra.
             * El hilo interpreta este error como una orden de terminación inmediata.
             */
            std::cerr << "Listener error: " << e.what() << std::endl;
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