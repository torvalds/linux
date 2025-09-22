set(LLVM_LINK_COMPONENTS
  Core
  ExecutionEngine
  IRReader
  JITLink
  OrcDebugging
  OrcJIT
  OrcShared
  OrcTargetProcess
  Support
  TargetParser
  nativecodegen
  )

if (LLVM_INCLUDE_UTILS)
  add_llvm_example(LLJITWithRemoteDebugging
    LLJITWithRemoteDebugging.cpp
    RemoteJITUtils.cpp

    DEPENDS
      llvm-jitlink-executor
  )

  export_executable_symbols(LLJITWithRemoteDebugging)
endif()
