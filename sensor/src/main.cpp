#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <boost/json.hpp>
#include <mqtt/async_client.h>

namespace json = boost::json;

// Configuration
const std::string ADDRESS   { "tcp://localhost:1883" };
const std::string CLIENT_ID { "cpu_node_01" }; // Unique for each of the 100 CPUs
const std::string TOPIC     { "test/topic" };

int main() {
    mqtt::async_client client(ADDRESS, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
        .clean_session(true)
        .keep_alive_interval(std::chrono::seconds(30))
        .finalize();

    // Random temp generator (30.0 - 80.0 C)
    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(30.0, 80.0);

    try {
        std::cout << "Connecting CPU Node: " << CLIENT_ID << "..." << std::endl;
        client.connect(connOpts)->wait();

        for (;;)
        {
            // 1. Generate Data
            double temp = distribution(generator);

            // 2. Build JSON with Boost.JSON
            json::object obj;
            obj["sensor_id"] = 1; // Must exist in 'sensor_config' table
            obj["temp"]      = std::round(temp * 100.0) / 100.0; // 2 decimals
            
            std::string payload = json::serialize(obj);

            // 3. Publish to Broker
            std::cout << "Sending: " << payload << std::endl;
            client.publish(TOPIC, payload, 1, false)->wait();

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}