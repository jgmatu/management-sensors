#include <json/JsonUtils.hpp>
#include <sstream>

void JsonUtils::print(std::ostream& os, boost::json::value const& jv, std::string indent)
{
    switch (jv.kind()) {
        case boost::json::kind::object: {
            os << "{\n";
            auto const& obj = jv.get_object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                os << indent << "  \"" << it->key() << "\" : ";
                print(os, it->value(), indent + "  ");
                os << (std::next(it) != obj.end() ? ",\n" : "\n");
            }
            os << indent << "}";
            break;
        }
        case boost::json::kind::array: {
            os << "[\n";
            auto const& arr = jv.get_array();
            for (auto it = arr.begin(); it != arr.end(); ++it) {
                os << indent << "  ";
                print(os, *it, indent + "  ");
                os << (std::next(it) != arr.end() ? ",\n" : "\n");
            }
            os << indent << "]";
            break;
        }
        default:
            os << boost::json::serialize(jv);
            break;
    }
}

// Returns a pretty string (useful for logging or Next.js API responses)
std::string JsonUtils::toString(boost::json::value const& jv)
{
    std::ostringstream ss;
    print(ss, jv);
    return ss.str();
}
