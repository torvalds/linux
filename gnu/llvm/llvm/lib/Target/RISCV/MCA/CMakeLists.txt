add_llvm_component_library(LLVMRISCVTargetMCA
  RISCVCustomBehaviour.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCA
  MCParser
  RISCVDesc
  RISCVInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  RISCV
  )
