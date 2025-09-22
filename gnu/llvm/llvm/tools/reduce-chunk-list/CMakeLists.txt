set(LLVM_LINK_COMPONENTS
  Support
  )

add_llvm_tool(reduce-chunk-list
  reduce-chunk-list.cpp

  DEPENDS
  )
