cmake_minimum_required(VERSION 3.15)
project(oro_health_monitoring_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

# Find ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

# Find the installed storage_handoff shared library
find_package(storage_handoff REQUIRED)

# Executable
add_executable(health_monitoring_node
    src/main.cpp
    src/health_monitor.cpp
    src/zmq_subscriber.cpp
)

# Include node-local headers and ZMQ headers
target_include_directories(health_monitoring_node
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(health_monitoring_node
    PRIVATE
    storage_handoff::storage_handoff
    ${ZMQ_LIBRARIES}
)
target_link_directories(health_monitoring_node PRIVATE ${ZMQ_LIBRARY_DIRS})

# Optional: build with test mode for inject_dummy_payload()
# cmake -DORO_HEALTH_TEST=ON ..
option(ORO_HEALTH_TEST "Enable test mode with inject_dummy_payload()" OFF)
if(ORO_HEALTH_TEST)
    target_compile_definitions(health_monitoring_node PRIVATE ORO_HEALTH_TEST)
endif()
