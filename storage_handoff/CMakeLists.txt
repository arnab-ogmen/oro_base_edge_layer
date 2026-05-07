cmake_minimum_required(VERSION 3.15)
project(storage_handoff VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(storage_handoff
    src/storage_handoff.cpp
)

target_include_directories(storage_handoff PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(storage_handoff PUBLIC pqxx pq)
