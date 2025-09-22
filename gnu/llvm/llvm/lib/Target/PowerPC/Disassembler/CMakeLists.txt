add_llvm_component_library(LLVMPowerPCDisassembler
  PPCDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  PowerPCInfo
  Support

  ADD_TO_COMPONENT
  PowerPC
  )
