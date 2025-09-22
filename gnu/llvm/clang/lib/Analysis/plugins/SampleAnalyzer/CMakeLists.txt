set(LLVM_LINK_COMPONENTS
  Support
  )

set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SampleAnalyzerPlugin.exports)
add_llvm_library(SampleAnalyzerPlugin MODULE BUILDTREE_ONLY MainCallChecker.cpp)

clang_target_link_libraries(SampleAnalyzerPlugin PRIVATE
  clangAnalysis
  clangAST
  clangStaticAnalyzerCore
  clangStaticAnalyzerFrontend
  )
