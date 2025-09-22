add_llvm_component_library(LLVMSparcAsmParser
  SparcAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  SparcDesc
  SparcInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  Sparc
  )
