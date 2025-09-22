add_llvm_component_library(LLVMLinker
  IRMover.cpp
  LinkModules.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/Linker

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  Object
  Support
  TransformUtils
  TargetParser
  )
