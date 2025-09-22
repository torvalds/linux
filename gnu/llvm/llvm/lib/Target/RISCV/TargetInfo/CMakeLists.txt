add_llvm_component_library(LLVMRISCVInfo
  RISCVTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  RISCV
  )
