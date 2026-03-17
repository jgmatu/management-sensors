#include <cli/SensorCommandCli.hpp>
#include <string>
#include <sstream>


SensorCommandCli::SensorCommandCli(const std::string& command)
    : command_(parse_sensor_command(command))
{
}

SensorCommandCli::~SensorCommandCli()
{
    ;
}

SensorCommand SensorCommandCli::parse_sensor_command(const std::string& line)
{
    SensorCommand sc;
    sc.valid = false;
    // 1. Normaliza espacios al principio y final
    auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return sc; // empty line or only spaces
    }
    auto last = line.find_last_not_of(" \t\r\n");
    const std::string trimmed = line.substr(first, last - first + 1);
    std::istringstream iss(trimmed);

    // 2. CMD
    if (!(iss >> sc.cmd)) {
        return sc;
    }
    // 3. ID (int)
    if (!(iss >> sc.id)) {
        return sc;
    }
    // 4. ATTR
    if (!(iss >> sc.attr)) {
        return sc;
    }
    // 5. VALUE
    if (!(iss >> sc.value)) {
        return sc;
    }

    // 6. Rechaza basura extra
    std::string extra;
    if (iss >> extra) {
        return sc;
    }
    sc.valid = true;
    return sc;
}

std::string SensorCommandCli::execute(const SensorCommand& command)
{
    // TODO: implement CLI execution logic
    // For now, just echo the raw line to signal the stub is wired.
    return command.cmd + " " + std::to_string(command.id) + " " + command.attr + " " + command.value;
}

std::string SensorCommandCli::handle(const SensorCommand& command)
{
    return execute(command);
}

std::ostream& operator<<(std::ostream& os, const SensorCommand& command)
{
    os << "[SensorCommand] " << "\n"
       << (command.valid ? "VALID" : "INVALID") << "\n"
       << "  Command: " << (command.cmd.empty() ? "N/A" : command.cmd) << "\n"
       << "  ID:      " << command.id << "\n"
       << "  Attr:    " << (command.attr.empty() ? "N/A" : command.attr) << "\n"
       << "  Value:   " << (command.value.empty() ? "N/A" : command.value);
    return os;
}