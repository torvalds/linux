add_llvm_component_library(LLVMVEDesc
  VEAsmBackend.cpp
  VEELFObjectWriter.cpp
  VEInstPrinter.cpp
  VEMCAsmInfo.cpp
  VEMCCodeEmitter.cpp
  VEMCExpr.cpp
  VEMCTargetDesc.cpp
  VETargetStreamer.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  Support
  TargetParser
  VEInfo

  ADD_TO_COMPONENT
  VE
  )
