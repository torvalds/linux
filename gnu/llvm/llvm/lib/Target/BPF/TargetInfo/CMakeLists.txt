add_llvm_component_library(LLVMBPFInfo
  BPFTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  BPF
  )
