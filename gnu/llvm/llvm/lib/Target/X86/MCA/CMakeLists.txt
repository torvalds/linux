add_llvm_component_library(LLVMX86TargetMCA
  X86CustomBehaviour.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCA
  MCParser
  Support
  TargetParser
  X86Desc
  X86Info

  ADD_TO_COMPONENT
  X86
  )
