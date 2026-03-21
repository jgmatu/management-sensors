#pragma once

#include <string>

class IConnDetailsProvider
{
   public:
    virtual ~IConnDetailsProvider() = default;
    virtual std::string get_latest_connection_details() const = 0;
};
