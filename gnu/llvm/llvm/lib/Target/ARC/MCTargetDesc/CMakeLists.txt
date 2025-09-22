add_llvm_component_library(LLVMARCDesc
  ARCInstPrinter.cpp
  ARCMCTargetDesc.cpp
  ARCMCAsmInfo.cpp

  LINK_COMPONENTS
  MC
  Support
  ARCInfo

  ADD_TO_COMPONENT
  ARC
  )
