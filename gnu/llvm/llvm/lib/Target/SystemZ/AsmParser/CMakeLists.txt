add_llvm_component_library(LLVMSystemZAsmParser
  SystemZAsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  SystemZDesc
  SystemZInfo

  ADD_TO_COMPONENT
  SystemZ
  )
