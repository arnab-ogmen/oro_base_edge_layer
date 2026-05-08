#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "storage_handoff::storage_handoff" for configuration "Release"
set_property(TARGET storage_handoff::storage_handoff APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(storage_handoff::storage_handoff PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libstorage_handoff.so.1.0.0"
  IMPORTED_SONAME_RELEASE "libstorage_handoff.so.1"
  )

list(APPEND _cmake_import_check_targets storage_handoff::storage_handoff )
list(APPEND _cmake_import_check_files_for_storage_handoff::storage_handoff "${_IMPORT_PREFIX}/lib/libstorage_handoff.so.1.0.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
