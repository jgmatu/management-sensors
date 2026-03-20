#pragma once

#include <string>

class IQuantumConnDetailsProvider
{
   public:
    virtual ~IQuantumConnDetailsProvider() = default;
    virtual std::string get_latest_connection_details() const = 0;
};
