# --- libpq (The C Shared Driver) ---
add_library(PostgreSQL::libpq SHARED IMPORTED) # Changed to SHARED
set_target_properties(PostgreSQL::libpq PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/pgsql/include"
    # Using the .so file for dynamic linking
    IMPORTED_LOCATION             "/usr/local/pgsql/lib/libpq.a"
)

# --- libpqxx (The C++ Shared Interface) ---
add_library(pqxx::pqxx STATIC IMPORTED) # Changed to STATIC
set_target_properties(pqxx::pqxx PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include"
    IMPORTED_LOCATION             "/usr/local/lib/libpqxx.a"
)

# --- Internal Postgres Support (Shared versions) ---
# Note: Usually libpgcommon and libpgport are only static in many installations.
# If they don't exist as .so, libpq.so usually already includes what it needs.
add_library(PostgreSQL::libpgcommon STATIC IMPORTED)
set_target_properties(PostgreSQL::libpgcommon PROPERTIES
    IMPORTED_LOCATION "/usr/local/pgsql/lib/libpgcommon.a"
)

add_library(PostgreSQL::libpgport STATIC IMPORTED)
set_target_properties(PostgreSQL::libpgport PROPERTIES
    IMPORTED_LOCATION "/usr/local/pgsql/lib/libpgport.a"
)

find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

# 1. Define the Shared Library
# We only include DatabaseManager here as requested.
add_library(shared_db STATIC 
    DatabaseManager.cpp
)

# 2. Configure Header Visibility
# PUBLIC: 'common/' is the interface. Consumers inherit this path.
# PRIVATE: 'src/' is only for compiling the .cpp file.
target_include_directories(shared_db 
    PUBLIC  ${CMAKE_SOURCE_DIR}/common
)

# 3. Link Database Dependencies
# PUBLIC ensures anyone linking to this library also gets pqxx.
target_link_libraries(shared_db 
    PUBLIC 
        pqxx::pqxx             # C++ Wrapper

        # --- ALIAS DE SÍMBOLOS (Soluciona el cambio de nombre en Postgres 17/18) ---
        -Wl,--defsym=pg_encoding_to_char=pg_encoding_to_char_private
        -Wl,--defsym=pg_char_to_encoding=pg_char_to_encoding_private

        PostgreSQL::libpq
        PostgreSQL::libpgcommon
        PostgreSQL::libpgport
        Boost::json
)

# 4. Set Shared Object Version (Good practice for RHEL 10)
set_target_properties(shared_db PROPERTIES
    VERSION 1.0.0
    SOVERSION 1
)