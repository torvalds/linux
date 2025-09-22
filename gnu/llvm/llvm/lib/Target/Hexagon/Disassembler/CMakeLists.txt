add_llvm_component_library(LLVMHexagonDisassembler
  HexagonDisassembler.cpp

  LINK_COMPONENTS
  HexagonDesc
  HexagonInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Hexagon
  )
