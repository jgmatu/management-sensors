cmake_minimum_required(VERSION 3.10)
project(DispatcherTest)

# Use C++17 for atomic and thread support
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find threading library (required for mutex/CV)
find_package(Threads REQUIRED)

# Define the test executable
add_executable(dispatcher_test
    src/main.cpp
)

# Tell CMake where to find Dispatcher.hpp
target_include_directories(dispatcher_test PRIVATE src/dispatcher)

# Link the threading library to the executable
target_link_libraries(dispatcher_test PRIVATE Threads::Threads)