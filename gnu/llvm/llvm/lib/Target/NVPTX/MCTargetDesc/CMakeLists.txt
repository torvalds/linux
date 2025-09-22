add_llvm_component_library(LLVMNVPTXDesc
  NVPTXInstPrinter.cpp
  NVPTXMCAsmInfo.cpp
  NVPTXMCTargetDesc.cpp
  NVPTXTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  NVPTXInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  NVPTX
  )
