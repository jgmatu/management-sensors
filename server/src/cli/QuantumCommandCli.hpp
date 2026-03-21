#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>

#include <net/ISessionHandler.hpp>

class Dispatcher;
class DatabaseManager;
enum class ResponseStatus;

struct QuantumCommand {
    std::string cmd;
    int id = -1;
    std::string attr;
    std::string value;
    bool valid = false;

    friend std::ostream& operator<<(std::ostream& os, const QuantumCommand& command);
};

/**
 * High-level entry point for handling sensor-related CLI commands.
 *
 * Implements ISessionHandler so the TLS engine can delegate raw-mode
 * sessions to this class without knowing protocol details.
 */
class QuantumCommandCli : public ISessionHandler
{
public:

    QuantumCommandCli(Dispatcher& dispatcher,
                      std::shared_ptr<DatabaseManager> db);
    ~QuantumCommandCli();

    boost::asio::awaitable<void> handle_session(TlsStream& stream) override;

    /**
     * Execute a single parsed command through the internal registry.
     *
     * @param command The command to execute.
     * @return Textual result to be returned to the caller.
     */
    std::string execute(const QuantumCommand& command);

private:
    QuantumCommand parse_sensor_command(const std::string& line);
    void register_commands();
    static std::string status_to_string(ResponseStatus status);

    using CommandHandler = std::function<std::string(const QuantumCommand&)>;
    std::map<std::string, CommandHandler> command_registry_;

    Dispatcher& dispatcher_;
    std::shared_ptr<DatabaseManager> db_;

    static constexpr int REQUEST_TIMEOUT_MS = 2000;
};
