add_llvm_component_library(LLVMLanaiInfo
  LanaiTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Lanai
  )
