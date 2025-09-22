add_llvm_component_library(LLVMDebugInfoBTF
  BTFParser.cpp
  BTFContext.cpp
  ADDITIONAL_HEADER_DIRS
  "${LLVM_MAIN_INCLUDE_DIR}/llvm/DebugInfo/BTF"

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Support
  )
