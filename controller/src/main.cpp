#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <functional>
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

// Definimos el tipo de handler para mayor claridad
using MqttHandler = std::function<void(const boost::json::object&)>;

// El mapa de despacho (Literal Map)
const std::unordered_map<std::string, MqttHandler> mqtt_dispatch_table = {
    
    {TELEMETRY_TOPIC, [](const boost::json::object& obj) {
        int sensor_id = static_cast<int>(obj.at("sensor_id").as_int64());
        double temp   = obj.at("temp").as_double();

        std::cout << "[TELEMETRY] Node: " << sensor_id << " | Temp: " << temp << "°C" << std::endl;
        g_db->upsert_sensor_state(sensor_id, temp);
    }},

    {EVENTS_CONFIG_TOPIC, [](const boost::json::object& obj) {
        if (obj.contains("payload")) {
            auto const& payload = obj.at("payload").as_object();
            
            int sensor_id        = payload.at("sensor_id").as_int64();
            std::string hostname = payload.at("hostname").as_string().c_str();
            std::string ip       = payload.at("ip_address").as_string().c_str();
            bool is_active       = payload.at("is_active").as_bool();
            std::string action   = payload.at("action").as_string().c_str();
            u_int64_t request_id = payload.at("request_id").as_int64();

            // 3. Complete Data Trace
            std::cout << "------------------------------------------" << std::endl;
            std::cout << "[CONFIG-EVENT] New Configuration Received" << std::endl;
            std::cout << " > Action:   " << action << std::endl;
            std::cout << " > Sensor:   " << sensor_id << std::endl;
            std::cout << " > Hostname: " << hostname << std::endl;
            std::cout << " > IP Addr:  " << ip << std::endl;
            std::cout << " > Request ID:  " << request_id << std::endl;
            std::cout << " > Status:   " << (is_active ? "ACTIVE" : "INACTIVE") << std::endl;
            std::cout << "------------------------------------------" << std::endl;

            g_db->upsert_sensor_config(sensor_id, hostname, ip, is_active, request_id);
        }
    }}
};

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
        g_db->run_listener_loop();
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
        std::cout << "[Agent] Agent active. Listening for Telemetry and Config..." << std::endl;

        // Subscribe to the config topic defined in your DB Trigger
        g_mqtt_client->subscribe(EVENTS_CONFIG_TOPIC, 1)->wait();
        g_mqtt_client->subscribe(TELEMETRY_TOPIC, 1)->wait();
        try
        {

            for (;;)
            {
                auto msg = g_mqtt_client->consume_message();
                if (!msg) continue;

                std::string topic = msg->get_topic();
                std::string payload = msg->to_string();
                auto jv = boost::json::parse(payload);
                auto obj = jv.as_object();

                // Buscamos el tópico en nuestro mapa
                auto it = mqtt_dispatch_table.find(topic);

                if (it != mqtt_dispatch_table.end())
                {
                    it->second(obj);
                    continue;
                }
                std::cout << "[Agent] Unknown Topic: " << topic << std::endl;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[Agent-Error] " << e.what() << std::endl;
        }
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