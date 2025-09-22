set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangRewriteFrontend
  FixItRewriter.cpp
  FrontendActions.cpp
  HTMLPrint.cpp
  InclusionRewriter.cpp
  RewriteMacros.cpp
  RewriteModernObjC.cpp
  RewriteObjC.cpp
  RewriteTest.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangEdit
  clangFrontend
  clangLex
  clangRewrite
  clangSerialization

  DEPENDS
  omp_gen
  )
