cmake_minimum_required(VERSION 3.20)
project(DispatcherTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_executable(dispatcher_tests
    src/main.cpp
    src/dispatcher/DispatcherTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../server/src/dispatcher/Dispatcher.cpp
)

target_include_directories(dispatcher_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../server/src
)

target_link_libraries(dispatcher_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
)

enable_testing()

add_test(
    NAME dispatcher_tests
    COMMAND dispatcher_tests
)