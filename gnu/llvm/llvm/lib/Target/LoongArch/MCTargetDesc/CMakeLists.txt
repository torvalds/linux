add_llvm_component_library(LLVMLoongArchDesc
  LoongArchAsmBackend.cpp
  LoongArchBaseInfo.cpp
  LoongArchELFObjectWriter.cpp
  LoongArchELFStreamer.cpp
  LoongArchInstPrinter.cpp
  LoongArchMCAsmInfo.cpp
  LoongArchMCCodeEmitter.cpp
  LoongArchMCExpr.cpp
  LoongArchMCTargetDesc.cpp
  LoongArchMatInt.cpp
  LoongArchTargetStreamer.cpp

  LINK_COMPONENTS
  MC
  LoongArchInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  LoongArch
  )
