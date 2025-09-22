add_llvm_component_group(XCore)

set(LLVM_TARGET_DEFINITIONS XCore.td)

tablegen(LLVM XCoreGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM XCoreGenCallingConv.inc -gen-callingconv)
tablegen(LLVM XCoreGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM XCoreGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM XCoreGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM XCoreGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM XCoreGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(XCoreCommonTableGen)

add_llvm_target(XCoreCodeGen
  XCoreAsmPrinter.cpp
  XCoreFrameLowering.cpp
  XCoreInstrInfo.cpp
  XCoreISelDAGToDAG.cpp
  XCoreISelLowering.cpp
  XCoreLowerThreadLocal.cpp
  XCoreMachineFunctionInfo.cpp
  XCoreMCInstLower.cpp
  XCoreRegisterInfo.cpp
  XCoreSubtarget.cpp
  XCoreTargetMachine.cpp
  XCoreTargetObjectFile.cpp
  XCoreSelectionDAGInfo.cpp
  XCoreFrameToArgsOffsetElim.cpp

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
  TargetParser
  TransformUtils
  XCoreDesc
  XCoreInfo

  ADD_TO_COMPONENT
  XCore
  )

add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
