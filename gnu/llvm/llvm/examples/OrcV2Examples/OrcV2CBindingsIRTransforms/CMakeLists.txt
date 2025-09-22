set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IRReader
  JITLink
  MC
  OrcJIT
  Passes
  Support
  Target
  nativecodegen
  )

add_llvm_example(OrcV2CBindingsIRTransforms
  OrcV2CBindingsIRTransforms.c
  )

export_executable_symbols(OrcV2CBindingsIRTransforms)
