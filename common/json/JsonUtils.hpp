#pragma once

#include <boost/json.hpp>
#include <iostream>
#include <string>

class JsonUtils {
public:
    static void print(std::ostream& os, boost::json::value const& jv, std::string indent = "");

    static std::string toString(boost::json::value const& jv);
};
