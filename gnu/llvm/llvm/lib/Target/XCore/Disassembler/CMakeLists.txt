add_llvm_component_library(LLVMXCoreDisassembler
  XCoreDisassembler.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCDisassembler
  Support
  XCoreInfo

  ADD_TO_COMPONENT
  XCore
  )
