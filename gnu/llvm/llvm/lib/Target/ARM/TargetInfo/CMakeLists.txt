add_llvm_component_library(LLVMARMInfo
  ARMTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  ARM
  )
