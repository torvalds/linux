add_llvm_component_library(LLVMARCInfo
  ARCTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  ARC
  )
