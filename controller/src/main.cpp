#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mosquitto.h>

#include <boost/json.hpp>
#include <mqtt/async_client.h> // Using async_client for better performance
#include <db/DatabaseManager.hpp>
#include <json/JsonUtils.hpp>

// Global pointer to the Database Manager
// Shared across the TLS Engine threads and the Main thread
std::shared_ptr<DatabaseManager> g_db;

// Global smart pointer to the MQTT client
std::unique_ptr<mqtt::async_client> g_mqtt_client = nullptr;

const std::string ADDRESS { "tcp://localhost:1883" };
/**
 * @brief Unique identifier for this node on the MQTT Broker.
 * Identifies this process as the central PQC Controller.
 */
const std::string CLIENT_ID { "pqc_controller_node" };
const std::string REQUEST_CONFIG_TOPIC { "config/requested" };
const std::string EVENTS_CONFIG_TOPIC { "config/events" };
const std::string TELEMETRY_TOPIC { "telemetry/state" };

/**
 * @brief Logic handler for Database NOTIFY events.
 * Processes JSON payloads from the PostgreSQL 'state_events' channel.
 */
void on_db_event_received(boost::json::object msg)
{
    if (msg.empty()) return;
    // 1. Extract the channel for logging
    std::string_view channel = msg.at("channel").as_string();
    std::cout << "[DB-Handler] Event on channel: " << channel << std::endl;

    // 2. Use your existing JsonUtils to print the payload
    JsonUtils::print(std::cout, msg);
    std::cout << std::endl;

    // 3. Example Logic:
    // Check if initialized and connected before using
    if (g_mqtt_client && g_mqtt_client->is_connected())
    {
        try
        {
            // 1. Serialize the boost::json object to a string
            std::string payload = boost::json::serialize(msg);

            // 2. Create the MQTT message with the JSON string
            auto pubmsg = mqtt::make_message(REQUEST_CONFIG_TOPIC, payload);
            
            // 3. Set Quality of Service (1 = At least once) for reliability
            pubmsg->set_qos(1);

            // 4. Global Publish
            g_mqtt_client->publish(pubmsg); 
            
            std::cout << "[MQTT-Bridge] Forwarded JSON to: " << REQUEST_CONFIG_TOPIC << std::endl;
        } 
        catch (const std::exception& e) {
            std::cerr << "[MQTT-Bridge] Serialization/Publish Error: " << e.what() << std::endl;
        }
    }
}

int main()
{
    try
    {
        // Build the connection string with TCP Keep-Alive parameters
        std::string conn_str = 
            "dbname=javi "
            "user=javi "
            "password=12345678 "
            "host=localhost "
            "port=5432 "
            "keepalives=1 "          // Enable TCP Keep-Alive
            "keepalives_idle=60 "    // 60s idle before first probe
            "keepalives_interval=5 " // 5s between probes
            "keepalives_count=3";    // Drop after 3 failed probes

        // Initialize the global shared_ptr
        g_db = std::make_shared<DatabaseManager>(conn_str);

        g_db->connect();
        g_db->register_listen_async("config_requested", on_db_event_received);
    }
    catch(const std::exception& e)
    {
        std::cerr << e.what() << '\n';
    }

    try
    {
        // 2. INITIALIZE THE GLOBAL SMART POINTER
        g_mqtt_client = std::make_unique<mqtt::async_client>(ADDRESS, CLIENT_ID);
        g_mqtt_client->start_consuming();

        // 3. Configure and Connect
        auto connOpts = mqtt::connect_options_builder()
            .clean_session(false)
            .keep_alive_interval(std::chrono::seconds(30))
            .finalize();

        // Use the global pointer to connect
        g_mqtt_client->connect(connOpts)->wait();

        std::cout << "MQTT Client initialized and connected globally." << std::endl;

        // LAUNCH SUBSCRIPTION AGENT (Background Listener for Dual Events)
        std::jthread telemetry_subs_agent([](std::stop_token st) {
            std::cout << "[Agent] Agent active. Listening for Telemetry and Config..." << std::endl;
            
            for (;;)
            {
                try
                {
                    if (g_mqtt_client && g_mqtt_client->is_connected())
                    {
                        auto msg = g_mqtt_client->consume_message();

                        if (msg)
                        {
                            std::string topic = msg->get_topic();
                            std::string payload = msg->to_string();
                            auto jv = boost::json::parse(payload);
                            auto obj = jv.as_object();

                            std::cout << "********* TOPIC ******** : " << topic << std::endl;

                            // --- SWITCH LOGIC BASED ON TOPIC ---
                            if (topic == TELEMETRY_TOPIC)
                            {
                                // Event: Real-time sensor data
                                std::cout << "[TELEMETRY] Node: " << obj["sensor_id"] << " | Temp: " << obj["temp"] << "°C" << std::endl;
                                int sensor_id = static_cast<int>(obj.at("sensor_id").as_int64());
                                double temp   = obj.at("temp").as_double();

                                std::cout << "Update sensor state!" << std::endl;
                                g_db->upsert_sensor_state(sensor_id, temp);
                            } 
                            else if (topic == EVENTS_CONFIG_TOPIC)
                            {
                                // Event: Configuration change from DB/PQC Engine
                                std::cout << "[CONFIG] Update for ID " << obj["sensor_id"]  << " | New IP: " << obj["ip_address"] << std::endl;
                                
                                // Trigger logic: update local state or notify system
                            }
                            else
                            {
                                std::cout << "[Agent] Unknown Topic: " << topic << std::endl;
                            }
                        }
                    }
                    else
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "[Agent-Error] " << e.what() << std::endl;
                }
            }
        });
    }
    catch (const mqtt::exception& exc) {
        std::cerr << "MQTT Init Error: " << exc.what() << std::endl;
        return 1;
    }

    // Wait until database finalize.
    std::cout << "Wait until db finalize" << std::endl;
    g_db->join();
    g_db.reset();
    return 0;
}