#include <testserver/DatabaseManager.hpp>

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
        if (!connection_ || !connection_->is_open()) {
            connect(); 
        }

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
