add_llvm_component_library(LLVMARMUtils
  ARMBaseInfo.cpp

  LINK_COMPONENTS
  Support

  ADD_TO_COMPONENT
  ARM
  )
