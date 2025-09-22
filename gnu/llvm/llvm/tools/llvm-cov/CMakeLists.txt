set(LLVM_LINK_COMPONENTS
  Core
  Support
  Object
  Coverage
  ProfileData
  TargetParser
  )

add_llvm_tool(llvm-cov
  llvm-cov.cpp
  gcov.cpp
  CodeCoverage.cpp
  CoverageExporterJson.cpp
  CoverageExporterLcov.cpp
  CoverageFilters.cpp
  CoverageReport.cpp
  CoverageSummaryInfo.cpp
  SourceCoverageView.cpp
  SourceCoverageViewHTML.cpp
  SourceCoverageViewText.cpp
  TestingSupport.cpp
  )

target_link_libraries(llvm-cov PRIVATE LLVMDebuginfod)
