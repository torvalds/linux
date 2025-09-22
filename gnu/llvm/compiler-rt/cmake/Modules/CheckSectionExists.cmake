function(check_section_exists section output)
  cmake_parse_arguments(ARG "" "" "SOURCE;FLAGS" ${ARGN})
  if(NOT ARG_SOURCE)
    set(ARG_SOURCE "int main(void) { return 0; }\n")
  endif()

  string(RANDOM TARGET_NAME)
  set(TARGET_NAME "${CMAKE_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/cmTC_${TARGET_NAME}.dir")
  file(MAKE_DIRECTORY ${TARGET_NAME})

  file(WRITE "${TARGET_NAME}/CheckSectionExists.c" "${ARG_SOURCE}\n")

  string(REGEX MATCHALL "<[A-Za-z0-9_]*>" substitutions
         ${CMAKE_C_COMPILE_OBJECT})

  set(try_compile_flags "${ARG_FLAGS}")
  if(CMAKE_C_COMPILER_ID MATCHES Clang AND CMAKE_C_COMPILER_TARGET)
    list(APPEND try_compile_flags "-target ${CMAKE_C_COMPILER_TARGET}")
  endif()
  append_list_if(COMPILER_RT_HAS_FNO_LTO_FLAG -fno-lto try_compile_flags)
  if(NOT COMPILER_RT_ENABLE_PGO)
    if(LLVM_PROFDATA_FILE AND COMPILER_RT_HAS_FNO_PROFILE_INSTR_USE_FLAG)
      list(APPEND try_compile_flags "-fno-profile-instr-use")
    endif()
    if(LLVM_BUILD_INSTRUMENTED MATCHES IR AND COMPILER_RT_HAS_FNO_PROFILE_GENERATE_FLAG)
      list(APPEND try_compile_flags "-fno-profile-generate")
    elseif((LLVM_BUILD_INSTRUMENTED OR LLVM_BUILD_INSTRUMENTED_COVERAGE) AND COMPILER_RT_HAS_FNO_PROFILE_INSTR_GENERATE_FLAG)
      list(APPEND try_compile_flags "-fno-profile-instr-generate")
      if(LLVM_BUILD_INSTRUMENTED_COVERAGE AND COMPILER_RT_HAS_FNO_COVERAGE_MAPPING_FLAG)
        list(APPEND try_compile_flags "-fno-coverage-mapping")
      endif()
    endif()
  endif()

  string(REPLACE ";" " " extra_flags "${try_compile_flags}")

  set(test_compile_command "${CMAKE_C_COMPILE_OBJECT}")
  foreach(substitution ${substitutions})
    if(substitution STREQUAL "<CMAKE_C_COMPILER>")
      string(REPLACE "<CMAKE_C_COMPILER>" "${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}"
             test_compile_command ${test_compile_command})
    elseif(substitution STREQUAL "<OBJECT>")
      string(REPLACE "<OBJECT>" "${TARGET_NAME}/CheckSectionExists.o"
             test_compile_command ${test_compile_command})
    elseif(substitution STREQUAL "<SOURCE>")
      string(REPLACE "<SOURCE>" "${TARGET_NAME}/CheckSectionExists.c"
             test_compile_command ${test_compile_command})
    elseif(substitution STREQUAL "<FLAGS>")
      string(REPLACE "<FLAGS>" "${CMAKE_C_FLAGS} ${extra_flags}"
             test_compile_command ${test_compile_command})
    else()
      string(REPLACE "${substitution}" "" test_compile_command
             ${test_compile_command})
    endif()
  endforeach()

  # Strip quotes from the compile command, as the compiler is not expecting
  # quoted arguments (potential quotes added from D62063).
  string(REPLACE "\"" "" test_compile_command "${test_compile_command}")

  string(REPLACE " " ";" test_compile_command "${test_compile_command}")

  execute_process(
    COMMAND ${test_compile_command}
    RESULT_VARIABLE TEST_RESULT
    OUTPUT_VARIABLE TEST_OUTPUT
    ERROR_VARIABLE TEST_ERROR
  )

  # Explicitly throw a fatal error message if test_compile_command fails.
  if(TEST_RESULT)
    message(FATAL_ERROR "${TEST_ERROR}")
    return()
  endif()

  execute_process(
    COMMAND ${CMAKE_OBJDUMP} -h "${TARGET_NAME}/CheckSectionExists.o"
    RESULT_VARIABLE CHECK_RESULT
    OUTPUT_VARIABLE CHECK_OUTPUT
    ERROR_VARIABLE CHECK_ERROR
  )
  string(FIND "${CHECK_OUTPUT}" "${section}" SECTION_FOUND)

  if(NOT SECTION_FOUND EQUAL -1)
    set(${output} TRUE PARENT_SCOPE)
  else()
    set(${output} FALSE PARENT_SCOPE)
  endif()

  file(REMOVE_RECURSE ${TARGET_NAME})
endfunction()
