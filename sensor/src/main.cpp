#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <boost/json.hpp>
#include <mqtt/async_client.h>

namespace json = boost::json;

// Configuration
const std::string ADDRESS { "tcp://localhost:1883" };
const std::string CLIENT_ID { "pqc_sensor_node" };
const std::string REQUEST_CONFIG_TOPIC { "config/requested" };
const std::string EVENTS_CONFIG_TOPIC { "config/events" };
const std::string TELEMETRY_TOPIC { "telemetry/state" };

int main()
{
    mqtt::async_client client(ADDRESS, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
        .clean_session(true)
        .keep_alive_interval(std::chrono::seconds(30))
        .finalize();

    // Random temp generator (30.0 - 80.0 C)
    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(30.0, 80.0);

    try
    {
        client.start_consuming();

        std::cout << "Connecting CPU Node: " << CLIENT_ID << "..." << std::endl;
        client.connect(connOpts)->wait();

        // Subscribe to the config topic defined in your DB Trigger
        client.subscribe(REQUEST_CONFIG_TOPIC, 1)->wait();

        // 1. LAUNCH SENSOR THREAD (Background Telemetry)
        std::jthread telemetry_thread([&client](std::stop_token st) {
            std::default_random_engine generator;
            std::uniform_real_distribution<double> distribution(30.0, 80.0);

            std::cout << "Start telemtry producer sensor!" << std::endl;

            while (!st.stop_requested())
            {
                double temp = distribution(generator);

                json::object obj;
                obj["sensor_id"] = 1;
                obj["temp"] = std::round(temp * 100.0) / 100.0;

                std::string payload = boost::json::serialize(obj);

                // Publish to "telemetry/state"
                client.publish(TELEMETRY_TOPIC, payload, 1, false);

                // I need see multiple callbaks register 
                std::this_thread::sleep_for(std::chrono::milliseconds(10 * 1000));
            }
        });

        // 3. MAIN LOOP: Block and Wait for Config Requests
        std::cout << "[Main] Subscriber active. Waiting for 'config/requested'..." << std::endl;

        for (;;)
        {
            // This blocks the main thread until a message arrives
            auto msg = client.consume_message();
            if (!msg) break;

            try
            {
                auto jv = json::parse(msg->to_string());
                auto& obj = jv.as_object();
                std::cout << "[CONFIG] Received Update: " << json::serialize(jv) << std::endl;

                // Simulate config sensor process latency...
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                obj["channel"] = "config_events";
                std::string payload = boost::json::serialize(obj);

                std::cout << "[CONFIG] Publish commit config: " << json::serialize(obj) << std::endl;

                client.publish(EVENTS_CONFIG_TOPIC, payload, 1, false);
            }
            catch (const std::exception& e) {
                std::cerr << "[JSON-Error] Malformed message: " << e.what() << std::endl;
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}