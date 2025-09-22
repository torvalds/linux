add_llvm_component_library(LLVMObjCARCOpts
  ObjCARC.cpp
  ObjCARCOpts.cpp
  ObjCARCExpand.cpp
  ObjCARCAPElim.cpp
  ObjCARCContract.cpp
  DependencyAnalysis.cpp
  ProvenanceAnalysis.cpp
  ProvenanceAnalysisEvaluator.cpp
  PtrState.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Transforms

  DEPENDS
  intrinsics_gen

  COMPONENT_NAME
  ObjCARC

  LINK_COMPONENTS
  Analysis
  Core
  Support
  TransformUtils
  )
