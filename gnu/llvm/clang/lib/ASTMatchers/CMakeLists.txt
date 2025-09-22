add_subdirectory(Dynamic)

set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
)

add_clang_library(clangASTMatchers
  ASTMatchFinder.cpp
  ASTMatchersInternal.cpp
  GtestMatchers.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangLex

  DEPENDS
  omp_gen
  ClangDriverOptions
  )
