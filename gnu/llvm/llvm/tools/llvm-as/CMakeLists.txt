set(LLVM_LINK_COMPONENTS
  AsmParser
  BitWriter
  Core
  Support
  )

add_llvm_tool(llvm-as
  llvm-as.cpp

  DEPENDS
  intrinsics_gen
  )
