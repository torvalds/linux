add_llvm_component_library(LLVMSystemZInfo
  SystemZTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  SystemZ
  )
