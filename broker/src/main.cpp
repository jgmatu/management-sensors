#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mqtt/async_client.h> // Using async_client for better performance
#include <db/DatabaseManager.hpp>

#define DATABASE_CERT "/home/javi/OpenSource/botan-tls-testserver/server/certs/ca.pem"

const std::string ADDRESS { "tcp://broker.emqx.io:1883" };
const std::string CLIENT_ID { "cpp_subscriber_client" };
const std::string TOPIC { "test/topic" };

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