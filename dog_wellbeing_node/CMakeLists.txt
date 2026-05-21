cmake_minimum_required(VERSION 3.16)
project(dog_wellbeing_node)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(storage_handoff REQUIRED)
find_package(nlohmann_json REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
find_package(Threads REQUIRED)

include_directories(
    include
    ../storage_handoff/include
    ${ZMQ_INCLUDE_DIRS}
)

add_executable(dog_wellbeing_node
    src/main.cpp
    src/dog_wellbeing_monitor.cpp
    src/zmq_subscriber.cpp
)

target_link_libraries(dog_wellbeing_node
    PRIVATE
    storage_handoff::storage_handoff
    nlohmann_json::nlohmann_json
    ${ZMQ_LIBRARIES}
    Threads::Threads
)
target_link_directories(dog_wellbeing_node PRIVATE ${ZMQ_LIBRARY_DIRS})

install(TARGETS dog_wellbeing_node DESTINATION bin)
