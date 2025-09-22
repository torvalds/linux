add_llvm_component_group(NVPTX)

set(LLVM_TARGET_DEFINITIONS NVPTX.td)

tablegen(LLVM NVPTXGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM NVPTXGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM NVPTXGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM NVPTXGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM NVPTXGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(NVPTXCommonTableGen)

set(NVPTXCodeGen_sources
  NVPTXAliasAnalysis.cpp
  NVPTXAllocaHoisting.cpp
  NVPTXAtomicLower.cpp
  NVPTXAsmPrinter.cpp
  NVPTXAssignValidGlobalNames.cpp
  NVPTXFrameLowering.cpp
  NVPTXGenericToNVVM.cpp
  NVPTXISelDAGToDAG.cpp
  NVPTXISelLowering.cpp
  NVPTXImageOptimizer.cpp
  NVPTXInstrInfo.cpp
  NVPTXLowerAggrCopies.cpp
  NVPTXLowerArgs.cpp
  NVPTXLowerAlloca.cpp
  NVPTXLowerUnreachable.cpp
  NVPTXPeephole.cpp
  NVPTXMCExpr.cpp
  NVPTXPrologEpilogPass.cpp
  NVPTXRegisterInfo.cpp
  NVPTXReplaceImageHandles.cpp
  NVPTXSubtarget.cpp
  NVPTXTargetMachine.cpp
  NVPTXTargetTransformInfo.cpp
  NVPTXUtilities.cpp
  NVVMIntrRange.cpp
  NVVMReflect.cpp
  NVPTXProxyRegErasure.cpp
  NVPTXCtorDtorLowering.cpp
  )

add_llvm_target(NVPTXCodeGen
  ${NVPTXCodeGen_sources}

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  IPO
  MC
  NVPTXDesc
  NVPTXInfo
  Scalar
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils
  Vectorize

  ADD_TO_COMPONENT
  NVPTX
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
