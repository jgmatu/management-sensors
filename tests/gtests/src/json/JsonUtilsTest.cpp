#include <gtest/gtest.h>
#include <json/JsonUtils.hpp>
#include <boost/json.hpp>
#include <sstream>

TEST(JsonUtilsTest, ObjectSimple)
{
    boost::json::object obj;
    obj["name"] = "sensor_1";
    obj["id"] = 42;
    obj["active"] = true;

    std::string result = JsonUtils::toString(boost::json::value(obj));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_object());
    EXPECT_EQ(parsed.as_object()["name"].as_string(), "sensor_1");
    EXPECT_EQ(parsed.as_object()["id"].as_int64(), 42);
    EXPECT_EQ(parsed.as_object()["active"].as_bool(), true);
}

TEST(JsonUtilsTest, NestedObject)
{
    boost::json::object inner;
    inner["ip"] = "10.0.0.1";
    inner["port"] = 8080;

    boost::json::object outer;
    outer["config"] = inner;
    outer["version"] = 3;

    std::string result = JsonUtils::toString(boost::json::value(outer));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_object());
    ASSERT_TRUE(parsed.as_object()["config"].is_object());
    EXPECT_EQ(parsed.as_object()["config"].as_object()["ip"].as_string(), "10.0.0.1");
    EXPECT_EQ(parsed.as_object()["config"].as_object()["port"].as_int64(), 8080);
    EXPECT_EQ(parsed.as_object()["version"].as_int64(), 3);
}

TEST(JsonUtilsTest, ArraySimple)
{
    boost::json::array arr = {1, 2, 3, 4, 5};

    std::string result = JsonUtils::toString(boost::json::value(arr));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.as_array().size(), 5u);
    EXPECT_EQ(parsed.as_array()[0].as_int64(), 1);
    EXPECT_EQ(parsed.as_array()[4].as_int64(), 5);
}

TEST(JsonUtilsTest, ArrayOfObjects)
{
    boost::json::object o1;
    o1["id"] = 1;
    o1["name"] = "alpha";

    boost::json::object o2;
    o2["id"] = 2;
    o2["name"] = "beta";

    boost::json::array arr;
    arr.push_back(o1);
    arr.push_back(o2);

    std::string result = JsonUtils::toString(boost::json::value(arr));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_array());
    ASSERT_EQ(parsed.as_array().size(), 2u);
    EXPECT_EQ(parsed.as_array()[0].as_object()["name"].as_string(), "alpha");
    EXPECT_EQ(parsed.as_array()[1].as_object()["name"].as_string(), "beta");
}

TEST(JsonUtilsTest, EmptyObject)
{
    boost::json::object obj;

    std::string result = JsonUtils::toString(boost::json::value(obj));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_object());
    EXPECT_TRUE(parsed.as_object().empty());
}

TEST(JsonUtilsTest, EmptyArray)
{
    boost::json::array arr;

    std::string result = JsonUtils::toString(boost::json::value(arr));
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_array());
    EXPECT_TRUE(parsed.as_array().empty());
}

TEST(JsonUtilsTest, ScalarString)
{
    boost::json::value jv("hello world");

    std::string result = JsonUtils::toString(jv);
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_string());
    EXPECT_EQ(parsed.as_string(), "hello world");
}

TEST(JsonUtilsTest, ScalarNumber)
{
    boost::json::value jv(3.14);

    std::string result = JsonUtils::toString(jv);
    boost::json::value parsed = boost::json::parse(result);

    ASSERT_TRUE(parsed.is_double());
    EXPECT_DOUBLE_EQ(parsed.as_double(), 3.14);
}

TEST(JsonUtilsTest, PrintToStream)
{
    boost::json::object obj;
    obj["key"] = "value";
    obj["count"] = 7;

    std::ostringstream oss;
    JsonUtils::print(oss, boost::json::value(obj));

    boost::json::value parsed = boost::json::parse(oss.str());
    ASSERT_TRUE(parsed.is_object());
    EXPECT_EQ(parsed.as_object()["key"].as_string(), "value");
    EXPECT_EQ(parsed.as_object()["count"].as_int64(), 7);
}

TEST(JsonUtilsTest, ToStringAndPrintConsistency)
{
    boost::json::object obj;
    obj["a"] = 1;
    obj["b"] = "two";
    obj["c"] = true;

    boost::json::value jv(obj);

    std::string from_to_string = JsonUtils::toString(jv);

    std::ostringstream oss;
    JsonUtils::print(oss, jv);
    std::string from_print = oss.str();

    EXPECT_EQ(from_to_string, from_print);
}
