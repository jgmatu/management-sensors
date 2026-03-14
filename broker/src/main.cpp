#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mqtt/async_client.h> // Using async_client for better performance
#include <broker/DatabaseManager.hpp>

#define DATABASE_CERT "/home/javi/OpenSource/botan-tls-testserver/server/certs/ca.pem"

const std::string ADDRESS { "tcp://broker.emqx.io:1883" };
const std::string CLIENT_ID { "cpp_subscriber_client" };
const std::string TOPIC { "test/topic" };

void insert_data(int cpu_id, double load) {
    try {
        // 1. Connect to the database
        // Connection string format: "host=localhost dbname=metrics_db user=postgres password=secret"
        pqxx::connection conn(
            "dbname=javi "
            "user=javi "
            "password=12345678 "
            "host=localhost "
            "port=5432 "
            "sslmode=verify-full "      // Fuerza SSL y verifica el certificado del servidor
            "sslrootcert=" DATABASE_CERT " " // Ruta al certificado de la CA (opcional según modo)
            "keepalives=1 "             // Activa Keep-Alive a nivel de TCP
            "keepalives_idle=60 "       // Segundos antes de enviar el primer keepalive
            "keepalives_interval=5 "    // Segundos entre reintentos si no hay respuesta
            "keepalives_count=3"        // Número de fallos antes de cerrar la conexión
        );

        if (conn.is_open()) {
            std::cout << "Connected to: " << conn.dbname() << std::endl;
        }

        // 2. Create a transaction
        pqxx::work tx(conn);

        // 3. Execute the query using "parameterized" inputs (prevents SQL injection)
        // Table assumes: id (SERIAL), cpu_id (INT), load_val (DOUBLE), ts (TIMESTAMP DEFAULT NOW())
        tx.exec(
            "INSERT INTO cpu_metrics (cpu_id, load_val) VALUES ($1, $2)",
            pqxx::params{cpu_id, load} 
        );


        // 4. Commit the changes
        tx.commit();
        std::cout << "Metric saved for CPU " << cpu_id << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Database Error: " << e.what() << std::endl;
    }
}

int main() {
    // 1. Create the client
    mqtt::async_client cli(ADDRESS, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
        .clean_session(false)
        .keep_alive_interval(std::chrono::seconds(30))
        .finalize();

    try {
        // 2. Start consuming (this is the easiest way to receive)
        std::cout << "Connecting to " << ADDRESS << "..." << std::endl;
        cli.start_consuming();
        cli.connect(connOpts)->wait();

        // 3. Subscribe to the topic
        std::cout << "Subscribing to topic: " << TOPIC << "..." << std::endl;
        cli.subscribe(TOPIC, 1)->wait();

        std::cout << "Waiting for messages... (Press Ctrl+C to stop)" << std::endl;

        for(;;)
        {
            // This blocks until a message arrives
            auto msg = cli.consume_message();
            if (!msg) break; // Should not happen unless client stops

            std::cout << "Received Message: [" << msg->get_topic() << "] " 
                      << msg->to_string() << std::endl;
        }
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;
        return 1;
    }

    return 0;
}