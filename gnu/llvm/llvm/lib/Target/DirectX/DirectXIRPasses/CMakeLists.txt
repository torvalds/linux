add_llvm_component_library(LLVMDirectXPointerTypeAnalysis
  PointerTypeAnalysis.cpp

  LINK_COMPONENTS
  Core
  Support

  ADD_TO_COMPONENT
  DirectX
  )
