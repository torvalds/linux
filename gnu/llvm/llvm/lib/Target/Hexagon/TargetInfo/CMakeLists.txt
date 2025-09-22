add_llvm_component_library(LLVMHexagonInfo
  HexagonTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  Hexagon
  )
