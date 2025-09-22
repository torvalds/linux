# This provides the implementation of MVT and LLT.
# Be careful to append deps on this, since Targets' tablegens depend on this.
add_llvm_component_library(LLVMCodeGenTypes
  LowLevelType.cpp

  DEPENDS
  vt_gen

  LINK_COMPONENTS
  Support
  )
