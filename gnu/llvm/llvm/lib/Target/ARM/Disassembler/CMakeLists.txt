add_llvm_component_library(LLVMARMDisassembler
  ARMDisassembler.cpp

  LINK_COMPONENTS
  ARMDesc
  ARMInfo
  ARMUtils
  CodeGenTypes
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  ARM
  )
