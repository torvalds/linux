add_llvm_component_group(PowerPC HAS_JIT)

set(LLVM_TARGET_DEFINITIONS PPC.td)

tablegen(LLVM PPCGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM PPCGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM PPCGenCallingConv.inc -gen-callingconv)
tablegen(LLVM PPCGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM PPCGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM PPCGenFastISel.inc -gen-fast-isel)
tablegen(LLVM PPCGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM PPCGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM PPCGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM PPCGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM PPCGenExegesis.inc -gen-exegesis)
tablegen(LLVM PPCGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM PPCGenGlobalISel.inc -gen-global-isel)

add_public_tablegen_target(PowerPCCommonTableGen)

add_llvm_target(PowerPCCodeGen
  GISel/PPCInstructionSelector.cpp
  PPCBoolRetToInt.cpp
  PPCAsmPrinter.cpp
  PPCBranchSelector.cpp
  PPCBranchCoalescing.cpp
  PPCCallingConv.cpp
  PPCCCState.cpp
  PPCCTRLoops.cpp
  PPCCTRLoopsVerify.cpp
  PPCExpandAtomicPseudoInsts.cpp
  PPCHazardRecognizers.cpp
  PPCInstrInfo.cpp
  PPCISelDAGToDAG.cpp
  PPCISelLowering.cpp
  PPCEarlyReturn.cpp
  PPCFastISel.cpp
  PPCFrameLowering.cpp
  PPCLoopInstrFormPrep.cpp
  PPCMCInstLower.cpp
  PPCMachineFunctionInfo.cpp
  PPCMachineScheduler.cpp
  PPCMacroFusion.cpp
  PPCMergeStringPool.cpp
  PPCMIPeephole.cpp
  PPCRegisterInfo.cpp
  PPCReturnProtectorLowering.cpp
  PPCSubtarget.cpp
  PPCTargetMachine.cpp
  PPCTargetObjectFile.cpp
  PPCTargetTransformInfo.cpp
  PPCTOCRegDeps.cpp
  PPCTLSDynamicCall.cpp
  PPCVSXCopy.cpp
  PPCReduceCRLogicals.cpp
  PPCVSXFMAMutate.cpp
  PPCVSXSwapRemoval.cpp
  PPCExpandISEL.cpp
  PPCPreEmitPeephole.cpp
  PPCLowerMASSVEntries.cpp
  PPCGenScalarMASSEntries.cpp
  GISel/PPCCallLowering.cpp
  GISel/PPCRegisterBankInfo.cpp
  GISel/PPCLegalizerInfo.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  BinaryFormat
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  PowerPCDesc
  PowerPCInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  PowerPC
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
