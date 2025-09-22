set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

add_clang_library(clangCrossTU
  CrossTranslationUnit.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangFrontend
  clangIndex
  )
