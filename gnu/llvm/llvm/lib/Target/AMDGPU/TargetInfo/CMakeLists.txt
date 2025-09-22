add_llvm_component_library(LLVMAMDGPUInfo
  AMDGPUTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  AMDGPU
  )
