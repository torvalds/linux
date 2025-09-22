add_llvm_component_library(LLVMAMDGPUUtils
  AMDGPUAsmUtils.cpp
  AMDGPUBaseInfo.cpp
  AMDGPUDelayedMCExpr.cpp
  AMDGPUMemoryUtils.cpp
  AMDGPUPALMetadata.cpp
  AMDKernelCodeTUtils.cpp

  LINK_COMPONENTS
  Analysis
  BinaryFormat
  CodeGenTypes
  Core
  MC
  MCParser
  Support
  TargetParser

  ADD_TO_COMPONENT
  AMDGPU
  )
