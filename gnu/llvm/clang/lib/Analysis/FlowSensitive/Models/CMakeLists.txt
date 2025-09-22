add_clang_library(clangAnalysisFlowSensitiveModels
  ChromiumCheckModel.cpp
  UncheckedOptionalAccessModel.cpp

  LINK_LIBS
  clangAnalysis
  clangAnalysisFlowSensitive
  clangAST
  clangASTMatchers
  clangBasic
  )
