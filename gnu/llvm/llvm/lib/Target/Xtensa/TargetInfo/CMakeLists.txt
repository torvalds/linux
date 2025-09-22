add_llvm_component_library(LLVMXtensaInfo
  XtensaTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Xtensa
  )
