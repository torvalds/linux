set(LLVM_LINK_COMPONENTS
  Object
  Support
  )
add_llvm_tool(llvm-debuginfod-find
  llvm-debuginfod-find.cpp
  )
target_link_libraries(llvm-debuginfod-find PRIVATE LLVMDebuginfod)
if(LLVM_INSTALL_BINUTILS_SYMLINKS)
  add_llvm_tool_symlink(debuginfod-find llvm-debuginfod-find)
endif()
