add_llvm_component_library(LLVMHipStdPar
  HipStdPar.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms/HipStdPar

  DEPENDS
  intrinsics_gen
  LLVMAnalysis

  COMPONENT_NAME
  HipStdPar

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils)
