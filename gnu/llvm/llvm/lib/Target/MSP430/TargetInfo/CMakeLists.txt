add_llvm_component_library(LLVMMSP430Info
  MSP430TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  MSP430
  )
