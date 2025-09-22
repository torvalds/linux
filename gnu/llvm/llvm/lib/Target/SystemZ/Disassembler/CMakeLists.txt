add_llvm_component_library(LLVMSystemZDisassembler
  SystemZDisassembler.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  Support
  SystemZDesc
  SystemZInfo

  ADD_TO_COMPONENT
  SystemZ
  )
