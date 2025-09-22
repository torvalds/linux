# This tool is excluded from the CMake build if Intel JIT events are disabled.

link_directories( ${LLVM_INTEL_JITEVENTS_LIBDIR} )
include_directories( ${LLVM_INTEL_JITEVENTS_INCDIR} )

set(LLVM_LINK_COMPONENTS
  asmparser
  bitreader
  DebugInfoDWARF
  inteljitevents
  interpreter
  irreader
  mcjit
  nativecodegen
  object
  selectiondag
  Support
  TargetParser
  ExecutionEngine
  RuntimeDyld
  Core
  )

add_llvm_tool(llvm-jitlistener
  llvm-jitlistener.cpp
  )
