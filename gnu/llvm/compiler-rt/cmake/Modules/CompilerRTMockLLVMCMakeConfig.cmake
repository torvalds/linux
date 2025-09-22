# This macro mocks enough of the changes `LLVMConfig.cmake` makes so that
# compiler-rt can successfully configure itself when a LLVM toolchain is
# available but the corresponding CMake build files are not.
#
# The motivation for this is to be able to generate the compiler-rt
# lit tests suites and run them against an arbitrary LLVM toolchain
# which doesn't ship the LLVM CMake build files.
macro(compiler_rt_mock_llvm_cmake_config)
  message(STATUS "Attempting to mock the changes made by LLVMConfig.cmake")
  compiler_rt_mock_llvm_cmake_config_set_cmake_path()
  compiler_rt_mock_llvm_cmake_config_set_target_triple()
  compiler_rt_mock_llvm_cmake_config_include_cmake_files()
endmacro()

macro(compiler_rt_mock_llvm_cmake_config_set_cmake_path)
  # Point `LLVM_CMAKE_DIR` at the source tree in the monorepo.
  set(LLVM_CMAKE_DIR "${LLVM_MAIN_SRC_DIR}/cmake/modules")
  if (NOT EXISTS "${LLVM_CMAKE_DIR}")
    message(FATAL_ERROR "LLVM_CMAKE_DIR (${LLVM_CMAKE_DIR}) does not exist")
  endif()
  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
  message(STATUS "LLVM_CMAKE_DIR: \"${LLVM_CMAKE_DIR}\"")
endmacro()

function(compiler_rt_mock_llvm_cmake_config_set_target_triple)
  # Various bits of compiler-rt depend on the `LLVM_TARGET_TRIPLE` variable
  # being defined. This function tries to set a sensible value for the
  # variable. This is a function rather than a macro to avoid polluting the
  # variable namespace.
  set(COMPILER_OUTPUT "")

  # If the user provides `COMPILER_RT_DEFAULT_TARGET_ONLY` and `CMAKE_C_COMPILER_TARGET`
  # (see `construct_compiler_rt_default_triple`) then prefer that to examining the
  # compiler.
  if (COMPILER_RT_DEFAULT_TARGET_ONLY)
    if (NOT "${CMAKE_C_COMPILER_TARGET}" STREQUAL "")
      message(STATUS
        "Using CMAKE_C_COMPILER_TARGET (${CMAKE_C_COMPILER_TARGET}) as LLVM_TARGET_TRIPLE")
    endif()
    set(COMPILER_OUTPUT "${CMAKE_C_COMPILER_TARGET}")
  endif()

  # Try asking the compiler for its default target triple.
  set(HAD_ERROR FALSE)
  if ("${COMPILER_OUTPUT}" STREQUAL "")
    if ("${CMAKE_C_COMPILER_ID}" MATCHES "Clang|GNU")
      # Note: Clang also supports `-print-target-triple` but gcc doesn't
      # support this flag.
      set(DUMPMACHINE_ARG -dumpmachine)
      if(MSVC)
        # Use /clang:-dumpmachine for clang-cl.
        set(DUMPMACHINE_ARG /clang:-dumpmachine)
      endif()
      execute_process(
        COMMAND "${CMAKE_C_COMPILER}" ${DUMPMACHINE_ARG}
        RESULT_VARIABLE HAD_ERROR
        OUTPUT_VARIABLE COMPILER_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    else()
      message(FATAL_ERROR
        "Fetching target triple from compiler \"${CMAKE_C_COMPILER_ID}\" "
        "is not implemented.")
    endif()
  endif()

  if (HAD_ERROR)
    message(FATAL_ERROR "Fetching target triple from compiler failed")
  endif()
  set(LLVM_TARGET_TRIPLE "${COMPILER_OUTPUT}")
  message(STATUS "TARGET_TRIPLE: \"${LLVM_TARGET_TRIPLE}\"")
  if ("${LLVM_TARGET_TRIPLE}" STREQUAL "")
    message(FATAL_ERROR "TARGET_TRIPLE cannot be empty")
  endif()
  set(LLVM_TARGET_TRIPLE "${LLVM_TARGET_TRIPLE}" PARENT_SCOPE)
endfunction()

macro(compiler_rt_mock_llvm_cmake_config_include_cmake_files)
  # Some compiler-rt CMake code needs to call code in this file.
  include("${LLVM_CMAKE_DIR}/AddLLVM.cmake")
endmacro()
