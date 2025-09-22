set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_executable(clang-diff
  ClangDiff.cpp
  )

clang_target_link_libraries(clang-diff
  PRIVATE
  clangBasic
  clangFrontend
  clangSerialization
  clangTooling
  clangToolingASTDiff
  )
