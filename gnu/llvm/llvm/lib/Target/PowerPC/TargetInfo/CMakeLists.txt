add_llvm_component_library(LLVMPowerPCInfo
  PowerPCTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  PowerPC
  )
