add_llvm_component_library(LLVMLanaiDisassembler
  LanaiDisassembler.cpp

  LINK_COMPONENTS
  CodeGenTypes
  LanaiDesc
  LanaiInfo
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  Lanai
  )
