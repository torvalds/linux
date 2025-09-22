add_llvm_component_library(LLVMXtensaAsmParser
  XtensaAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  XtensaDesc
  XtensaInfo

  ADD_TO_COMPONENT
  Xtensa
  )
