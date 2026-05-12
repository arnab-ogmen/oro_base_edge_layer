cmake_minimum_required(VERSION 3.15)
project(command_executor_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(storage_handoff REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../oro_base_input_layer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../oro_base_hardware_layer/radxa_drivers/include
    ${ZMQ_INCLUDE_DIRS}
)

add_executable(command_executor_node
    src/main.cpp
    src/command_dispatcher.cpp
    src/command_executor.cpp
    src/command_handlers.cpp
    src/signal_logger.cpp
    src/status_subscriber.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../oro_base_hardware_layer/radxa_drivers/src/radxa_services.cpp
)

target_link_libraries(command_executor_node
    PRIVATE
    Threads::Threads
    ${ZMQ_LIBRARIES}
    storage_handoff::storage_handoff
)

target_link_directories(command_executor_node PRIVATE ${ZMQ_LIBRARY_DIRS})

install(TARGETS command_executor_node
    RUNTIME DESTINATION bin
)
