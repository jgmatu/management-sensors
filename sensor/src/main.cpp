#include <string>
#include <chrono>
#include <thread>
#include <random>
#include <boost/json.hpp>
#include <mqtt/async_client.h>
#include <log/Log.hpp>

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

    logging::Logger::instance().info(
        "sensor",
        "[Telemetry] Sensor " + std::to_string(sensor_id) + " producer started!"
    );

    while (!st.stop_requested()) {
        try {
            double temp = distribution(generator);

            boost::json::object obj;
            obj["sensor_id"] = sensor_id;
            obj["temp"] = std::round(temp * 100.0) / 100.0;

            std::string payload = boost::json::serialize(obj);

            client.publish(TELEMETRY_TOPIC, payload, 1, false);
        }
        catch (const mqtt::exception& exc) {
            logging::Logger::instance().error(
                "sensor",
                std::string("[Telemetry-Error] MQTT: ") + exc.what()
            );
        }

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

        logging::Logger::instance().info(
            "sensor",
            "Connecting CPU Node: " + CLIENT_ID + "..."
        );
        client.connect(connOpts)->wait();

        std::jthread telemetry_thread([&client](std::stop_token st) {
            run_telemetry_producer(st, client, 1);
        });

        logging::Logger::instance().info(
            "sensor",
            "[Main] Subscriber active. Waiting for 'config/requested'..."
        );
        client.subscribe(REQUEST_CONFIG_TOPIC, 1)->wait();

        for (;;)
        {
            auto msg = client.consume_message();
            if (!msg) break;

            try
            {
                auto jv = json::parse(msg->to_string());
                auto& obj = jv.as_object();

                logging::Logger::instance().info(
                    "sensor",
                    std::string("[CONFIG] Received Update: ") + json::serialize(jv)
                );

                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                obj["channel"] = "config_events";
                std::string payload = boost::json::serialize(obj);

                logging::Logger::instance().info(
                    "sensor",
                    std::string("[CONFIG] Publish commit config: ") + json::serialize(obj)
                );

                client.publish(EVENTS_CONFIG_TOPIC, payload, 1, false);
            }
            catch (const std::exception& e) {
                logging::Logger::instance().error(
                    "sensor",
                    std::string("[JSON-Error] Malformed message: ") + e.what()
                );
            }
        }
    }
    catch (const std::exception& e)
    {
        logging::Logger::instance().error(
            "sensor",
            std::string("Error: ") + e.what()
        );
        return 1;
    }

    return 0;
}