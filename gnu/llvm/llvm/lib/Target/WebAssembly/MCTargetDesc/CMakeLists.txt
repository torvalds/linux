add_llvm_component_library(LLVMWebAssemblyDesc
  WebAssemblyAsmBackend.cpp
  WebAssemblyInstPrinter.cpp
  WebAssemblyMCAsmInfo.cpp
  WebAssemblyMCCodeEmitter.cpp
  WebAssemblyMCTargetDesc.cpp
  WebAssemblyMCTypeUtilities.cpp
  WebAssemblyTargetStreamer.cpp
  WebAssemblyWasmObjectWriter.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  Support
  TargetParser
  WebAssemblyInfo

  ADD_TO_COMPONENT
  WebAssembly
  )
