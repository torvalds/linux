set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  Object
  OrcJIT
  Support
  TargetParser
  native
  )

add_kaleidoscope_chapter(Kaleidoscope-Ch9
  toy.cpp
  )

export_executable_symbols(Kaleidoscope-Ch9)
