cmake_minimum_required(VERSION 3.15)
project(oro_health_monitoring_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find necessary libraries (ZeroMQ)
# We will use this eventually for ZMQ Subscriber
# find_package(ZeroMQ REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Add separate package
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../storage_handoff ${CMAKE_CURRENT_BINARY_DIR}/storage_handoff)


# Executable
add_executable(health_monitoring_node
    src/main.cpp
    src/health_monitor.cpp
    src/zmq_subscriber.cpp
)

# Link directories and libraries
target_link_libraries(health_monitoring_node
    PRIVATE
    storage_handoff
)
