add_llvm_component_group(RISCV)

set(LLVM_TARGET_DEFINITIONS RISCV.td)

tablegen(LLVM RISCVGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM RISCVGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM RISCVGenCompressInstEmitter.inc -gen-compress-inst-emitter)
tablegen(LLVM RISCVGenMacroFusion.inc -gen-macro-fusion-pred)
tablegen(LLVM RISCVGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM RISCVGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM RISCVGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM RISCVGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM RISCVGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM RISCVGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM RISCVGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM RISCVGenSearchableTables.inc -gen-searchable-tables)
tablegen(LLVM RISCVGenSubtargetInfo.inc -gen-subtarget)

set(LLVM_TARGET_DEFINITIONS RISCVGISel.td)
tablegen(LLVM RISCVGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM RISCVGenO0PreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="RISCVO0PreLegalizerCombiner")
tablegen(LLVM RISCVGenPreLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="RISCVPreLegalizerCombiner")
tablegen(LLVM RISCVGenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="RISCVPostLegalizerCombiner")

add_public_tablegen_target(RISCVCommonTableGen)

add_llvm_target(RISCVCodeGen
  RISCVAsmPrinter.cpp
  RISCVCodeGenPrepare.cpp
  RISCVDeadRegisterDefinitions.cpp
  RISCVMakeCompressible.cpp
  RISCVExpandAtomicPseudoInsts.cpp
  RISCVExpandPseudoInsts.cpp
  RISCVFrameLowering.cpp
  RISCVGatherScatterLowering.cpp
  RISCVInsertVSETVLI.cpp
  RISCVInsertReadWriteCSR.cpp
  RISCVInsertWriteVXRM.cpp
  RISCVInstrInfo.cpp
  RISCVISelDAGToDAG.cpp
  RISCVISelLowering.cpp
  RISCVMachineFunctionInfo.cpp
  RISCVMergeBaseOffset.cpp
  RISCVOptWInstrs.cpp
  RISCVPostRAExpandPseudoInsts.cpp
  RISCVRedundantCopyElimination.cpp
  RISCVMoveMerger.cpp
  RISCVPushPopOptimizer.cpp
  RISCVRegisterInfo.cpp
  RISCVSubtarget.cpp
  RISCVTargetMachine.cpp
  RISCVTargetObjectFile.cpp
  RISCVTargetTransformInfo.cpp
  RISCVVectorPeephole.cpp
  GISel/RISCVCallLowering.cpp
  GISel/RISCVInstructionSelector.cpp
  GISel/RISCVLegalizerInfo.cpp
  GISel/RISCVPostLegalizerCombiner.cpp
  GISel/RISCVO0PreLegalizerCombiner.cpp
  GISel/RISCVPreLegalizerCombiner.cpp
  GISel/RISCVRegisterBankInfo.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  IPO
  MC
  RISCVDesc
  RISCVInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize

  ADD_TO_COMPONENT
  RISCV
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(MCA)
add_subdirectory(TargetInfo)
