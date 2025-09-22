add_llvm_component_library(LLVMSparcDisassembler
  SparcDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  SparcInfo
  Support

  ADD_TO_COMPONENT
  Sparc
  )
