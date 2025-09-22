add_llvm_component_library(LLVMM68kAsmParser
  M68kAsmParser.cpp

  LINK_COMPONENTS
  CodeGenTypes
  M68kCodeGen
  M68kInfo
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  M68k
  )
