add_llvm_component_library(LLVMLanaiDesc
  LanaiAsmBackend.cpp
  LanaiELFObjectWriter.cpp
  LanaiInstPrinter.cpp
  LanaiMCAsmInfo.cpp
  LanaiMCCodeEmitter.cpp
  LanaiMCExpr.cpp
  LanaiMCTargetDesc.cpp

  LINK_COMPONENTS
  LanaiInfo
  MC
  MCDisassembler
  Support
  TargetParser

  ADD_TO_COMPONENT
  Lanai
)
