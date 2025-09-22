set(LLVM_LINK_COMPONENTS
  OrcJIT
  OrcShared
  OrcTargetProcess
  Support
  )

add_llvm_utility(lli-child-target
  ChildTarget.cpp

  DEPENDS
  intrinsics_gen
)

export_executable_symbols(lli-child-target)
