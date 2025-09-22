add_llvm_component_library(LLVMMCJIT
  MCJIT.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  ExecutionEngine
  Object
  RuntimeDyld
  Support
  Target
  )
