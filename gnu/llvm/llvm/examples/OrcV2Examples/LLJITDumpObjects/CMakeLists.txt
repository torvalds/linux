set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IRReader
  JITLink
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(LLJITDumpObjects
  LLJITDumpObjects.cpp
  )

export_executable_symbols(LLJITDumpObjects)
