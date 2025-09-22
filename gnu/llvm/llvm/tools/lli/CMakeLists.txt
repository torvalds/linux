if ( LLVM_INCLUDE_UTILS )
  add_subdirectory(ChildTarget)
endif()

set(LLVM_LINK_COMPONENTS
  CodeGen
  Core
  ExecutionEngine
  IRReader
  Interpreter
  JITLink
  MC
  MCJIT
  Object
  OrcJIT
  OrcDebugging
  OrcShared
  OrcTargetProcess
  Passes
  RuntimeDyld
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  native
  )

if( LLVM_USE_OPROFILE )
  set(LLVM_LINK_COMPONENTS
    ${LLVM_LINK_COMPONENTS}
    OProfileJIT
    )
endif( LLVM_USE_OPROFILE )

if( LLVM_USE_INTEL_JITEVENTS )
  set(LLVM_LINK_COMPONENTS
    ${LLVM_LINK_COMPONENTS}
    DebugInfoDWARF
    IntelJITEvents
    Object
    )
endif( LLVM_USE_INTEL_JITEVENTS )

if( LLVM_USE_PERF )
  set(LLVM_LINK_COMPONENTS
    ${LLVM_LINK_COMPONENTS}
    DebugInfoDWARF
    PerfJITEvents
    Object
    )
endif( LLVM_USE_PERF )

add_llvm_tool(lli
  lli.cpp

  DEPENDS
  intrinsics_gen
  )

export_executable_symbols(lli)
