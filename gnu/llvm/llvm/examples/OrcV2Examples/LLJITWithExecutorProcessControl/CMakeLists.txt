set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IRReader
  OrcJIT
  Support
  nativecodegen
  )

add_llvm_example(LLJITWithExecutorProcessControl
  LLJITWithExecutorProcessControl.cpp
  )

export_executable_symbols(LLJITWithExecutorProcessControl)
