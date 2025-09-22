add_llvm_component_library(LLVMM68kInfo
  M68kTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  M68k
)
