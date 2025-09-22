if (LLVM_USE_SANITIZE_COVERAGE)
  add_executable(get_error_info_fuzzer
      get_error_info_fuzzer.cpp)
  set_target_properties(
      get_error_info_fuzzer PROPERTIES FOLDER "Fuzzers")
  target_compile_options(
      get_error_info_fuzzer PRIVATE -fsanitize=fuzzer)
  set_target_properties(
      get_error_info_fuzzer PROPERTIES LINK_FLAGS -fsanitize=fuzzer)
  target_include_directories(
      get_error_info_fuzzer PRIVATE .. ../include)
endif()
