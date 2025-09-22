add_llvm_component_group(ARC)

set(LLVM_TARGET_DEFINITIONS ARC.td)

tablegen(LLVM ARCGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM ARCGenCallingConv.inc -gen-callingconv)
tablegen(LLVM ARCGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM ARCGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM ARCGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM ARCGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM ARCGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(ARCCommonTableGen)

add_llvm_target(ARCCodeGen
  ARCAsmPrinter.cpp
  ARCBranchFinalize.cpp
  ARCExpandPseudos.cpp
  ARCFrameLowering.cpp
  ARCInstrInfo.cpp
  ARCISelDAGToDAG.cpp
  ARCISelLowering.cpp
  ARCMachineFunctionInfo.cpp
  ARCMCInstLower.cpp
  ARCOptAddrMode.cpp
  ARCRegisterInfo.cpp
  ARCSubtarget.cpp
  ARCTargetMachine.cpp

  LINK_COMPONENTS
  ARCDesc
  ARCInfo
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target
  TargetParser
  TransformUtils

  ADD_TO_COMPONENT
  ARC
  )

add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
