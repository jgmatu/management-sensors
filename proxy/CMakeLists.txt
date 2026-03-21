cmake_minimum_required(VERSION 3.20)

project(proxy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Botan::Botan defined in root CMakeLists.txt

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS program_options)

add_executable(proxy
    src/main.cpp
)

target_include_directories(proxy
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
)

target_link_libraries(proxy
    PRIVATE
        Botan::Botan
        Boost::program_options
        Threads::Threads
)