cmake_minimum_required(VERSION 3.20)

project(proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Botan 3 (reuse the same imported target definition as in server/)
add_library(Botan::Botan STATIC IMPORTED)
set_target_properties(Botan::Botan PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "/usr/local/include/botan-3/"
    IMPORTED_LOCATION             "/usr/local/lib/libbotan-3.a"
    INTERFACE_LINK_LIBRARIES      "Threads::Threads;dl"
)

find_package(Threads REQUIRED)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options)

add_executable(proxy
    src/main.cpp
)

target_link_libraries(proxy
    PRIVATE
        Botan::Botan
        Boost::program_options
        Threads::Threads
)