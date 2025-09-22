add_llvm_component_library(LLVMMSP430AsmParser
  MSP430AsmParser.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCParser
  MSP430Desc
  MSP430Info
  Support

  ADD_TO_COMPONENT
  MSP430
  )
