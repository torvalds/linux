add_llvm_component_library(LLVMDirectXDesc
  DirectXContainerObjectWriter.cpp
  DirectXMCTargetDesc.cpp

  LINK_COMPONENTS
  DirectXInfo
  MC
  Support
  TargetParser

  ADD_TO_COMPONENT
  DirectX
  )
