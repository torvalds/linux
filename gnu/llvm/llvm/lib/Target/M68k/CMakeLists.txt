add_llvm_component_group(M68k)

set(LLVM_TARGET_DEFINITIONS M68k.td)

tablegen(LLVM M68kGenGlobalISel.inc       -gen-global-isel)
tablegen(LLVM M68kGenRegisterInfo.inc     -gen-register-info)
tablegen(LLVM M68kGenRegisterBank.inc     -gen-register-bank)
tablegen(LLVM M68kGenInstrInfo.inc        -gen-instr-info)
tablegen(LLVM M68kGenSubtargetInfo.inc    -gen-subtarget)
tablegen(LLVM M68kGenMCCodeEmitter.inc    -gen-emitter)
tablegen(LLVM M68kGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM M68kGenDAGISel.inc          -gen-dag-isel)
tablegen(LLVM M68kGenCallingConv.inc      -gen-callingconv)
tablegen(LLVM M68kGenAsmWriter.inc        -gen-asm-writer)
tablegen(LLVM M68kGenAsmMatcher.inc       -gen-asm-matcher)
tablegen(LLVM M68kGenDisassemblerTable.inc -gen-disassembler)

add_public_tablegen_target(M68kCommonTableGen)

add_llvm_target(M68kCodeGen
  GISel/M68kCallLowering.cpp
  GISel/M68kInstructionSelector.cpp
  GISel/M68kLegalizerInfo.cpp
  GISel/M68kRegisterBankInfo.cpp
  M68kAsmPrinter.cpp
  M68kCollapseMOVEMPass.cpp
  M68kExpandPseudo.cpp
  M68kFrameLowering.cpp
  M68kInstrInfo.cpp
  M68kISelLowering.cpp
  M68kISelDAGToDAG.cpp
  M68kMachineFunction.cpp
  M68kMCInstLower.cpp
  M68kRegisterInfo.cpp
  M68kSubtarget.cpp
  M68kTargetMachine.cpp
  M68kTargetObjectFile.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  GlobalISel
  M68kDesc
  M68kInfo
  MC
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  M68k
  )

add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
