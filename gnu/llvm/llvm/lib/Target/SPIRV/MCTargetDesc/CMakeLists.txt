add_llvm_component_library(LLVMSPIRVDesc
  SPIRVBaseInfo.cpp
  SPIRVMCAsmInfo.cpp
  SPIRVMCTargetDesc.cpp
  SPIRVTargetStreamer.cpp
  SPIRVAsmBackend.cpp
  SPIRVMCCodeEmitter.cpp
  SPIRVObjectTargetWriter.cpp
  SPIRVInstPrinter.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  SPIRVInfo
  Support
  TargetParser

  ADD_TO_COMPONENT
  SPIRV
  )
