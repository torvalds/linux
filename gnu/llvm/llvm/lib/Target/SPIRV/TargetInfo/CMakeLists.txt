add_llvm_component_library(LLVMSPIRVInfo
  SPIRVTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  SPIRV
  )
