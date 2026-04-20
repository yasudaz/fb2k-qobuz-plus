# Creates foo_qobuz.fb2k-component (a ZIP with the 32-bit DLL at the root
# and the 64-bit DLL under x64/).
#
# Called by the foo_qobuz_component CMake target via:
#   cmake -DFOO_DLL=... -DOUTPUT=... -DARCH_X86=... -DEXTRA_DLL=... -P package_component.cmake
#
# Variables (all passed via -D):
#   FOO_DLL   - path to the current-architecture DLL
#   OUTPUT    - destination path for the .fb2k-component file
#   ARCH_X86  - TRUE if the current build is 32-bit (Win32)
#   EXTRA_DLL - (optional) path to the other-architecture DLL to bundle

cmake_minimum_required(VERSION 3.16)

get_filename_component(_output_dir "${OUTPUT}" DIRECTORY)
set(_staging "${_output_dir}/_fb2k_component_staging")

file(REMOVE_RECURSE "${_staging}")
file(MAKE_DIRECTORY "${_staging}/x64")

# Place each DLL in the correct location:
#   32-bit DLL -> root (foo_qobuz.dll)
#   64-bit DLL -> x64/ (x64/foo_qobuz.dll)
if(ARCH_X86)
    file(COPY "${FOO_DLL}" DESTINATION "${_staging}")
    if(EXTRA_DLL AND EXISTS "${EXTRA_DLL}")
        file(COPY "${EXTRA_DLL}" DESTINATION "${_staging}/x64")
    endif()
else()
    file(COPY "${FOO_DLL}" DESTINATION "${_staging}/x64")
    if(EXTRA_DLL AND EXISTS "${EXTRA_DLL}")
        file(COPY "${EXTRA_DLL}" DESTINATION "${_staging}")
    endif()
endif()

# Build the list of files to include (relative paths for clean zip entries).
set(_archive_files)
if(EXISTS "${_staging}/foo_qobuz.dll")
    list(APPEND _archive_files "foo_qobuz.dll")
endif()
if(EXISTS "${_staging}/x64/foo_qobuz.dll")
    list(APPEND _archive_files "x64/foo_qobuz.dll")
endif()

if(NOT _archive_files)
    message(FATAL_ERROR "No DLLs found to package in staging directory ${_staging}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT}" --format=zip -- ${_archive_files}
    WORKING_DIRECTORY "${_staging}"
    RESULT_VARIABLE _result
)

file(REMOVE_RECURSE "${_staging}")

if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Failed to create .fb2k-component archive (cmake -E tar exited ${_result})")
endif()

message(STATUS "Created: ${OUTPUT}")
