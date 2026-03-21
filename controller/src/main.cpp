#include <string>
#include <chrono>
#include <thread>
#include <functional>
#include <mosquitto.h>

#include <boost/json.hpp>
#include <mqtt/async_client.h> // Using async_client for better performance
#include <db/DatabaseManager.hpp>
#include <json/JsonUtils.hpp>
#include <log/Log.hpp>

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
    std::string channel = std::string(msg.at("channel").as_string());
    logging::Logger::instance().info(
        "controller",
        "[DB-Handler] Event on channel: " + channel
    );

    // 2. Log the full JSON payload
    logging::Logger::instance().info(
        "controller",
        "[DB-Handler] Full JSON: " + JsonUtils::toString(msg)
    );

    // 3. Example Logic:
    if (!g_mqtt_client || !g_mqtt_client->is_connected())
    {
        logging::Logger::instance().error(
            "controller",
            "[MQTT-Bridge] MQTT client not connected"
        );
        return;
    }

    try
    {
        std::string payload = boost::json::serialize(msg);

        auto pubmsg = mqtt::make_message(REQUEST_CONFIG_TOPIC, payload);
        pubmsg->set_qos(1);

        g_mqtt_client->publish(pubmsg);

        logging::Logger::instance().info(
            "controller",
            "[MQTT-Bridge] Forwarded JSON to: " + REQUEST_CONFIG_TOPIC
        );
    }
    catch (const std::exception& e) {
        logging::Logger::instance().error(
            "controller",
            std::string("[MQTT-Bridge] Serialization/Publish Error: ") + e.what()
        );
    }
}

// Definimos el tipo de handler para mayor claridad
using MqttHandler = std::function<void(const boost::json::object&)>;

// El mapa de despacho (Literal Map)
const std::unordered_map<std::string, MqttHandler> mqtt_dispatch_table = {

    {TELEMETRY_TOPIC, [](const boost::json::object& obj) {
        int sensor_id = static_cast<int>(obj.at("sensor_id").as_int64());
        double temp   = obj.at("temp").as_double();

        logging::Logger::instance().info(
            "controller",
            "[TELEMETRY] Node: " + std::to_string(sensor_id) +
                " | Temp: " + std::to_string(temp) + "°C"
        );
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

            std::string msg =
                "------------------------------------------\n"
                "[CONFIG-EVENT] New Configuration Received\n"
                " > Action:   " + action + "\n"
                " > Sensor:   " + std::to_string(sensor_id) + "\n"
                " > Hostname: " + hostname + "\n"
                " > IP Addr:  " + ip + "\n"
                " > Request ID:  " + std::to_string(request_id) + "\n"
                " > Status:   " + std::string(is_active ? "ACTIVE" : "INACTIVE") + "\n"
                "------------------------------------------";
            logging::Logger::instance().info("controller", msg);

            g_db->upsert_sensor_config(sensor_id, hostname, ip, is_active, request_id);
        }
    }}
};

int main()
{
    logging::Logger::instance().set_process_name("controller");
    try
    {
        // Build the connection string with TCP Keep-Alive parameters
        std::string conn_str =
            "dbname=javi "
            "user=javi "
            "password=12345678 "
            "host=localhost "
            "port=5432 "
            "keepalives=1 "
            "keepalives_idle=60 "
            "keepalives_interval=5 "
            "keepalives_count=3";

        // Initialize the global shared_ptr
        g_db = std::make_shared<DatabaseManager>(conn_str);

        g_db->connect();
        g_db->register_listen_async("config_requested", on_db_event_received);
        g_db->run_listener_loop();
    }
    catch(const std::exception& e)
    {
        logging::Logger::instance().error("controller", e.what());
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

        g_mqtt_client->connect(connOpts)->wait();

        logging::Logger::instance().info(
            "controller",
            "MQTT Client initialized and connected globally."
        );

        logging::Logger::instance().info(
            "controller",
            "[Agent] Agent active. Listening for Telemetry and Config..."
        );

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

                auto it = mqtt_dispatch_table.find(topic);

                if (it != mqtt_dispatch_table.end())
                {
                    it->second(obj);
                    continue;
                }

                logging::Logger::instance().info(
                    "controller",
                    "[Agent] Unknown Topic: " + topic
                );
            }
        }
        catch (const std::exception& e) {
            logging::Logger::instance().error(
                "controller",
                std::string("[Agent-Error] ") + e.what()
            );
        }
    }
    catch (const mqtt::exception& exc) {
        logging::Logger::instance().error(
            "controller",
            std::string("MQTT Init Error: ") + exc.what()
        );
        return 1;
    }

    logging::Logger::instance().info(
        "controller",
        "Wait until db finalize"
    );
    g_db->join();
    g_db.reset();
    return 0;
}