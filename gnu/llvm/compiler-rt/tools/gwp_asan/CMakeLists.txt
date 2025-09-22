# Build the stack trace compressor fuzzer. This will require Clang >= 6.0.0, as
# -fsanitize=fuzzer-no-link was not a valid command line flag prior to this.
if (LLVM_USE_SANITIZE_COVERAGE)
  add_executable(stack_trace_compressor_fuzzer
      ../../lib/gwp_asan/stack_trace_compressor.cpp
      ../../lib/gwp_asan/stack_trace_compressor.h
      stack_trace_compressor_fuzzer.cpp)
  set_target_properties(
      stack_trace_compressor_fuzzer PROPERTIES FOLDER "Fuzzers")
  target_compile_options(
      stack_trace_compressor_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
  set_target_properties(
      stack_trace_compressor_fuzzer PROPERTIES LINK_FLAGS -fsanitize=fuzzer)
  target_include_directories(
      stack_trace_compressor_fuzzer PRIVATE ../../lib/)

  add_executable(options_parser_fuzzer
      ../../lib/gwp_asan/optional/options_parser.cpp
      ../../lib/gwp_asan/optional/options_parser.h
      options_parser_fuzzer.cpp)
  set_target_properties(
      options_parser_fuzzer PROPERTIES FOLDER "Fuzzers")
  target_compile_options(
      options_parser_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
  set_target_properties(
      options_parser_fuzzer PROPERTIES LINK_FLAGS -fsanitize=fuzzer)
  target_include_directories(
      options_parser_fuzzer PRIVATE ../../lib/)

  if (TARGET gwp_asan)
    add_dependencies(gwp_asan stack_trace_compressor_fuzzer options_parser_fuzzer)
  endif()
endif()
