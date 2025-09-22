add_llvm_component_library(LLVMCSKYAsmParser
  CSKYAsmParser.cpp

  LINK_COMPONENTS
  CSKYDesc
  CSKYInfo
  MC
  MCParser
  Support
  TargetParser

  ADD_TO_COMPONENT
  CSKY
  )
