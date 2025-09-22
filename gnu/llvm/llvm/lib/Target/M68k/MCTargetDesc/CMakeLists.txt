add_llvm_component_library(LLVMM68kDesc
  M68kAsmBackend.cpp
  M68kELFObjectWriter.cpp
  M68kInstPrinter.cpp
  M68kMCAsmInfo.cpp
  M68kMCCodeEmitter.cpp
  M68kMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  MCDisassembler
  Support
  TargetParser
  M68kInfo

  ADD_TO_COMPONENT
  M68k
)
