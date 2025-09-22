add_llvm_component_group(VE)

set(LLVM_TARGET_DEFINITIONS VE.td)

tablegen(LLVM VEGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM VEGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM VEGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM VEGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM VEGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM VEGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM VEGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM VEGenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM VEGenCallingConv.inc -gen-callingconv)
add_public_tablegen_target(VECommonTableGen)

add_llvm_target(VECodeGen
  LVLGen.cpp
  VEAsmPrinter.cpp
  VECustomDAG.cpp
  VEFrameLowering.cpp
  VEISelDAGToDAG.cpp
  VEISelLowering.cpp
  VEInstrInfo.cpp
  VEMachineFunctionInfo.cpp
  VEMCInstLower.cpp
  VERegisterInfo.cpp
  VESubtarget.cpp
  VETargetMachine.cpp
  VVPISelLowering.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TransformUtils
  VEDesc
  VEInfo

  ADD_TO_COMPONENT
  VE
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
