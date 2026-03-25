# FetchMSYS2Package.cmake
# CMake module to download and extract MSYS2 packages for cross-compilation
#
# Usage:
#   fetch_msys2_package(
#     PACKAGE_NAME "mingw-w64-ucrt-x86_64-flint"
#     VERSION "3.4.0-1"
#     SHA256 "84a0b82609433b6829fdaf7543de234d7d7a0c074087f677b47494de45ac307d"
#     DESTINATION "${CMAKE_BINARY_DIR}/third_party"
#   )
#
# After downloading, the following variables are set:
#   <PACKAGE_NAME>_SOURCE_DIR    - Root directory of extracted package
#   <PACKAGE_NAME>_INCLUDE_DIR   - Include directory (ucrt64/include)
#   <PACKAGE_NAME>_LIB_DIR       - Library directory (ucrt64/lib)
#   <PACKAGE_NAME>_BIN_DIR       - Binary/DLL directory (ucrt64/bin)
#   <PACKAGE_NAME>_DLLS          - List of DLL files in BIN_DIR

include(FetchContent)

function(fetch_msys2_package)
  set(options "")
  set(oneValueArgs PACKAGE_NAME VERSION SHA256 DESTINATION)
  set(multiValueArgs "")
  cmake_parse_arguments(FETCH_MSYS2 "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT FETCH_MSYS2_PACKAGE_NAME)
    message(FATAL_ERROR "PACKAGE_NAME is required for fetch_msys2_package")
  endif()
  if(NOT FETCH_MSYS2_VERSION)
    message(FATAL_ERROR "VERSION is required for fetch_msys2_package")
  endif()
  if(NOT FETCH_MSYS2_SHA256)
    message(FATAL_ERROR "SHA256 is required for fetch_msys2_package")
  endif()
  if(NOT FETCH_MSYS2_DESTINATION)
    set(FETCH_MSYS2_DESTINATION "${CMAKE_BINARY_DIR}/third_party")
  endif()

  set(PACKAGE_URL "https://mirror.msys2.org/mingw/ucrt64/${FETCH_MSYS2_PACKAGE_NAME}-${FETCH_MSYS2_VERSION}-any.pkg.tar.zst")
  set(PACKAGE_DOWNLOAD_DIR "${FETCH_MSYS2_DESTINATION}/downloads")
  set(PACKAGE_EXTRACT_DIR "${FETCH_MSYS2_DESTINATION}/${FETCH_MSYS2_PACKAGE_NAME}")

  # Create directories
  file(MAKE_DIRECTORY "${PACKAGE_DOWNLOAD_DIR}")
  file(MAKE_DIRECTORY "${PACKAGE_EXTRACT_DIR}")

  # Check if already extracted
  if(NOT EXISTS "${PACKAGE_EXTRACT_DIR}/.extracted")
    message(STATUS "Downloading MSYS2 package: ${FETCH_MSYS2_PACKAGE_NAME} ${FETCH_MSYS2_VERSION}")
    
    # Just use CMake's built-in file(DOWNLOAD) - it's more reliable in CMake context
    file(DOWNLOAD
      "${PACKAGE_URL}"
      "${PACKAGE_DOWNLOAD_DIR}/${FETCH_MSYS2_PACKAGE_NAME}-${FETCH_MSYS2_VERSION}-any.pkg.tar.zst"
      EXPECTED_HASH SHA256=${FETCH_MSYS2_SHA256}
      SHOW_PROGRESS
      STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_STATUS_CODE)
    list(GET DOWNLOAD_STATUS 1 DOWNLOAD_STATUS_MESSAGE)
    if(NOT DOWNLOAD_STATUS_CODE EQUAL 0)
      message(FATAL_ERROR "Failed to download ${FETCH_MSYS2_PACKAGE_NAME}: ${DOWNLOAD_STATUS_MESSAGE}")
    endif()

    message(STATUS "Extracting MSYS2 package: ${FETCH_MSYS2_PACKAGE_NAME}")
    
    # Try to extract with tar --zstd first
    execute_process(
      COMMAND tar --zstd -xf "${PACKAGE_DOWNLOAD_DIR}/${FETCH_MSYS2_PACKAGE_NAME}-${FETCH_MSYS2_VERSION}-any.pkg.tar.zst"
      WORKING_DIRECTORY "${PACKAGE_EXTRACT_DIR}"
      RESULT_VARIABLE TAR_RESULT
      ERROR_QUIET
      OUTPUT_QUIET
    )
    
    # If tar --zstd fails, try unzstd first then tar
    if(NOT TAR_RESULT EQUAL 0)
      message(STATUS "tar --zstd failed, trying unzstd + tar...")
      execute_process(
        COMMAND unzstd -q -o - "${PACKAGE_DOWNLOAD_DIR}/${FETCH_MSYS2_PACKAGE_NAME}-${FETCH_MSYS2_VERSION}-any.pkg.tar.zst"
        COMMAND tar -xf -
        WORKING_DIRECTORY "${PACKAGE_EXTRACT_DIR}"
        RESULT_VARIABLE TAR_RESULT2
      )
      if(NOT TAR_RESULT2 EQUAL 0)
        message(FATAL_ERROR "Failed to extract ${FETCH_MSYS2_PACKAGE_NAME} (tried tar --zstd and unzstd+tar)")
      endif()
    endif()

    # Mark as extracted
    file(WRITE "${PACKAGE_EXTRACT_DIR}/.extracted" "")
    message(STATUS "Successfully fetched ${FETCH_MSYS2_PACKAGE_NAME}")
  else()
    message(STATUS "MSYS2 package ${FETCH_MSYS2_PACKAGE_NAME} already fetched")
  endif()

  # Set output variables
  set(${FETCH_MSYS2_PACKAGE_NAME}_SOURCE_DIR "${PACKAGE_EXTRACT_DIR}" PARENT_SCOPE)
  set(${FETCH_MSYS2_PACKAGE_NAME}_INCLUDE_DIR "${PACKAGE_EXTRACT_DIR}/ucrt64/include" PARENT_SCOPE)
  set(${FETCH_MSYS2_PACKAGE_NAME}_LIB_DIR "${PACKAGE_EXTRACT_DIR}/ucrt64/lib" PARENT_SCOPE)
  set(${FETCH_MSYS2_PACKAGE_NAME}_BIN_DIR "${PACKAGE_EXTRACT_DIR}/ucrt64/bin" PARENT_SCOPE)
  
  # Collect all DLL files
  file(GLOB_RECURSE _dll_files "${PACKAGE_EXTRACT_DIR}/ucrt64/bin/*.dll")
  set(${FETCH_MSYS2_PACKAGE_NAME}_DLLS "${_dll_files}" PARENT_SCOPE)
endfunction()

# Helper function: collect DLLs from multiple packages and copy to target directory
function(collect_and_copy_dlls TARGET_DIR)
  set(_all_dlls "")
  foreach(_pkg IN LISTS ARGN)
    if(DEFINED ${_pkg}_DLLS)
      list(APPEND _all_dlls ${${_pkg}_DLLS})
    endif()
  endforeach()
  
  if(_all_dlls)
    file(MAKE_DIRECTORY "${TARGET_DIR}")
    file(COPY ${_all_dlls} DESTINATION "${TARGET_DIR}" FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
    list(LENGTH _all_dlls _dll_count)
    message(STATUS "Copied ${_dll_count} DLL(s) to ${TARGET_DIR}")
  endif()
endfunction()
