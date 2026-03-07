#pragma once

#include <boost/json.hpp>
#include <iostream>
#include <string>
#include <sstream>

class JsonUtils {
public:
    // Prints directly to a stream (e.g., std::cout or a file)
    static void print(std::ostream& os, boost::json::value const& jv, std::string indent = "");

    // Returns a pretty string (useful for logging or Next.js API responses)
    static std::string toString(boost::json::value const& jv);
};
