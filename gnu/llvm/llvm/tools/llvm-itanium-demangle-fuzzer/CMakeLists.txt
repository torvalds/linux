set(LLVM_LINK_COMPONENTS
  Demangle
  FuzzerCLI
  Support
)

add_llvm_fuzzer(llvm-itanium-demangle-fuzzer
  llvm-itanium-demangle-fuzzer.cpp
  DUMMY_MAIN DummyDemanglerFuzzer.cpp
  )
