include(CMakeCheckCompilerFlagCommonPatterns)

# Test compiler can compile simple C/C++/Objective-C program without invoking
# the linker.
#
# try_compile_only(
#   OUTPUT_VAR
#   [SOURCE source_text]
#   [FLAGS flag_0 [ flag_1 ]]
# )
#
# OUTPUT_VAR - The variable name to store the result. The result is a boolean
#              `True` or `False`.
#
# SOURCE     - Optional. If specified use source the source text string
#              specified. If not specified source code will be used that is C,
#              C++, and Objective-C compatible.
#
# FLAGS      - Optional. If specified pass the one or more specified flags to
#              the compiler.
#
# EXAMPLES:
#
# try_compile_only(HAS_F_NO_RTTI FLAGS "-fno-rtti")
#
# try_compile_only(HAS_CXX_AUTO_TYPE_DECL
#   SOURCE "int foo(int x) { auto y = x + 1; return y;}"
#   FLAGS "-x" "c++" "-std=c++11" "-Werror=c++11-extensions"
# )
#
function(try_compile_only output)
  # NOTE: `SOURCE` needs to be a multi-argument because source code
  # often contains semicolons which happens to be CMake's list separator
  # which confuses `cmake_parse_arguments()`.
  cmake_parse_arguments(ARG "" "" "SOURCE;FLAGS" ${ARGN})
  if (ARG_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unexpected arguments \"${ARG_UNPARSED_ARGUMENTS}\"")
  endif()
  if(NOT ARG_SOURCE)
    set(ARG_SOURCE "int foo(int x, int y) { return x + y; }\n")
  endif()
  set(SIMPLE_C ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/src.c)
  file(WRITE ${SIMPLE_C} "${ARG_SOURCE}\n")
  string(REGEX MATCHALL "<[A-Za-z0-9_]*>" substitutions
         ${CMAKE_C_COMPILE_OBJECT})

  set(TRY_COMPILE_FLAGS "${ARG_FLAGS}")
  if(CMAKE_C_COMPILER_ID MATCHES Clang AND CMAKE_C_COMPILER_TARGET)
    list(APPEND TRY_COMPILE_FLAGS "--target=${CMAKE_C_COMPILER_TARGET}")
  endif()

  string(REPLACE ";" " " extra_flags "${TRY_COMPILE_FLAGS}")

  set(test_compile_command "${CMAKE_C_COMPILE_OBJECT}")
  foreach(substitution ${substitutions})
    if(substitution STREQUAL "<CMAKE_C_COMPILER>")
      string(REPLACE "<CMAKE_C_COMPILER>"
             "${CMAKE_C_COMPILER}" test_compile_command ${test_compile_command})
    elseif(substitution STREQUAL "<OBJECT>")
      string(REPLACE "<OBJECT>"
             "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/test.o"
             test_compile_command ${test_compile_command})
    elseif(substitution STREQUAL "<SOURCE>")
      string(REPLACE "<SOURCE>" "${SIMPLE_C}" test_compile_command
             ${test_compile_command})
    elseif(substitution STREQUAL "<FLAGS>")
      string(REPLACE "<FLAGS>" "${CMAKE_C_FLAGS} ${extra_flags}"
             test_compile_command ${test_compile_command})
    else()
      string(REPLACE "${substitution}" "" test_compile_command
             ${test_compile_command})
    endif()
  endforeach()

  # Strip quotes from the compile command, as the compiler is not expecting
  # quoted arguments (see discussion on D62063 for when this can come up). If
  # the quotes were there for arguments with spaces in them, the quotes were
  # not going to help since the string gets split on spaces below.
  string(REPLACE "\"" "" test_compile_command "${test_compile_command}")

  string(REPLACE " " ";" test_compile_command "${test_compile_command}")

  execute_process(
    COMMAND ${test_compile_command}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE TEST_OUTPUT
    ERROR_VARIABLE TEST_ERROR
  )

  CHECK_COMPILER_FLAG_COMMON_PATTERNS(_CheckCCompilerFlag_COMMON_PATTERNS)
  set(ERRORS_FOUND OFF)
  foreach(var ${_CheckCCompilerFlag_COMMON_PATTERNS})
    if("${var}" STREQUAL "FAIL_REGEX")
      continue()
    endif()
    if("${TEST_ERROR}" MATCHES "${var}" OR "${TEST_OUTPUT}" MATCHES "${var}")
      set(ERRORS_FOUND ON)
    endif()
  endforeach()

  if(result EQUAL 0 AND NOT ERRORS_FOUND)
    set(${output} True PARENT_SCOPE)
  else()
    file(APPEND ${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/CMakeError.log
        "Testing compiler for supporting " ${ARGN} ":\n"
        "Command: ${test_compile_command}\n"
        "${TEST_OUTPUT}\n${TEST_ERROR}\n${result}\n")
    set(${output} False PARENT_SCOPE)
  endif()
endfunction()

function(builtin_check_c_compiler_flag flag output)
  if(NOT DEFINED ${output})
    message(STATUS "Performing Test ${output}")
    try_compile_only(result FLAGS ${flag} ${CMAKE_REQUIRED_FLAGS})
    set(${output} ${result} CACHE INTERNAL "Compiler supports ${flag}")
    if(${result})
      message(STATUS "Performing Test ${output} - Success")
    else()
      message(STATUS "Performing Test ${output} - Failed")
    endif()
  endif()
endfunction()

function(builtin_check_c_compiler_source output source)
  if(NOT DEFINED ${output})
    message(STATUS "Performing Test ${output}")
    try_compile_only(result SOURCE ${source})
    set(${output} ${result} CACHE INTERNAL "Compiler supports ${flag}")
    if(${result})
      message(STATUS "Performing Test ${output} - Success")
    else()
      message(STATUS "Performing Test ${output} - Failed")
    endif()
  endif()
endfunction()
