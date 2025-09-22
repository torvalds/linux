add_llvm_component_group(Mips HAS_JIT)

set(LLVM_TARGET_DEFINITIONS Mips.td)

tablegen(LLVM MipsGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM MipsGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MipsGenCallingConv.inc -gen-callingconv)
tablegen(LLVM MipsGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MipsGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM MipsGenFastISel.inc -gen-fast-isel)
tablegen(LLVM MipsGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM MipsGenPostLegalizeGICombiner.inc -gen-global-isel-combiner
              -combiners="MipsPostLegalizerCombiner")
tablegen(LLVM MipsGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MipsGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM MipsGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM MipsGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM MipsGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MipsGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM MipsGenExegesis.inc -gen-exegesis)

add_public_tablegen_target(MipsCommonTableGen)

add_llvm_target(MipsCodeGen
  Mips16FrameLowering.cpp
  Mips16HardFloat.cpp
  Mips16HardFloatInfo.cpp
  Mips16InstrInfo.cpp
  Mips16ISelDAGToDAG.cpp
  Mips16ISelLowering.cpp
  Mips16RegisterInfo.cpp
  MipsAnalyzeImmediate.cpp
  MipsAsmPrinter.cpp
  MipsCallLowering.cpp
  MipsCCState.cpp
  MipsConstantIslandPass.cpp
  MipsDelaySlotFiller.cpp
  MipsExpandPseudo.cpp
  MipsFastISel.cpp
  MipsInstrInfo.cpp
  MipsInstructionSelector.cpp
  MipsISelDAGToDAG.cpp
  MipsISelLowering.cpp
  MipsFrameLowering.cpp
  MipsLegalizerInfo.cpp
  MipsLoongson2FBTBFix.cpp
  MipsBranchExpansion.cpp
  MipsMCInstLower.cpp
  MipsMachineFunction.cpp
  MipsModuleISelDAGToDAG.cpp
  MipsOptimizePICCall.cpp
  MipsOs16.cpp
  MipsPreLegalizerCombiner.cpp
  MipsPostLegalizerCombiner.cpp
  MipsRegisterBankInfo.cpp
  MipsRegisterInfo.cpp
  MipsReturnProtectorLowering.cpp
  MipsSEFrameLowering.cpp
  MipsSEInstrInfo.cpp
  MipsSEISelDAGToDAG.cpp
  MipsSEISelLowering.cpp
  MipsSERegisterInfo.cpp
  MipsSubtarget.cpp
  MipsTargetMachine.cpp
  MipsTargetObjectFile.cpp
  MipsTargetTransformInfo.cpp
  MicroMipsSizeReduction.cpp
  MipsMulMulBugPass.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  MC
  MipsDesc
  MipsInfo
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  Mips
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
