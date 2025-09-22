set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

add_clang_library(clangIndex
  CommentToXML.cpp
  FileIndexRecord.cpp
  IndexBody.cpp
  IndexDecl.cpp
  IndexingAction.cpp
  IndexingContext.cpp
  IndexSymbol.cpp
  IndexTypeSourceInfo.cpp
  USRGeneration.cpp

  ADDITIONAL_HEADERS
  IndexingContext.h

  LINK_LIBS
  clangAST
  clangBasic
  clangFormat
  clangFrontend
  clangLex
  clangSerialization
  clangToolingCore

  DEPENDS
  omp_gen
  ClangDriverOptions
  )
