add_llvm_component_library(LLVMM68kDisassembler
  M68kDisassembler.cpp

  LINK_COMPONENTS
  CodeGenTypes
  M68kDesc
  M68kInfo
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  M68k
)

