add_llvm_component_library(LLVMLoongArchDisassembler
  LoongArchDisassembler.cpp

  LINK_COMPONENTS
  LoongArchDesc
  LoongArchInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  LoongArch
  )
