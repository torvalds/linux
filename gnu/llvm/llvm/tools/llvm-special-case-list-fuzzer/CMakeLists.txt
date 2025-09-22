set(LLVM_LINK_COMPONENTS
  Support
  FuzzerCLI
)

add_llvm_fuzzer(llvm-special-case-list-fuzzer
  special-case-list-fuzzer.cpp
  DUMMY_MAIN DummySpecialCaseListFuzzer.cpp
  )
