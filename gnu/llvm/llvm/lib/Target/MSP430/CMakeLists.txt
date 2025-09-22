add_llvm_component_group(MSP430)

set(LLVM_TARGET_DEFINITIONS MSP430.td)

tablegen(LLVM MSP430GenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM MSP430GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MSP430GenCallingConv.inc -gen-callingconv)
tablegen(LLVM MSP430GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MSP430GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM MSP430GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MSP430GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM MSP430GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MSP430GenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(MSP430CommonTableGen)

add_llvm_target(MSP430CodeGen
  MSP430BranchSelector.cpp
  MSP430ISelDAGToDAG.cpp
  MSP430ISelLowering.cpp
  MSP430InstrInfo.cpp
  MSP430FrameLowering.cpp
  MSP430MachineFunctionInfo.cpp
  MSP430RegisterInfo.cpp
  MSP430Subtarget.cpp
  MSP430TargetMachine.cpp
  MSP430AsmPrinter.cpp
  MSP430MCInstLower.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  MSP430Desc
  MSP430Info
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  MSP430
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
