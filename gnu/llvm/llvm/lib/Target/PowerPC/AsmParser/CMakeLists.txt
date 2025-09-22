add_llvm_component_library(LLVMPowerPCAsmParser
  PPCAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  PowerPCDesc
  PowerPCInfo
  Support

  ADD_TO_COMPONENT
  PowerPC
  )
