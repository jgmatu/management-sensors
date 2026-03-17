#include "SensorCommandCli.hpp"

void SensorCommandCli::parse(const std::string& /*line*/)
{
    // TODO: implement CLI parsing logic
}

std::string SensorCommandCli::execute()
{
    // TODO: implement CLI execution logic
    return {};
}

std::string SensorCommandCli::handle(const std::string& line)
{
    parse(line);
    return execute();
}

