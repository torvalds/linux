
set(LLVM_EXEGESIS_TARGETS)
if (LLVM_TARGETS_TO_BUILD MATCHES "X86")
  list(APPEND LLVM_EXEGESIS_TARGETS "X86")
endif()
if (LLVM_TARGETS_TO_BUILD MATCHES "AArch64")
  list(APPEND LLVM_EXEGESIS_TARGETS "AArch64")
endif()
if (LLVM_TARGETS_TO_BUILD MATCHES "PowerPC")
  list(APPEND LLVM_EXEGESIS_TARGETS "PowerPC")
endif()
if (LLVM_TARGETS_TO_BUILD MATCHES "Mips")
  list(APPEND LLVM_EXEGESIS_TARGETS "Mips")
endif()

set(LLVM_EXEGESIS_TARGETS ${LLVM_EXEGESIS_TARGETS} PARENT_SCOPE)

foreach(t ${LLVM_EXEGESIS_TARGETS})
  add_subdirectory(${t})
endforeach()

set(LLVM_LINK_COMPONENTS
  Analysis
  CodeGen
  CodeGenTypes
  Core
  ExecutionEngine
  GlobalISel
  MC
  MCA
  MCDisassembler
  MCParser
  Object
  ObjectYAML
  OrcJIT
  RuntimeDyld
  Support
  TargetParser
  )

set(libs)
if(LLVM_ENABLE_LIBPFM AND HAVE_LIBPFM)
  list(APPEND libs pfm)
endif()
if(HAVE_LIBRT)
  list(APPEND libs rt)
endif()

add_llvm_library(LLVMExegesis
  DISABLE_LLVM_LINK_LLVM_DYLIB
  STATIC
  Analysis.cpp
  Assembler.cpp
  BenchmarkResult.cpp
  BenchmarkRunner.cpp
  Clustering.cpp
  CodeTemplate.cpp
  DisassemblerHelper.cpp
  Error.cpp
  LatencyBenchmarkRunner.cpp
  LlvmState.cpp
  MCInstrDescView.cpp
  ParallelSnippetGenerator.cpp
  PerfHelper.cpp
  RegisterAliasing.cpp
  RegisterValue.cpp
  ResultAggregator.cpp
  SchedClassResolution.cpp
  SerialSnippetGenerator.cpp
  SnippetFile.cpp
  SnippetGenerator.cpp
  SnippetRepetitor.cpp
  SubprocessMemory.cpp
  Target.cpp
  UopsBenchmarkRunner.cpp
  ValidationEvent.cpp

  LINK_LIBS ${libs}

  DEPENDS
  intrinsics_gen
  )
