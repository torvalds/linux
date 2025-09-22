add_llvm_component_group(AVR)

set(LLVM_TARGET_DEFINITIONS AVR.td)

tablegen(LLVM AVRGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM AVRGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM AVRGenCallingConv.inc -gen-callingconv)
tablegen(LLVM AVRGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM AVRGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM AVRGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM AVRGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM AVRGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM AVRGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(AVRCommonTableGen)

add_llvm_target(AVRCodeGen
  AVRAsmPrinter.cpp
  AVRExpandPseudoInsts.cpp
  AVRFrameLowering.cpp
  AVRInstrInfo.cpp
  AVRISelDAGToDAG.cpp
  AVRISelLowering.cpp
  AVRMCInstLower.cpp
  AVRRegisterInfo.cpp
  AVRShiftExpand.cpp
  AVRSubtarget.cpp
  AVRTargetMachine.cpp
  AVRTargetObjectFile.cpp

  DEPENDS
  intrinsics_gen

  LINK_COMPONENTS
  AVRDesc
  AVRInfo
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  AVR
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
