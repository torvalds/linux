add_llvm_component_library(LLVMLoongArchAsmParser
  LoongArchAsmParser.cpp

  LINK_COMPONENTS
  LoongArchDesc
  LoongArchInfo
  MC
  MCParser
  Support

  ADD_TO_COMPONENT
  LoongArch
  )
