add_clang_library(clangAnalysisFlowSensitive
  AdornedCFG.cpp
  Arena.cpp
  ASTOps.cpp
  CNFFormula.cpp
  DataflowAnalysisContext.cpp
  DataflowEnvironment.cpp
  Formula.cpp
  HTMLLogger.cpp
  Logger.cpp
  RecordOps.cpp
  SimplifyConstraints.cpp
  Transfer.cpp
  TypeErasedDataflowAnalysis.cpp
  Value.cpp
  WatchedLiteralsSolver.cpp
  DebugSupport.cpp

  DEPENDS
  clangAnalysisFlowSensitiveResources
  LINK_LIBS
  clangAnalysis
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  )

add_subdirectory(Models)

add_custom_command(OUTPUT HTMLLogger.inc
  COMMAND "${Python3_EXECUTABLE}" ${CLANG_SOURCE_DIR}/utils/bundle_resources.py
  ${CMAKE_CURRENT_BINARY_DIR}/HTMLLogger.inc
  HTMLLogger.html HTMLLogger.css HTMLLogger.js
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Bundling HTMLLogger resources"
  DEPENDS ${CLANG_SOURCE_DIR}/utils/bundle_resources.py HTMLLogger.html HTMLLogger.css HTMLLogger.js
  VERBATIM)
add_custom_target(clangAnalysisFlowSensitiveResources DEPENDS HTMLLogger.inc)
set_target_properties(clangAnalysisFlowSensitiveResources PROPERTIES FOLDER "Clang/Misc")
