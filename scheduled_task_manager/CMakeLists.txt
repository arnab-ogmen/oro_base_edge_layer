cmake_minimum_required(VERSION 3.15)
project(scheduled_task_manager_node VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(storage_handoff REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ZMQ_INCLUDE_DIRS}
)

add_executable(scheduled_task_manager_node
    src/main.cpp
    src/scheduler_config.cpp
    src/scheduler_engine.cpp
    src/job_executor.cpp
    src/job_registry.cpp
    src/jobs/care_jobs.cpp
    src/jobs/device_jobs.cpp
    src/jobs/health_jobs.cpp
    src/jobs/summary_jobs.cpp
    src/jobs/retry_jobs.cpp
    src/jobs/cleanup_jobs.cpp
)

target_link_libraries(scheduled_task_manager_node
    PRIVATE
    Threads::Threads
    ${ZMQ_LIBRARIES}
    storage_handoff::storage_handoff
)

target_link_directories(scheduled_task_manager_node PRIVATE ${ZMQ_LIBRARY_DIRS})

install(TARGETS scheduled_task_manager_node
    RUNTIME DESTINATION bin
)
