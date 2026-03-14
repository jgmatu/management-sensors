cmake_minimum_required(VERSION 3.24)
project(testserver LANGUAGES CXX)

# -----------------------------------------------------------------------------
# 1. Manual Library Imports (Custom Installations)
# -----------------------------------------------------------------------------

# --- Botan 3 ---
add_library(Botan::Botan STATIC IMPORTED)
set_target_properties(Botan::Botan PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include/botan-3/"
    IMPORTED_LOCATION             "/usr/local/lib/libbotan-3.a"
    INTERFACE_LINK_LIBRARIES      "Threads::Threads;dl"
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

# -----------------------------------------------------------------------------
# 2. Dependencies
# -----------------------------------------------------------------------------
find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
# Ensure your Boost installation path is in CMAKE_PREFIX_PATH if not in /usr
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

# -----------------------------------------------------------------------------
# 3. Build Executable
# -----------------------------------------------------------------------------
add_executable(${PROJECT_NAME}
    src/oscp/ocsp_cache.cpp
    src/db/DatabaseManager.cpp
    src/json/JsonUtils.cpp
    src/main.cpp
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD            20
    CXX_STANDARD_REQUIRED   ON
    EXPORT_COMPILE_COMMANDS ON
)

# --- ADDED: Public Include Folder ---
target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include" # Your new public include folder
        "${CMAKE_CURRENT_SOURCE_DIR}/src"     # For internal headers
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        pqxx::pqxx             # C++ Wrapper

        # --- ALIAS DE SÍMBOLOS (Soluciona el cambio de nombre en Postgres 17/18) ---
        -Wl,--defsym=pg_encoding_to_char=pg_encoding_to_char_private
        -Wl,--defsym=pg_char_to_encoding=pg_char_to_encoding_private

        PostgreSQL::libpq
        PostgreSQL::libpgcommon
        PostgreSQL::libpgport
        Botan::Botan
        Boost::program_options
        Boost::json
        Threads::Threads

        # --- PROVEEDORES DE SÍMBOLOS (Al final) ---
        OpenSSL::SSL            # Resuelve símbolos de libpq y libpgcommon
        OpenSSL::Crypto         # Resuelve símbolos de SSL y libpgport
)