add_llvm_component_library(LLVMMipsDesc
  MipsABIInfo.cpp
  MipsABIFlagsSection.cpp
  MipsAsmBackend.cpp
  MipsELFObjectWriter.cpp
  MipsELFStreamer.cpp
  MipsInstPrinter.cpp
  MipsMCAsmInfo.cpp
  MipsMCCodeEmitter.cpp
  MipsMCExpr.cpp
  MipsMCTargetDesc.cpp
  MipsNaClELFStreamer.cpp
  MipsOptionRecord.cpp
  MipsTargetStreamer.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MipsInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  Mips
  )
