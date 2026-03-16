#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mosquitto.h>

#include <mqtt/async_client.h> // Using async_client for better performance
#include <db/DatabaseManager.hpp>

const std::string ADDRESS { "tcp://localhost:1883" };
const std::string CLIENT_ID { "cpp_subscriber_client" };
const std::string TOPIC { "test/topic" };


int main()
{
    DatabaseManager db(
        "dbname=javi "
        "user=javi "
        "password=12345678 "
        "host=localhost "
        "port=5432 "
        "keepalives=1 "             // Activa Keep-Alive a nivel de TCP
        "keepalives_idle=60 "       // Segundos antes de enviar el primer keepalive
        "keepalives_interval=5 "    // Segundos entre reintentos si no hay respuesta
        "keepalives_count=3"        // Número de fallos antes de cerrar la conexión
    );
    db.connect();

    db.listen_async('config_request')

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