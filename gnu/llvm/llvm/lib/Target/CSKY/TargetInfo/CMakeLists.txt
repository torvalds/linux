add_llvm_component_library(LLVMCSKYInfo
  CSKYTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  CSKY
)
