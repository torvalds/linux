set(LLVM_LINK_COMPONENTS
  Option
  Support
  )

add_clang_tool(clang-refactor
  ClangRefactor.cpp
  TestSupport.cpp
  )

clang_target_link_libraries(clang-refactor
  PRIVATE
  clangAST
  clangBasic
  clangFormat
  clangFrontend
  clangLex
  clangRewrite
  clangSerialization
  clangTooling
  clangToolingCore
  clangToolingRefactoring
  )
