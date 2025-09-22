set(LLVM_LINK_COMPONENTS support)

if(LLVM_USE_SANITIZE_COVERAGE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
endif()

add_clang_executable(clang-format-fuzzer
  EXCLUDE_FROM_ALL
  ClangFormatFuzzer.cpp
  )

target_link_libraries(clang-format-fuzzer
  PRIVATE
  ${CLANG_FORMAT_LIB_DEPS}
  ${LLVM_LIB_FUZZING_ENGINE}
  )
