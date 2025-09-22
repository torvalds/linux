add_llvm_component_library(LLVMNVPTXInfo
  NVPTXTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  NVPTX
  )
