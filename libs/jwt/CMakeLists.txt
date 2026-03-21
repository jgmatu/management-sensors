find_package(Boost 1.90.0 CONFIG REQUIRED COMPONENTS json)

add_library(jwt STATIC
    JwtManager.cpp
)

target_include_directories(jwt
    PUBLIC ${CMAKE_SOURCE_DIR}/common
)

target_link_libraries(jwt
    PUBLIC
        Botan::Botan
        Boost::json
)
