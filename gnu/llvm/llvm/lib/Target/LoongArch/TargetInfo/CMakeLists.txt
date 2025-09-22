add_llvm_component_library(LLVMLoongArchInfo
  LoongArchTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  LoongArch
  )
