add_llvm_component_library(LLVMCodeGenData
  OutlinedHashTree.cpp
  OutlinedHashTreeRecord.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/CodeGenData

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Core
  Support
  Object
  )
