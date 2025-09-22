set(LLVM_LINK_COMPONENTS
  Analysis
  BitWriter
  Core
  IPO
  IRReader
  IRPrinter
  Passes
  Support
  )

add_llvm_tool(llvm-extract
  llvm-extract.cpp

  DEPENDS
  intrinsics_gen
  )
