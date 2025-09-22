set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  Core
  ExecutionEngine
  IPO
  IRPrinter
  IRReader
  MC
  MCJIT
  Object
  Passes
  RuntimeDyld
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  native
)

add_clang_library(clangHandleLLVM
  handle_llvm.cpp

  DEPENDS
  intrinsics_gen
  )
