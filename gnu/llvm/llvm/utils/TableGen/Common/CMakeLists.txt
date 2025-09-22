# The common library is similar to the basic library except it can
# depend on vt_gen.
#
# This library contains the bulk of the supporting code for all
# TableGen backends. It's split off as a separate library to
# allow unit-testing those components.

set(LLVM_LINK_COMPONENTS
  Support
  TableGen
  )

add_llvm_library(LLVMTableGenCommon STATIC OBJECT EXCLUDE_FROM_ALL
  GlobalISel/CodeExpander.cpp
  GlobalISel/CombinerUtils.cpp
  GlobalISel/CXXPredicates.cpp
  GlobalISel/GlobalISelMatchTable.cpp
  GlobalISel/GlobalISelMatchTableExecutorEmitter.cpp
  GlobalISel/PatternParser.cpp
  GlobalISel/Patterns.cpp

  AsmWriterInst.cpp
  CodeGenDAGPatterns.cpp
  CodeGenHwModes.cpp
  CodeGenInstAlias.cpp
  CodeGenInstruction.cpp
  CodeGenRegisters.cpp
  CodeGenSchedule.cpp
  CodeGenTarget.cpp
  DAGISelMatcher.cpp
  InfoByHwMode.cpp
  OptEmitter.cpp
  PredicateExpander.cpp
  SubtargetFeatureInfo.cpp
  Types.cpp
  VarLenCodeEmitterGen.cpp

  LINK_LIBS
  LLVMTableGenBasic

  DEPENDS
  vt_gen
  intrinsics_gen
  )

# Users may include its headers as "Common/*.h"
target_include_directories(LLVMTableGenCommon
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
  )
