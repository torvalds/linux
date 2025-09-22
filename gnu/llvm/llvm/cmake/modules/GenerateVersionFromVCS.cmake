# CMake script that writes version control information to a header.
#
# Input variables:
#   NAMES             - A list of names for each of the source directories.
#   <NAME>_SOURCE_DIR - A path to source directory for each name in NAMES.
#   HEADER_FILE       - The header file to write
#
# The output header will contain macros <NAME>_REPOSITORY and <NAME>_REVISION,
# where "<NAME>" is substituted with the names specified in the input variables,
# for each of the <NAME>_SOURCE_DIR given.

get_filename_component(LLVM_CMAKE_DIR "${CMAKE_SCRIPT_MODE_FILE}" PATH)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

include(VersionFromVCS)

# Handle strange terminals
set(ENV{TERM} "dumb")

function(append_info name revision repository)
  if(revision)
    file(APPEND "${HEADER_FILE}.tmp"
      "#define ${name}_REVISION \"${revision}\"\n")
  else()
    file(APPEND "${HEADER_FILE}.tmp"
      "#undef ${name}_REVISION\n")
  endif()
  if(repository)
    file(APPEND "${HEADER_FILE}.tmp"
      "#define ${name}_REPOSITORY \"${repository}\"\n")
  else()
    file(APPEND "${HEADER_FILE}.tmp"
      "#undef ${name}_REPOSITORY\n")
  endif()
endfunction()

foreach(name IN LISTS NAMES)
  if(LLVM_FORCE_VC_REVISION AND LLVM_FORCE_VC_REPOSITORY)
    set(revision ${LLVM_FORCE_VC_REVISION})
    set(repository ${LLVM_FORCE_VC_REPOSITORY})
  elseif(LLVM_FORCE_VC_REVISION)
    set(revision ${LLVM_FORCE_VC_REVISION})
  elseif(LLVM_FORCE_VC_REPOSITORY)
    set(repository ${LLVM_FORCE_VC_REPOSITORY})
  elseif(${name}_VC_REPOSITORY AND ${name}_VC_REVISION)
    set(revision ${${name}_VC_REVISION})
    set(repository ${${name}_VC_REPOSITORY})
  elseif(DEFINED ${name}_SOURCE_DIR)
    if (${name}_SOURCE_DIR)
      get_source_info("${${name}_SOURCE_DIR}" revision repository)
    endif()
  else()
    message(FATAL_ERROR "${name}_SOURCE_DIR is not defined")
  endif()
  append_info(${name} "${revision}" "${repository}")
endforeach()

# Copy the file only if it has changed.
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different
  "${HEADER_FILE}.tmp" "${HEADER_FILE}")
file(REMOVE "${HEADER_FILE}.tmp")
