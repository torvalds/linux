set(LLVM_LINK_COMPONENTS
  Support
  TextAPI
  TextAPIBinaryReader
  Demangle
  Core
  )

add_clang_library(clangInstallAPI
  DiagnosticBuilderWrappers.cpp
  DirectoryScanner.cpp
  DylibVerifier.cpp
  FileList.cpp
  Frontend.cpp
  HeaderFile.cpp
  Library.cpp
  Visitor.cpp

  LINK_LIBS
  clangAST
  clangBasic
  clangLex
  )
