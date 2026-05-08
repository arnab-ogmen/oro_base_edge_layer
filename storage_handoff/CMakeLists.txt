cmake_minimum_required(VERSION 3.15)
project(storage_handoff VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ── Build as a SHARED library ─────────────────────────────────────────────────
add_library(storage_handoff SHARED
    src/storage_handoff.cpp
)

# Namespaced alias so consumers can reference storage_handoff::storage_handoff
# whether they use add_subdirectory or find_package.
add_library(storage_handoff::storage_handoff ALIAS storage_handoff)

target_include_directories(storage_handoff PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(storage_handoff PUBLIC pqxx pq)

# Set shared library versioning
set_target_properties(storage_handoff PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ── Install: library binary ───────────────────────────────────────────────────
install(TARGETS storage_handoff
    EXPORT  storage_handoffTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ── Install: public headers ───────────────────────────────────────────────────
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ── Install: CMake export targets ─────────────────────────────────────────────
install(EXPORT storage_handoffTargets
    FILE      storage_handoffTargets.cmake
    NAMESPACE storage_handoff::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/storage_handoff
)

# ── Generate and install: Config + Version files ──────────────────────────────
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/storage_handoffConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/storage_handoffConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/storage_handoff
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/storage_handoffConfigVersion.cmake
    VERSION     ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/storage_handoffConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/storage_handoffConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/storage_handoff
)
