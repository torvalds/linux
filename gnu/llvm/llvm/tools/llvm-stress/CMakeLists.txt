set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

add_llvm_tool(llvm-stress
  llvm-stress.cpp

  DEPENDS
  intrinsics_gen
  )
