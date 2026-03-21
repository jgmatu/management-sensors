#include <cli/QuantumCommandCli.hpp>
#include <db/DatabaseManager.hpp>
#include <dispatcher/Dispatcher.hpp>
#include <json/JsonUtils.hpp>
#include <log/Log.hpp>

#include <boost/json.hpp>
#include <sstream>
#include <string>

QuantumCommandCli::QuantumCommandCli(
    Dispatcher& dispatcher,
    std::shared_ptr<DatabaseManager> db)
    : dispatcher_(dispatcher)
    , db_(std::move(db))
{
    register_commands();
}

QuantumCommandCli::~QuantumCommandCli()
{
}

boost::asio::awaitable<void> QuantumCommandCli::handle_session(TlsStream& stream)
{
    constexpr size_t BUFFER_SIZE = 8192;
    std::vector<uint8_t> buffer(BUFFER_SIZE);

    for (;;)
    {
        size_t n = co_await stream.async_read_some(boost::asio::buffer(buffer));
        std::string request(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n));

        auto cmd = parse_sensor_command(request);

        if (!cmd.valid && cmd.cmd == "quit") {
            co_return;
        }

        logging::Logger::instance().info(
            "server",
            "[CLI] Command received: " + JsonUtils::toString(
                boost::json::value{
                    { "cmd",    cmd.cmd },
                    { "id",     cmd.id },
                    { "attr",   cmd.attr },
                    { "value",  cmd.value },
                    { "valid",  cmd.valid }
                }
            )
        );

        std::string response = execute(cmd);

        if (!response.empty()) {
            std::vector<uint8_t> out(response.begin(), response.end());
            out.push_back('\n');
            co_await stream.async_write_some(boost::asio::buffer(out));
        }
    }
}

void QuantumCommandCli::register_commands()
{
    command_registry_ = {
        {"CONFIG_IP", [this](const QuantumCommand& sc) -> std::string {
            uint64_t request_id = db_->generate_request_id();

            logging::Logger::instance().info(
                "server",
                "[CLI] New CONFIG_IP request created | sensor_id=" +
                    std::to_string(sc.id) +
                    " | request_id=" + std::to_string(request_id)
            );

            db_->add_pending_config(
                sc.id, "sensor-" + std::to_string(sc.id),
                sc.value, true, request_id);

            logging::Logger::instance().info(
                "server",
                "[CLI] Waiting for DB confirmation | request_id=" +
                    std::to_string(request_id) +
                    " | timeout_ms=" + std::to_string(REQUEST_TIMEOUT_MS)
            );

            auto status = dispatcher_.wait_for_response(request_id, REQUEST_TIMEOUT_MS);

            logging::Logger::instance().info(
                "server",
                "[CLI] DB wait finished | request_id=" +
                    std::to_string(request_id) +
                    " | status=" + status_to_string(status)
            );

            if (status == ResponseStatus::SUCCESS) {
                return "OK: Sensor " + std::to_string(sc.id) + " updated successfully.";
            }
            return "ERROR: " + status_to_string(status) +
                   " (ID: " + std::to_string(request_id) + ")";
        }},
        {"REBOOT", [](const QuantumCommand& sc) -> std::string {
            return "OK: Rebooting sensor " + std::to_string(sc.id);
        }}
    };
}

std::string QuantumCommandCli::status_to_string(ResponseStatus status)
{
    switch (status) {
        case ResponseStatus::SUCCESS:     return "SUCCESS";
        case ResponseStatus::TIMEOUT:     return "ERROR: Request Timed Out";
        case ResponseStatus::DB_ERROR:    return "ERROR: Database Failure";
        case ResponseStatus::SYSTEM_FULL: return "ERROR: Maximum Pending Requests Reached";
        case ResponseStatus::PENDING:     return "PENDING";
        default:                          return "UNKNOWN_ERROR";
    }
}

QuantumCommand QuantumCommandCli::parse_sensor_command(const std::string& line)
{
    QuantumCommand sc;
    sc.valid = false;
    auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return sc;
    }
    auto last = line.find_last_not_of(" \t\r\n");
    const std::string trimmed = line.substr(first, last - first + 1);
    std::istringstream iss(trimmed);

    if (!(iss >> sc.cmd)) return sc;
    if (!(iss >> sc.id))  return sc;
    if (!(iss >> sc.attr)) return sc;
    if (!(iss >> sc.value)) return sc;

    std::string extra;
    if (iss >> extra) return sc;

    sc.valid = true;
    return sc;
}

std::string QuantumCommandCli::execute(const QuantumCommand& command)
{
    auto it = command_registry_.find(command.cmd);
    if (it != command_registry_.end()) {
        return it->second(command);
    }
    return "Command '" + command.cmd + "' not found in registry.";
}

std::ostream& operator<<(std::ostream& os, const QuantumCommand& command)
{
    os << "[QuantumCommand] " << "\n"
       << (command.valid ? "VALID" : "INVALID") << "\n"
       << "  Command: " << (command.cmd.empty() ? "N/A" : command.cmd) << "\n"
       << "  ID:      " << command.id << "\n"
       << "  Attr:    " << (command.attr.empty() ? "N/A" : command.attr) << "\n"
       << "  Value:   " << (command.value.empty() ? "N/A" : command.value);
    return os;
}
