cmake_minimum_required(VERSION 3.10)
project(SensorNode_Project VERSION 1.0)

# 1. Set C++20 Standard for 100 CPU high-perf sensors
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific extensions (e.g., use -std=c++20 instead of -std=gnu++20)
set(CMAKE_CXX_EXTENSIONS OFF)

# 2. Find Paho MQTT C++ (Required)
# On RHEL 10, ensure paho-mqtt-cpp-devel is installed
find_package(PahoMqttCpp REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
# Ensure your Boost installation path is in CMAKE_PREFIX_PATH if not in /usr
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

# 3. Define the executable
add_executable(sensor src/main.cpp)

# 4. Include directories (if you have headers in an 'include' folder)
target_include_directories(sensor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 5. Link the Paho MQTT C++ library
# This automatically handles the underlying Paho C and OpenSSL dependencies
target_link_libraries(sensor 
    PRIVATE 
        PahoMqttCpp::paho-mqttpp3
        Boost::json
)

# 6. Install rule (Optional)
install(TARGETS sensor DESTINATION bin)