set(LLVM_LINK_COMPONENTS
  FrontendOpenMP
  Support
)

add_clang_library(clangDynamicASTMatchers
  Diagnostics.cpp
  Marshallers.cpp
  Parser.cpp
  Registry.cpp
  VariantValue.cpp

  LINK_LIBS
  clangAST
  clangASTMatchers
  clangBasic

  DEPENDS
  omp_gen
  ClangDriverOptions
  )
