cmake_minimum_required(VERSION 3.20)
project(DispatcherTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)
find_package(GTest REQUIRED)

add_executable(dispatcher_tests
    src/main.cpp
    src/dispatcher/DispatcherTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/dispatcher/Dispatcher.cpp
)

target_include_directories(dispatcher_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(dispatcher_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
)

add_executable(db_tests
    src/main.cpp
    src/db/DatabaseManagerTest.cpp
)

target_link_libraries(db_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
        db
)

target_include_directories(db_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(mock_db_tests
    src/main.cpp
    src/db/MockDatabaseTest.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src/dispatcher/Dispatcher.cpp
)

target_include_directories(mock_db_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../server/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(mock_db_tests
    PRIVATE
        GTest::gtest_main
        GTest::gmock
        Threads::Threads
        db
)

add_executable(json_tests
    src/main.cpp
    src/json/JsonUtilsTest.cpp
)

target_include_directories(json_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(json_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
        json
)

add_executable(jwt_tests
    src/main.cpp
    src/jwt/JwtManagerTest.cpp
)

target_include_directories(jwt_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(jwt_tests
    PRIVATE
        TEST_CERTS_DIR="${CMAKE_SOURCE_DIR}/server/certs"
)

target_link_libraries(jwt_tests
    PRIVATE
        GTest::gtest_main
        Threads::Threads
        jwt
)

enable_testing()
add_test(NAME dispatcher_tests COMMAND dispatcher_tests)
add_test(NAME db_tests COMMAND db_tests)
add_test(NAME mock_db_tests COMMAND mock_db_tests)
add_test(NAME json_tests COMMAND json_tests)
add_test(NAME jwt_tests COMMAND jwt_tests)
gtest_discover_tests(dispatcher_tests)
gtest_discover_tests(db_tests)
gtest_discover_tests(mock_db_tests)
gtest_discover_tests(json_tests)
gtest_discover_tests(jwt_tests)