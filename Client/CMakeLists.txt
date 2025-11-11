cmake_minimum_required(VERSION 3.16)
project(TradingPlatformClient CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon CONFIG REQUIRED)

add_executable(TradingPlatformClient
        main.cpp
)

target_link_libraries(TradingPlatformClient
        PRIVATE Drogon::Drogon
)