add_llvm_component_library(LLVMWebAssemblyUtils
  WebAssemblyTypeUtilities.cpp

  LINK_COMPONENTS
  CodeGen
  CodeGenTypes
  Core
  MC
  Support
  WebAssemblyDesc

  ADD_TO_COMPONENT
  WebAssembly
  )
