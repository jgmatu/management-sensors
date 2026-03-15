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
    INTERFACE_LINK_LIBRARIES      "PahoMqttCpp::paho-mqtt3as;OpenSSL::SSL;OpenSSL::Crypto;Threads::Threads"
)

# --- libpq (The C Driver required by pqxx) ---
# Note: pqxx needs the base C library to function
add_library(PostgreSQL::libpq STATIC IMPORTED)
set_target_properties(PostgreSQL::libpq PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/pgsql/include"
    IMPORTED_LOCATION             "/usr/local/pgsql/lib/libpq.a"
)

# --- libpqxx (The C++ Interface you just installed) ---
add_library(pqxx::pqxx STATIC IMPORTED)
set_target_properties(pqxx::pqxx PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    IMPORTED_LOCATION             "/usr/local/lib/libpqxx.a"
)

# --- libpgcommon (Soporte interno de Postgres) ---
add_library(PostgreSQL::libpgcommon STATIC IMPORTED)
set_target_properties(PostgreSQL::libpgcommon PROPERTIES
    IMPORTED_LOCATION "/usr/local/pgsql/lib/libpgcommon.a"
)

# --- libpgport (Soporte de portabilidad de Postgres) ---
add_library(PostgreSQL::libpgport STATIC IMPORTED)
set_target_properties(PostgreSQL::libpgport PROPERTIES
    IMPORTED_LOCATION "/usr/local/pgsql/lib/libpgport.a"
)

# --- OpenSSL (Manual en /usr/local/lib64) ---
add_library(OpenSSL::SSL STATIC IMPORTED)
set_target_properties(OpenSSL::SSL PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    IMPORTED_LOCATION             "/usr/local/lib64/libssl.a"
)

add_library(OpenSSL::Crypto STATIC IMPORTED)
set_target_properties(OpenSSL::Crypto PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    IMPORTED_LOCATION             "/usr/local/lib64/libcrypto.a"
)

# 1. Find the Mosquitto library on your system
find_library(MOSQUITTO_LIB mosquitto)

set(Boost_USE_STATIC_LIBS ON)
# Ensure your Boost installation path is in CMAKE_PREFIX_PATH if not in /usr
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

find_package(Threads REQUIRED)

# 2. Define the executable and its source files
add_executable(broker 
    src/main.cpp
    src/db/DatabaseManager.cpp
)

# 3. Include directories
# 'include' is for public headers, 'src' for private ones
target_include_directories(broker
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# 4. Link Libraries
# - PahoMqttCpp::paho-mqttpp3: The MQTT library
target_link_libraries(broker 
    PRIVATE 
        # 1. High-level C++ wrappers first
        pqxx::pqxx                 
        PahoMqttCpp::paho-mqttpp3

        # --- ALIAS DE SÍMBOLOS (Soluciona el cambio de nombre en Postgres 17/18) ---
        -Wl,--defsym=pg_encoding_to_char=pg_encoding_to_char_private
        -Wl,--defsym=pg_char_to_encoding=pg_char_to_encoding_private

        # 2. Base C libraries that the wrappers depend on
        PostgreSQL::libpq
        PahoMqttCpp::paho-mqtt3as

        # 3. System and helper libraries (Postgres internals)
        PostgreSQL::libpgcommon
        PostgreSQL::libpgport
        Threads::Threads

        Boost::json
)