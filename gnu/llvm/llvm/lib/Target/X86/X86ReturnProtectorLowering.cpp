//===-- X86ReturnProtectorLowering.cpp - ----------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of ReturnProtectorLowering class.
//
//===----------------------------------------------------------------------===//

#include "X86ReturnProtectorLowering.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

using namespace llvm;

void X86ReturnProtectorLowering::insertReturnProtectorPrologue(
    MachineFunction &MF, MachineBasicBlock &MBB, GlobalVariable *cookie) const {

  MachineBasicBlock::instr_iterator MI = MBB.instr_begin();
  DebugLoc MBBDL = MBB.findDebugLoc(MI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  BuildMI(MBB, MI, MBBDL, TII->get(X86::MOV64rm), REG)
      .addReg(X86::RIP)
      .addImm(0)
      .addReg(0)
      .addGlobalAddress(cookie)
      .addReg(0);
  addDirectMem(BuildMI(MBB, MI, MBBDL, TII->get(X86::XOR64rm), REG).addReg(REG),
               X86::RSP);
}

void X86ReturnProtectorLowering::insertReturnProtectorEpilogue(
    MachineFunction &MF, MachineInstr &MI, GlobalVariable *cookie) const {

  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc MBBDL = MI.getDebugLoc();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  addDirectMem(BuildMI(MBB, MI, MBBDL, TII->get(X86::XOR64rm), REG).addReg(REG),
               X86::RSP);
  BuildMI(MBB, MI, MBBDL, TII->get(X86::CMP64rm))
      .addReg(REG)
      .addReg(X86::RIP)
      .addImm(0)
      .addReg(0)
      .addGlobalAddress(cookie)
      .addReg(0);
  BuildMI(MBB, MI, MBBDL, TII->get(X86::RETGUARD_JMP_TRAP));
}

bool X86ReturnProtectorLowering::opcodeIsReturn(unsigned opcode) const {
  switch (opcode) {
  case X86::RET:
  case X86::RET16:
  case X86::RET32:
  case X86::RET64:
  case X86::RETI16:
  case X86::RETI32:
  case X86::RETI64:
  case X86::LRET16:
  case X86::LRET32:
  case X86::LRET64:
  case X86::LRETI16:
  case X86::LRETI32:
  case X86::LRETI64:
    return true;
  default:
    return false;
  }
}

void X86ReturnProtectorLowering::fillTempRegisters(
    MachineFunction &MF, std::vector<unsigned> &TempRegs) const {

  TempRegs.push_back(X86::R11);
  TempRegs.push_back(X86::R10);
  const Function &F = MF.getFunction();
  if (!F.isVarArg()) {
    // We can use any of the caller saved unused arg registers
    switch (F.arg_size()) {
    case 0:
      TempRegs.push_back(X86::RDI);
      LLVM_FALLTHROUGH;
    case 1:
      TempRegs.push_back(X86::RSI);
      LLVM_FALLTHROUGH;
    case 2: // RDX is the 2nd return register
    case 3:
      TempRegs.push_back(X86::RCX);
      LLVM_FALLTHROUGH;
    case 4:
      TempRegs.push_back(X86::R8);
      LLVM_FALLTHROUGH;
    case 5:
      TempRegs.push_back(X86::R9);
      LLVM_FALLTHROUGH;
    default:
      break;
    }
  }
}
