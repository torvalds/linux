find_program(CODE_COVERAGE_LCOV lcov)
if (NOT CODE_COVERAGE_LCOV)
  message(FATAL_ERROR "Cannot find lcov...")
endif()

find_program(CODE_COVERAGE_LLVM_COV llvm-cov)
if (NOT CODE_COVERAGE_LLVM_COV)
  message(FATAL_ERROR "Cannot find llvm-cov...")
endif()

find_program(CODE_COVERAGE_GENHTML genhtml)
if (NOT CODE_COVERAGE_GENHTML)
  message(FATAL_ERROR "Cannot find genhtml...")
endif()

set(CMAKE_CXX_FLAGS_COVERAGE "-g -O0 --coverage")

function(setup_lcov_test_target_coverage target_name output_dir capture_dirs source_dirs)
  if (NOT DEFINED LIBCXX_BINARY_DIR)
    message(FATAL_ERROR "Variable must be set")
  endif()

  set(GCOV_TOOL "${LIBCXX_BINARY_DIR}/llvm-cov-wrapper")
  file(GENERATE OUTPUT ${GCOV_TOOL}
    CONTENT "#!/usr/bin/env bash\n${CODE_COVERAGE_LLVM_COV} gcov \"$@\"\n")

  file(MAKE_DIRECTORY ${output_dir})

  set(CAPTURE_DIRS "")
  foreach(cdir ${capture_dirs})
    list(APPEND CAPTURE_DIRS "-d;${cdir}")
  endforeach()

  set(EXTRACT_DIRS "")
  foreach(sdir ${source_dirs})
    list(APPEND EXTRACT_DIRS "'${sdir}/*'")
  endforeach()

  message(STATUS "Capture Directories: ${CAPTURE_DIRS}")
  message(STATUS "Extract Directories: ${EXTRACT_DIRS}")

  add_custom_target(generate-lib${target_name}-coverage
        COMMAND chmod +x ${GCOV_TOOL}
        COMMAND ${CODE_COVERAGE_LCOV} --gcov-tool ${GCOV_TOOL} --capture ${CAPTURE_DIRS} -o test_coverage.info
        COMMAND ${CODE_COVERAGE_LCOV} --gcov-tool ${GCOV_TOOL} --extract test_coverage.info ${EXTRACT_DIRS} -o test_coverage.info
        COMMAND ${CODE_COVERAGE_GENHTML} --demangle-cpp test_coverage.info -o test_coverage
        COMMAND ${CMAKE_COMMAND} -E remove test_coverage.info
        WORKING_DIRECTORY ${output_dir}
        COMMENT "Generating coverage results")
endfunction()
