cmake_minimum_required(VERSION 3.20)
project(CPPBackend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable compile warnings for safer code
if (MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE CORE_TP_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Core/*.cpp"
)

# Optionally collect headers for IDE visibility (not required to build)
file(GLOB_RECURSE CORE_TP_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Core/*.h"
)

# vcpkg manifest mode provides config packages
find_package(Drogon CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

add_library(core_tp STATIC
        Core/Utilities.h
        Core/Utilities.cpp)
target_sources(core_tp PRIVATE ${CORE_TP_SOURCES} ${CORE_TP_HEADERS})

target_include_directories(core_tp PUBLIC
        ${CMAKE_SOURCE_DIR} # project includes
)


target_link_libraries(core_tp
  PUBLIC
    Drogon::Drogon
    libpqxx::pqxx

)

add_executable(trading_platform
        main.cpp
)

target_link_libraries(trading_platform PRIVATE core_tp)

# On some platforms pqxx requires pthread explicitly
if (UNIX AND NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(trading_platform PRIVATE Threads::Threads)
endif()
