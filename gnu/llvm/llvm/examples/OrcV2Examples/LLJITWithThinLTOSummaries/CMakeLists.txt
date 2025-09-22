set(LLVM_LINK_COMPONENTS
  BitReader
  Core
  ExecutionEngine
  OrcJIT
  OrcTargetProcess
  Support
  TargetParser
  nativecodegen
  )

add_llvm_example(LLJITWithThinLTOSummaries
  LLJITWithThinLTOSummaries.cpp
  )

export_executable_symbols(LLJITWithThinLTOSummaries)
