add_llvm_component_group(CSKY)

set(LLVM_TARGET_DEFINITIONS CSKY.td)

tablegen(LLVM CSKYGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM CSKYGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM CSKYGenCallingConv.inc -gen-callingconv)
tablegen(LLVM CSKYGenCompressInstEmitter.inc -gen-compress-inst-emitter)
tablegen(LLVM CSKYGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM CSKYGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM CSKYGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM CSKYGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM CSKYGenMCPseudoLowering.inc -gen-pseudo-lowering)
tablegen(LLVM CSKYGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM CSKYGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(CSKYCommonTableGen)

add_llvm_target(CSKYCodeGen
  CSKYAsmPrinter.cpp
  CSKYConstantIslandPass.cpp
  CSKYConstantPoolValue.cpp
  CSKYFrameLowering.cpp
  CSKYInstrInfo.cpp
  CSKYISelDAGToDAG.cpp
  CSKYISelLowering.cpp
  CSKYMCInstLower.cpp
  CSKYRegisterInfo.cpp
  CSKYSubtarget.cpp
  CSKYTargetMachine.cpp
  CSKYTargetObjectFile.cpp

  LINK_COMPONENTS
  Analysis
  AsmPrinter
  CSKYDesc
  CSKYInfo
  CodeGen
  CodeGenTypes
  Core
  MC
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  CSKY
  )

add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
