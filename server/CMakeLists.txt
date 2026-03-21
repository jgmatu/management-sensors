cmake_minimum_required(VERSION 3.24)
project(server LANGUAGES CXX)

# -----------------------------------------------------------------------------
# Dependencies (Botan::Botan defined in root CMakeLists.txt)
# -----------------------------------------------------------------------------
set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options json)

# -----------------------------------------------------------------------------
# 3. Build Executable
# -----------------------------------------------------------------------------
add_executable(${PROJECT_NAME}
    src/oscp/ocsp_cache.cpp
    src/dispatcher/Dispatcher.cpp
    src/cli/QuantumCommandCli.cpp
    src/http/QuantumSafeHttp.cpp
    src/net/QuantumSafeTlsEngine.cpp
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
        Botan::Botan
        Boost::program_options
        Boost::json
        Threads::Threads
        db
        json
        jwt
)