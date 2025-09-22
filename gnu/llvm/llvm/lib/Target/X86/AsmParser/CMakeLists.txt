add_llvm_component_library(LLVMX86AsmParser
  X86AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  Support
  X86Desc
  X86Info

  ADD_TO_COMPONENT
  X86
  )
