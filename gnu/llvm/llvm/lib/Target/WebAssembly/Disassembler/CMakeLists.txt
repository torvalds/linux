add_llvm_component_library(LLVMWebAssemblyDisassembler
  WebAssemblyDisassembler.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCDisassembler
  Support
  WebAssemblyDesc
  WebAssemblyInfo

  ADD_TO_COMPONENT
  WebAssembly
  )
