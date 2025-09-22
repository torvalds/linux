add_llvm_component_group(AMDGPU)

set(LLVM_TARGET_DEFINITIONS AMDGPU.td)

tablegen(LLVM AMDGPUGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM AMDGPUGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM AMDGPUGenCallingConv.inc -gen-callingconv)
tablegen(LLVM AMDGPUGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM AMDGPUGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM AMDGPUGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM AMDGPUGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM AMDGPUGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM AMDGPUGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM AMDGPUGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM AMDGPUGenSearchableTables.inc -gen-searchable-tables)
tablegen(LLVM AMDGPUGenSubtargetInfo.inc -gen-subtarget)

set(LLVM_TARGET_DEFINITIONS AMDGPUGISel.td)
tablegen(LLVM AMDGPUGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM AMDGPUGenPreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="AMDGPUPreLegalizerCombiner")
tablegen(LLVM AMDGPUGenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="AMDGPUPostLegalizerCombiner")
tablegen(LLVM AMDGPUGenRegBankGICombiner.inc -gen-global-isel-combiner
              -combiners="AMDGPURegBankCombiner")

set(LLVM_TARGET_DEFINITIONS R600.td)
tablegen(LLVM R600GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM R600GenCallingConv.inc -gen-callingconv)
tablegen(LLVM R600GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM R600GenDFAPacketizer.inc -gen-dfa-packetizer)
tablegen(LLVM R600GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM R600GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM R600GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM R600GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(AMDGPUCommonTableGen)

set(LLVM_TARGET_DEFINITIONS InstCombineTables.td)
tablegen(LLVM InstCombineTables.inc -gen-searchable-tables)
add_public_tablegen_target(InstCombineTableGen)

add_llvm_target(AMDGPUCodeGen
  AMDGPUAliasAnalysis.cpp
  AMDGPUAlwaysInlinePass.cpp
  AMDGPUAnnotateKernelFeatures.cpp
  AMDGPUAnnotateUniformValues.cpp
  AMDGPUArgumentUsageInfo.cpp
  AMDGPUAsmPrinter.cpp
  AMDGPUAtomicOptimizer.cpp
  AMDGPUAttributor.cpp
  AMDGPUCallLowering.cpp
  AMDGPUCodeGenPassBuilder.cpp
  AMDGPUCodeGenPrepare.cpp
  AMDGPUCombinerHelper.cpp
  AMDGPUCtorDtorLowering.cpp
  AMDGPUExportClustering.cpp
  AMDGPUFrameLowering.cpp
  AMDGPUGlobalISelDivergenceLowering.cpp
  AMDGPUGlobalISelUtils.cpp
  AMDGPUHSAMetadataStreamer.cpp
  AMDGPUInsertDelayAlu.cpp
  AMDGPUInstCombineIntrinsic.cpp
  AMDGPUInstrInfo.cpp
  AMDGPUInstructionSelector.cpp
  AMDGPUISelDAGToDAG.cpp
  AMDGPUISelLowering.cpp
  AMDGPULateCodeGenPrepare.cpp
  AMDGPULegalizerInfo.cpp
  AMDGPULibCalls.cpp
  AMDGPUImageIntrinsicOptimizer.cpp
  AMDGPULibFunc.cpp
  AMDGPULowerBufferFatPointers.cpp
  AMDGPULowerKernelArguments.cpp
  AMDGPULowerKernelAttributes.cpp
  AMDGPULowerModuleLDSPass.cpp
  AMDGPUMachineCFGStructurizer.cpp
  AMDGPUMachineFunction.cpp
  AMDGPUMachineModuleInfo.cpp
  AMDGPUMacroFusion.cpp
  AMDGPUMCInstLower.cpp
  AMDGPUIGroupLP.cpp
  AMDGPUInsertSingleUseVDST.cpp
  AMDGPUMarkLastScratchLoad.cpp
  AMDGPUMIRFormatter.cpp
  AMDGPUOpenCLEnqueuedBlockLowering.cpp
  AMDGPUPerfHintAnalysis.cpp
  AMDGPUPostLegalizerCombiner.cpp
  AMDGPUPreLegalizerCombiner.cpp
  AMDGPUPrintfRuntimeBinding.cpp
  AMDGPUPromoteAlloca.cpp
  AMDGPUPromoteKernelArguments.cpp
  AMDGPURegBankCombiner.cpp
  AMDGPURegBankSelect.cpp
  AMDGPURegisterBankInfo.cpp
  AMDGPURemoveIncompatibleFunctions.cpp
  AMDGPUResourceUsageAnalysis.cpp
  AMDGPURewriteOutArguments.cpp
  AMDGPURewriteUndefForPHI.cpp
  AMDGPUSetWavePriority.cpp
  AMDGPUSplitModule.cpp
  AMDGPUSubtarget.cpp
  AMDGPUTargetMachine.cpp
  AMDGPUTargetObjectFile.cpp
  AMDGPUTargetTransformInfo.cpp
  AMDGPUUnifyDivergentExitNodes.cpp
  AMDGPUUnifyMetadata.cpp
  R600MachineCFGStructurizer.cpp
  GCNCreateVOPD.cpp
  GCNDPPCombine.cpp
  GCNHazardRecognizer.cpp
  GCNILPSched.cpp
  GCNIterativeScheduler.cpp
  GCNMinRegStrategy.cpp
  GCNNSAReassign.cpp
  GCNPreRAOptimizations.cpp
  GCNPreRALongBranchReg.cpp
  GCNRegPressure.cpp
  GCNRewritePartialRegUses.cpp
  GCNSchedStrategy.cpp
  GCNVOPDUtils.cpp
  R600AsmPrinter.cpp
  R600ClauseMergePass.cpp
  R600CodeGenPassBuilder.cpp
  R600ControlFlowFinalizer.cpp
  R600EmitClauseMarkers.cpp
  R600ExpandSpecialInstrs.cpp
  R600FrameLowering.cpp
  R600InstrInfo.cpp
  R600ISelDAGToDAG.cpp
  R600ISelLowering.cpp
  R600MachineFunctionInfo.cpp
  R600MachineScheduler.cpp
  R600MCInstLower.cpp
  R600OpenCLImageTypeLoweringPass.cpp
  R600OptimizeVectorRegisters.cpp
  R600Packetizer.cpp
  R600RegisterInfo.cpp
  R600Subtarget.cpp
  R600TargetMachine.cpp
  R600TargetTransformInfo.cpp
  SIAnnotateControlFlow.cpp
  SIFixSGPRCopies.cpp
  SIFixVGPRCopies.cpp
  SIFoldOperands.cpp
  SIFormMemoryClauses.cpp
  SIFrameLowering.cpp
  SIInsertHardClauses.cpp
  SIInsertWaitcnts.cpp
  SIInstrInfo.cpp
  SIISelLowering.cpp
  SILateBranchLowering.cpp
  SILoadStoreOptimizer.cpp
  SILowerControlFlow.cpp
  SILowerI1Copies.cpp
  SILowerWWMCopies.cpp
  SILowerSGPRSpills.cpp
  SIMachineFunctionInfo.cpp
  SIMachineScheduler.cpp
  SIMemoryLegalizer.cpp
  SIModeRegister.cpp
  SIModeRegisterDefaults.cpp
  SIOptimizeExecMasking.cpp
  SIOptimizeExecMaskingPreRA.cpp
  SIOptimizeVGPRLiveRange.cpp
  SIPeepholeSDWA.cpp
  SIPostRABundler.cpp
  SIPreAllocateWWMRegs.cpp
  SIPreEmitPeephole.cpp
  SIProgramInfo.cpp
  SIRegisterInfo.cpp
  SIShrinkInstructions.cpp
  SIWholeQuadMode.cpp

  LINK_COMPONENTS
  AMDGPUDesc
  AMDGPUInfo
  AMDGPUUtils
  Analysis
  AsmPrinter
  BinaryFormat
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  HipStdPar
  IPO
  IRPrinter
  MC
  MIRParser
  Passes
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize

  ADD_TO_COMPONENT
  AMDGPU
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCA)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(Utils)
