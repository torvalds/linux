add_llvm_component_library(LLVMXCoreInfo
  XCoreTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  XCore
  )
