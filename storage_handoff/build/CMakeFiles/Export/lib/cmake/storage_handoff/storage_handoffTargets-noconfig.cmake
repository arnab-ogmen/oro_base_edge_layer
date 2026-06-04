#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "storage_handoff::storage_handoff" for configuration ""
set_property(TARGET storage_handoff::storage_handoff APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(storage_handoff::storage_handoff PROPERTIES
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libstorage_handoff.so.1.0.0"
  IMPORTED_SONAME_NOCONFIG "libstorage_handoff.so.1"
  )

list(APPEND _IMPORT_CHECK_TARGETS storage_handoff::storage_handoff )
list(APPEND _IMPORT_CHECK_FILES_FOR_storage_handoff::storage_handoff "${_IMPORT_PREFIX}/lib/libstorage_handoff.so.1.0.0" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
