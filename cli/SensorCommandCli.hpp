#pragma once

#include <string>
#include <vector>

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
    SensorCommandCli() = default;
    ~SensorCommandCli() = default;

    /**
     * Parse a raw CLI line into internal state.
     *
     * @param line Raw command line string.
     */
    void parse(const std::string& line);

    /**
     * Execute the previously parsed command.
     *
     * @return Textual result to be returned to the caller.
     */
    std::string execute();

    /**
     * Convenience one-shot helper: parse and execute in a single call.
     *
     * @param line Raw command line string.
     * @return Textual result to be returned to the caller.
     */
    std::string handle(const std::string& line);

private:
    // Placeholder for future CLI-specific state (command name, args, etc.).
};

