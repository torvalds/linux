add_llvm_component_library(LLVMFrontendDriver
  CodeGenOptions.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend/Driver

  DEPENDS
  LLVMAnalysis
  LLVMTargetParser

  LINK_COMPONENTS
  Core
  Support
  Analysis
  )
