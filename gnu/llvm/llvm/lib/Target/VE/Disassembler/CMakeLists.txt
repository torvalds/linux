add_llvm_component_library(LLVMVEDisassembler
  VEDisassembler.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCDisassembler
  Support
  VEInfo

  ADD_TO_COMPONENT
  VE
  )
