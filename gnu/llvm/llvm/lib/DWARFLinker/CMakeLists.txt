add_llvm_component_library(LLVMDWARFLinker
  DWARFLinkerBase.cpp
  Utils.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DWARFLinker
  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  BinaryFormat
  CodeGen
  DebugInfoDWARF
  Object
  Support
  )

add_subdirectory(Classic)
add_subdirectory(Parallel)
