add_llvm_component_library(LLVMAArch64Utils
  AArch64BaseInfo.cpp
  AArch64SMEAttributes.cpp

  LINK_COMPONENTS
  Support
  Core

  ADD_TO_COMPONENT
  AArch64
  )
