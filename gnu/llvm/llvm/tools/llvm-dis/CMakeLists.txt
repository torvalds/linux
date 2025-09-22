set(LLVM_LINK_COMPONENTS
  BitReader
  Core
  Support
  )

add_llvm_tool(llvm-dis
  llvm-dis.cpp

  DEPENDS
  intrinsics_gen
  )
