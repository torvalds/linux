add_llvm_component_library(LLVMSPIRVAnalysis
  SPIRVConvergenceRegionAnalysis.cpp

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils

  ADD_TO_COMPONENT
  SPIRV
  )
