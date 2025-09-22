set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangRewrite
  DeltaTree.cpp
  HTMLRewrite.cpp
  RewriteRope.cpp
  Rewriter.cpp
  TokenRewriter.cpp

  LINK_LIBS
  clangBasic
  clangLex
  )
