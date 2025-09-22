add_llvm_component_library(LLVMXCoreDesc
  XCoreInstPrinter.cpp
  XCoreMCTargetDesc.cpp
  XCoreMCAsmInfo.cpp

  LINK_COMPONENTS
  MC
  Support
  XCoreInfo

  ADD_TO_COMPONENT
  XCore
  )
