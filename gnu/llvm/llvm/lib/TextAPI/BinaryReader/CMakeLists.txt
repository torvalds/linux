add_llvm_component_library(LLVMTextAPIBinaryReader
  DylibReader.cpp

  LINK_COMPONENTS
  DebugInfoDWARF
  Support
  Object
  TextAPI
  TargetParser
)
