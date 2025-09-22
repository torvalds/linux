# Don't include this target in LLVMExports. FIXME: is there a cleaner way?
set(EXCLUDE_FROM_ALL ON)

# Not add_clang_library: this is not part of clang's public library interface.
# Unit tests should depend on this with target_link_libraries(), rather
# than with clang_target_link_libraries().
add_llvm_library(clangTesting
  CommandLineArgs.cpp
  TestAST.cpp

  BUILDTREE_ONLY

  LINK_COMPONENTS
  MC
  Support
  )

clang_target_link_libraries(clangTesting
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangLex
  clangSerialization
  )

target_link_libraries(clangTesting
  PRIVATE
  llvm_gtest
  )
