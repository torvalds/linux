add_llvm_component_library(LLVMAVRDesc
  AVRAsmBackend.cpp
  AVRELFObjectWriter.cpp
  AVRELFStreamer.cpp
  AVRInstPrinter.cpp
  AVRMCAsmInfo.cpp
  AVRMCCodeEmitter.cpp
  AVRMCELFStreamer.cpp
  AVRMCExpr.cpp
  AVRMCTargetDesc.cpp
  AVRTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  AVRInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  AVR
)

add_dependencies(LLVMAVRDesc AVRCommonTableGen)

