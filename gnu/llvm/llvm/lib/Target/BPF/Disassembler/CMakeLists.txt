add_llvm_component_library(LLVMBPFDisassembler
  BPFDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  BPFInfo
  Support

  ADD_TO_COMPONENT
  BPF
)

