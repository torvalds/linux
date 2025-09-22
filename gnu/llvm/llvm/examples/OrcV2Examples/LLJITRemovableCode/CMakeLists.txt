set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IPO
  IRReader
  OrcJIT
  ScalarOpts
  Support
  nativecodegen
  )

add_llvm_example(LLJITRemovableCode
  LLJITRemovableCode.cpp
  )

export_executable_symbols(LLJITRemovableCode)
