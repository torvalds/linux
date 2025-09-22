add_llvm_component_library(LLVMX86Info
  X86TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  X86
  )
