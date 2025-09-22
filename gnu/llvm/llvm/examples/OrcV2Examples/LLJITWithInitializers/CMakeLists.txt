set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IRReader
  JITLink
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithInitializers
  LLJITWithInitializers.cpp
  )

export_executable_symbols(LLJITWithInitializers)
