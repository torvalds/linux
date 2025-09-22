include_directories( ${CMAKE_CURRENT_BINARY_DIR}/.. ${CMAKE_CURRENT_SOURCE_DIR}/.. )

add_llvm_component_library(LLVMAArch64Disassembler
  AArch64Disassembler.cpp
  AArch64ExternalSymbolizer.cpp

  LINK_COMPONENTS
  AArch64Desc
  AArch64Info
  AArch64Utils
  MC
  MCDisassembler
  Support

  ADD_TO_COMPONENT
  AArch64
  )

add_dependencies(LLVMAArch64Disassembler AArch64CommonTableGen)
