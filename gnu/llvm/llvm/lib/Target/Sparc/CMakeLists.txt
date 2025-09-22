add_llvm_component_group(Sparc)

set(LLVM_TARGET_DEFINITIONS Sparc.td)

tablegen(LLVM SparcGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM SparcGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM SparcGenCallingConv.inc -gen-callingconv)
tablegen(LLVM SparcGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM SparcGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM SparcGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM SparcGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM SparcGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM SparcGenSearchableTables.inc -gen-searchable-tables)
tablegen(LLVM SparcGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(SparcCommonTableGen)

add_llvm_target(SparcCodeGen
  DelaySlotFiller.cpp
  LeonPasses.cpp
  SparcAsmPrinter.cpp
  SparcInstrInfo.cpp
  SparcISelDAGToDAG.cpp
  SparcISelLowering.cpp
  SparcFrameLowering.cpp
  SparcMachineFunctionInfo.cpp
  SparcRegisterInfo.cpp
  SparcSubtarget.cpp
  SparcTargetMachine.cpp
  SparcMCInstLower.cpp
  SparcTargetObjectFile.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  SparcDesc
  SparcInfo
  Support
  Target
  TargetParser

  ADD_TO_COMPONENT
  Sparc
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
