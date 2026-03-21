cmake_minimum_required(VERSION 3.20)
project(QuantumSafeSystem LANGUAGES CXX)

# Global C++20 requirement for jthread and coroutines
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(Threads REQUIRED)

# ── Botan 3 (global imported target) ────────────────────────────────────────
add_library(Botan::Botan STATIC IMPORTED)
set_target_properties(Botan::Botan PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include/botan-3/"
    IMPORTED_LOCATION             "/usr/local/lib/libbotan-3.a"
    INTERFACE_LINK_LIBRARIES      "Threads::Threads;dl"
)

enable_testing()
add_subdirectory(libs/json)
add_subdirectory(libs/db)
add_subdirectory(libs/jwt)
add_subdirectory(server)
add_subdirectory(controller)
add_subdirectory(sensor)
add_subdirectory(proxy)
add_subdirectory(tests/gtests)
