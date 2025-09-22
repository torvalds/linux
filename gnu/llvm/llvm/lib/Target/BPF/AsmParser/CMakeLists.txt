add_llvm_component_library(LLVMBPFAsmParser
  BPFAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  BPFDesc
  BPFInfo
  Support

  ADD_TO_COMPONENT
  BPF
  )
