set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

add_clang_library(clangExtractAPI
  API.cpp
  APIIgnoresList.cpp
  ExtractAPIConsumer.cpp
  DeclarationFragments.cpp
  Serialization/SymbolGraphSerializer.cpp
  TypedefUnderlyingTypeResolver.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangFrontend
  clangIndex
  clangInstallAPI
  clangLex
  )
