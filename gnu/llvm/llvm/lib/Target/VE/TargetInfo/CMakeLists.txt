add_llvm_component_library(LLVMVEInfo
  VETargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  VE
  )
