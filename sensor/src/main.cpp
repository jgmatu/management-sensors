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


/**
 * @brief Función de telemetría para el sensor.
 */
void run_telemetry_producer(std::stop_token st, mqtt::async_client& client, int sensor_id)
{
    std::default_random_engine generator;
    std::uniform_real_distribution<double> distribution(30.0, 80.0);

    std::cout << "[Telemetry] Sensor " << sensor_id << " producer started!" << std::endl;

    while (!st.stop_requested()) {
        try {
            double temp = distribution(generator);

            boost::json::object obj;
            obj["sensor_id"] = sensor_id;
            obj["temp"] = std::round(temp * 100.0) / 100.0;

            std::string payload = boost::json::serialize(obj);

            // Publicamos usando la API de Paho C++
            // Importante: TELEMETRY_TOPIC debe ser const char* o std::string
            client.publish(TELEMETRY_TOPIC, payload, 1, false);

        } catch (const mqtt::exception& exc) {
            std::cerr << "[Telemetry-Error] MQTT: " << exc.what() << std::endl;
        }

        // Espera de 10 segundos o hasta que se solicite la parada
        std::this_thread::sleep_for(std::chrono::seconds(100000));
    }
}

int main()
{
    mqtt::async_client client(ADDRESS, CLIENT_ID);

    auto connOpts = mqtt::connect_options_builder()
        .clean_session(true)
        .keep_alive_interval(std::chrono::seconds(30))
        .finalize();

    try
    {
        client.start_consuming();

        std::cout << "Connecting CPU Node: " << CLIENT_ID << "..." << std::endl;
        client.connect(connOpts)->wait();

        std::jthread telemetry_thread([&client](std::stop_token st) {
            run_telemetry_producer(st, client, 1);
        });

        std::cout << "[Main] Subscriber active. Waiting for 'config/requested'..." << std::endl;
        client.subscribe(REQUEST_CONFIG_TOPIC, 1)->wait();

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