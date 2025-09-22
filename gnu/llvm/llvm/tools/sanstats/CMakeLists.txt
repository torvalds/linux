set(LLVM_LINK_COMPONENTS
  Support
  Symbolize
  )

add_llvm_tool(sanstats
  sanstats.cpp

  DEPENDS
  intrinsics_gen
  )
