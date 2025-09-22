add_llvm_component_group(WebAssembly)

set(LLVM_TARGET_DEFINITIONS WebAssembly.td)

tablegen(LLVM WebAssemblyGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM WebAssemblyGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM WebAssemblyGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM WebAssemblyGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM WebAssemblyGenFastISel.inc -gen-fast-isel)
tablegen(LLVM WebAssemblyGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM WebAssemblyGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM WebAssemblyGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM WebAssemblyGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(WebAssemblyCommonTableGen)

add_llvm_target(WebAssemblyCodeGen
  WebAssemblyAddMissingPrototypes.cpp
  WebAssemblyArgumentMove.cpp
  WebAssemblyAsmPrinter.cpp
  WebAssemblyCFGStackify.cpp
  WebAssemblyCleanCodeAfterTrap.cpp
  WebAssemblyCFGSort.cpp
  WebAssemblyDebugFixup.cpp
  WebAssemblyDebugValueManager.cpp
  WebAssemblyLateEHPrepare.cpp
  WebAssemblyExceptionInfo.cpp
  WebAssemblyExplicitLocals.cpp
  WebAssemblyFastISel.cpp
  WebAssemblyFixBrTableDefaults.cpp
  WebAssemblyFixIrreducibleControlFlow.cpp
  WebAssemblyFixFunctionBitcasts.cpp
  WebAssemblyFrameLowering.cpp
  WebAssemblyISelDAGToDAG.cpp
  WebAssemblyISelLowering.cpp
  WebAssemblyInstrInfo.cpp
  WebAssemblyLowerBrUnless.cpp
  WebAssemblyLowerEmscriptenEHSjLj.cpp
  WebAssemblyLowerRefTypesIntPtrConv.cpp
  WebAssemblyMachineFunctionInfo.cpp
  WebAssemblyMCInstLower.cpp
  WebAssemblyMCLowerPrePass.cpp
  WebAssemblyNullifyDebugValueLists.cpp
  WebAssemblyOptimizeLiveIntervals.cpp
  WebAssemblyOptimizeReturned.cpp
  WebAssemblyPeephole.cpp
  WebAssemblyRefTypeMem2Local.cpp
  WebAssemblyRegisterInfo.cpp
  WebAssemblyRegColoring.cpp
  WebAssemblyRegNumbering.cpp
  WebAssemblyRegStackify.cpp
  WebAssemblyReplacePhysRegs.cpp
  WebAssemblyRuntimeLibcallSignatures.cpp
  WebAssemblySelectionDAGInfo.cpp
  WebAssemblySetP2AlignOperands.cpp
  WebAssemblySortRegion.cpp
  WebAssemblyMemIntrinsicResults.cpp
  WebAssemblySubtarget.cpp
  WebAssemblyTargetMachine.cpp
  WebAssemblyTargetObjectFile.cpp
  WebAssemblyTargetTransformInfo.cpp
  WebAssemblyUtilities.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  BinaryFormat
  CodeGen
  CodeGenTypes
  Core
  MC
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  WebAssemblyDesc
  WebAssemblyInfo
  WebAssemblyUtils

  ADD_TO_COMPONENT
  WebAssembly
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(Utils)
