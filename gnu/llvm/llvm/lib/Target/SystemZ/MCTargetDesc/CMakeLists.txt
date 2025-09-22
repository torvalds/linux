add_llvm_component_library(LLVMSystemZDesc
  SystemZELFObjectWriter.cpp
  SystemZGOFFObjectWriter.cpp
  SystemZInstPrinter.cpp
  SystemZMCAsmBackend.cpp
  SystemZMCAsmInfo.cpp
  SystemZMCCodeEmitter.cpp
  SystemZMCExpr.cpp
  SystemZMCTargetDesc.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  Support
  SystemZInfo

  ADD_TO_COMPONENT
  SystemZ
  )
