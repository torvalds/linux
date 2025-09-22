set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangToolingASTDiff
  ASTDiff.cpp
  LINK_LIBS
  clangBasic
  clangAST
  clangLex

  DEPENDS
  omp_gen
  )
