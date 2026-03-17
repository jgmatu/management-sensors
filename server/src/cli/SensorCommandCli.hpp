#pragma once

#include <string>
#include <iostream>

struct SensorCommand {
    std::string cmd;
    int id = -1;
    std::string attr;
    std::string value;
    bool valid = false;

    friend std::ostream& operator<<(std::ostream& os, const SensorCommand& command);
};

/**
 * High-level entry point for handling sensor-related CLI commands.
 *
 * This class is intended to encapsulate all parsing and execution logic
 * for commands coming from the command-line interface, keeping it
 * decoupled from the TLS server and database layers.
 */
class SensorCommandCli
{
public:

    SensorCommandCli(const std::string& command);
    ~SensorCommandCli();

    /**
     * Execute the previously parsed command.
     *
     * @param command The command to execute.
     * @return Textual result to be returned to the caller.
     */
    std::string execute(const SensorCommand& command);

    /**
     * Convenience one-shot helper: parse and execute in a single call.
     *
     * @param line Raw command line string.
     * @return Textual result to be returned to the caller.
     */
    std::string handle(const SensorCommand& command);

    SensorCommand command_;

private:
    /**
     * Parse a raw CLI line into internal state.
     *
     * @param line Raw command line string.
     */
     SensorCommand parse_sensor_command(const std::string& line);
};
