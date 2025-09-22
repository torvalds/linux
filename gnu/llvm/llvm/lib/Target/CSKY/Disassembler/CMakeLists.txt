add_llvm_component_library(LLVMCSKYDisassembler
  CSKYDisassembler.cpp

  LINK_COMPONENTS
  CSKYInfo
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  CSKY
  )
