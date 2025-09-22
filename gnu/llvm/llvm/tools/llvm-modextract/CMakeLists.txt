set(LLVM_LINK_COMPONENTS
  BitReader
  BitWriter
  Core
  Support
  )

add_llvm_tool(llvm-modextract
  llvm-modextract.cpp
  )
