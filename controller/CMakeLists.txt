cmake_minimum_required(VERSION 3.10)
project(ProcessA_MQTT_Hello VERSION 1.0)

# Set C++20 as the standard
set(CMAKE_CXX_STANDARD 20)

# Ensure the compiler actually supports it (fails early if not)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable compiler-specific extensions (e.g., use -std=c++20 instead of -std=gnu++20)
set(CMAKE_CXX_EXTENSIONS OFF)

# 1. Find the Paho MQTT C++ package
# On RHEL 10, this looks for PahoMqttCppConfig.cmake

# --- Paho MQTT C (Base Library) ---
# Note: Check if your file is in /usr/local/lib or /usr/local/lib64
add_library(PahoMqttCpp::paho-mqtt3as SHARED IMPORTED)
set_target_properties(PahoMqttCpp::paho-mqtt3as PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    IMPORTED_LOCATION             "/usr/local/lib/libpaho-mqtt3as.so"
)

# --- Paho MQTT C++ (The Wrapper) ---
add_library(PahoMqttCpp::paho-mqttpp3 SHARED IMPORTED)
set_target_properties(PahoMqttCpp::paho-mqttpp3 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    # Note: Use the base name .so, Linux will handle the versioning
    IMPORTED_LOCATION             "/usr/local/lib/libpaho-mqttpp3.so"
    INTERFACE_LINK_LIBRARIES      "PahoMqttCpp::paho-mqtt3as;Threads::Threads"
)

# 1. Find the Mosquitto library on your system
find_library(MOSQUITTO_LIB mosquitto)

set(Boost_USE_STATIC_LIBS ON)
# Ensure your Boost installation path is in CMAKE_PREFIX_PATH if not in /usr
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

find_package(Threads REQUIRED)

# 2. Define the executable and its source files
add_executable(controller 
    src/main.cpp
)

# 3. Include directories
# 'include' is for public headers, 'src' for private ones
target_include_directories(controller
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 4. Link Libraries
# - PahoMqttCpp::paho-mqttpp3: The MQTT library
target_link_libraries(controller 
    PRIVATE 
        PahoMqttCpp::paho-mqttpp3
        Threads::Threads
        Boost::json
        shared_db
        shared_json
)