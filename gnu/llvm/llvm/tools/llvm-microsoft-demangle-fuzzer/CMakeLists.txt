set(LLVM_LINK_COMPONENTS
  Demangle
  FuzzerCLI
  Support
)

add_llvm_fuzzer(llvm-microsoft-demangle-fuzzer
  llvm-microsoft-demangle-fuzzer.cpp
  DUMMY_MAIN DummyDemanglerFuzzer.cpp
  )
