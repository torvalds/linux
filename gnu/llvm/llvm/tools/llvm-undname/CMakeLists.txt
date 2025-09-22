set(LLVM_LINK_COMPONENTS
  Demangle
  Support
  )

add_llvm_tool(llvm-undname
  llvm-undname.cpp
  )
