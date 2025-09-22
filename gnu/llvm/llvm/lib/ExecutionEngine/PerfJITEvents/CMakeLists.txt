add_llvm_component_library(LLVMPerfJITEvents
  PerfJITEventListener.cpp

  LINK_COMPONENTS
  CodeGen
  Core
  DebugInfoDWARF
  ExecutionEngine
  Object
  Support
  )

add_dependencies(LLVMPerfJITEvents LLVMCodeGen)
