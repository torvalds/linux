add_llvm_component_library(LLVMFrontendOpenMP
  OMP.cpp
  OMPContext.cpp
  OMPIRBuilder.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Frontend/OpenMP

  DEPENDS
  intrinsics_gen
  omp_gen

  LINK_COMPONENTS
  Core
  Support
  TargetParser
  TransformUtils
  Analysis
  MC
  Scalar
  BitReader
  FrontendOffloading
  )
