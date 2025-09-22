add_llvm_component_library(LLVMAMDGPUAsmParser
  AMDGPUAsmParser.cpp

  LINK_COMPONENTS
  AMDGPUDesc
  AMDGPUInfo
  AMDGPUUtils
  CodeGenTypes
  MC
  MCParser
  Support
  TargetParser

  ADD_TO_COMPONENT
  AMDGPU
  )
