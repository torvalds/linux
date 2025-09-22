add_llvm_component_library(LLVMXtensaDesc
  XtensaAsmBackend.cpp
  XtensaELFObjectWriter.cpp
  XtensaInstPrinter.cpp
  XtensaMCAsmInfo.cpp
  XtensaMCCodeEmitter.cpp
  XtensaMCExpr.cpp
  XtensaMCTargetDesc.cpp
  XtensaTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  Support
  XtensaInfo

  ADD_TO_COMPONENT
  Xtensa
 )
