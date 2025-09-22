include_directories( ${CMAKE_CURRENT_BINARY_DIR}/../Checkers )

set(LLVM_LINK_COMPONENTS
  Support
  )

add_clang_library(clangStaticAnalyzerFrontend
  AnalysisConsumer.cpp
  AnalyzerHelpFlags.cpp
  CheckerRegistry.cpp
  CreateCheckerManager.cpp
  FrontendActions.cpp
  ModelConsumer.cpp
  ModelInjector.cpp

  LINK_LIBS
  clangAST
  clangASTMatchers
  clangAnalysis
  clangBasic
  clangCrossTU
  clangFrontend
  clangLex
  clangStaticAnalyzerCheckers
  clangStaticAnalyzerCore

  DEPENDS
  omp_gen
  ClangDriverOptions
  )
