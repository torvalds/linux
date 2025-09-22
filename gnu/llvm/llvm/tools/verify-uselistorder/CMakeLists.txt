set(LLVM_LINK_COMPONENTS
  AsmParser
  BitReader
  BitWriter
  Core
  IRReader
  Support
  )

add_llvm_tool(verify-uselistorder
  verify-uselistorder.cpp

  DEPENDS
  intrinsics_gen
  )
