cmake_minimum_required(VERSION 3.16)
project(environment_condition_node)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(storage_handoff REQUIRED)
find_package(nlohmann_json REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
pkg_check_modules(OPENCV REQUIRED opencv4)
find_package(Threads REQUIRED)

include_directories(
    include
    ../storage_handoff/include
    ${ZMQ_INCLUDE_DIRS}
    ${OPENCV_INCLUDE_DIRS}
)

add_executable(environment_condition_node
    src/main.cpp
    src/environment_condition_monitor.cpp
    src/zmq_subscriber.cpp
    src/ambient_light_estimator.cpp
)

target_link_libraries(environment_condition_node
    PRIVATE
    storage_handoff::storage_handoff
    nlohmann_json::nlohmann_json
    ${ZMQ_LIBRARIES}
    ${OPENCV_LIBRARIES}
    Threads::Threads
)
target_link_directories(environment_condition_node PRIVATE ${ZMQ_LIBRARY_DIRS} ${OPENCV_LIBRARY_DIRS})

install(TARGETS environment_condition_node DESTINATION bin)
