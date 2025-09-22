add_llvm_component_group(SPIRV)

set(LLVM_TARGET_DEFINITIONS SPIRV.td)

tablegen(LLVM SPIRVGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM SPIRVGenGlobalISel.inc -gen-global-isel)
tablegen(LLVM SPIRVGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM SPIRVGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM SPIRVGenRegisterBank.inc -gen-register-bank)
tablegen(LLVM SPIRVGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM SPIRVGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM SPIRVGenTables.inc -gen-searchable-tables)

add_public_tablegen_target(SPIRVCommonTableGen)

add_llvm_target(SPIRVCodeGen
  SPIRVAsmPrinter.cpp
  SPIRVBuiltins.cpp
  SPIRVCallLowering.cpp
  SPIRVInlineAsmLowering.cpp
  SPIRVCommandLine.cpp
  SPIRVDuplicatesTracker.cpp
  SPIRVEmitIntrinsics.cpp
  SPIRVGlobalRegistry.cpp
  SPIRVInstrInfo.cpp
  SPIRVInstructionSelector.cpp
  SPIRVStripConvergentIntrinsics.cpp
  SPIRVMergeRegionExitTargets.cpp
  SPIRVISelLowering.cpp
  SPIRVLegalizerInfo.cpp
  SPIRVMCInstLower.cpp
  SPIRVMetadata.cpp
  SPIRVModuleAnalysis.cpp
  SPIRVPreLegalizer.cpp
  SPIRVPostLegalizer.cpp
  SPIRVPrepareFunctions.cpp
  SPIRVRegisterBankInfo.cpp
  SPIRVRegisterInfo.cpp
  SPIRVRegularizer.cpp
  SPIRVSubtarget.cpp
  SPIRVTargetMachine.cpp
  SPIRVUtils.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  Demangle
  GlobalISel
  SPIRVAnalysis
  MC
  SPIRVDesc
  SPIRVInfo
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  SPIRV
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(Analysis)
