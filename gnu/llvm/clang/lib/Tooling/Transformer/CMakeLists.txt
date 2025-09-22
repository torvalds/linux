set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
)

add_clang_library(clangTransformer
  Parsing.cpp
  RangeSelector.cpp
  RewriteRule.cpp
  SourceCode.cpp
  SourceCodeBuilders.cpp
  Stencil.cpp
  Transformer.cpp

  LINK_LIBS
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangToolingCore
  clangToolingRefactoring

  DEPENDS
  omp_gen
  )
