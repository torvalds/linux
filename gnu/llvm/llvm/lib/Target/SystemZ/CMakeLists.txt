add_llvm_component_group(SystemZ HAS_JIT)

set(LLVM_TARGET_DEFINITIONS SystemZ.td)

tablegen(LLVM SystemZGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM SystemZGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM SystemZGenCallingConv.inc -gen-callingconv)
tablegen(LLVM SystemZGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM SystemZGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM SystemZGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM SystemZGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM SystemZGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM SystemZGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(SystemZCommonTableGen)

add_llvm_target(SystemZCodeGen
  SystemZAsmPrinter.cpp
  SystemZCallingConv.cpp
  SystemZConstantPoolValue.cpp
  SystemZCopyPhysRegs.cpp
  SystemZElimCompare.cpp
  SystemZFrameLowering.cpp
  SystemZHazardRecognizer.cpp
  SystemZISelDAGToDAG.cpp
  SystemZISelLowering.cpp
  SystemZInstrInfo.cpp
  SystemZLDCleanup.cpp
  SystemZLongBranch.cpp
  SystemZMachineFunctionInfo.cpp
  SystemZMachineScheduler.cpp
  SystemZMCInstLower.cpp
  SystemZPostRewrite.cpp
  SystemZRegisterInfo.cpp
  SystemZSelectionDAGInfo.cpp
  SystemZShortenInst.cpp
  SystemZSubtarget.cpp
  SystemZTargetMachine.cpp
  SystemZTargetObjectFile.cpp
  SystemZTargetTransformInfo.cpp
  SystemZTDC.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  Scalar
  SelectionDAG
  Support
  SystemZDesc
  SystemZInfo
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  SystemZ
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
