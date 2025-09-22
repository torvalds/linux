set(LLVM_LINK_COMPONENTS
  Analysis
  Core
  ExecutionEngine
  InstCombine
  Object
  OrcJIT
  RuntimeDyld
  ScalarOpts
  Support
  native
  )

add_kaleidoscope_chapter(BuildingAJIT-Ch1
  toy.cpp
  )

export_executable_symbols(BuildingAJIT-Ch1)
