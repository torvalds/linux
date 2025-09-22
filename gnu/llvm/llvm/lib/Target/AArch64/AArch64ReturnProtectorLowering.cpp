//===-- AArch64ReturnProtectorLowering.cpp --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of ReturnProtectorLowering
// class.
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64RegisterInfo.h"
#include "AArch64ReturnProtectorLowering.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetMachine.h"
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

void AArch64ReturnProtectorLowering::insertReturnProtectorPrologue(
    MachineFunction &MF, MachineBasicBlock &MBB, GlobalVariable *cookie) const {

  MachineBasicBlock::instr_iterator MI = MBB.instr_begin();
  DebugLoc MBBDL = MBB.findDebugLoc(MI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::ADRP), REG)
      .addGlobalAddress(cookie, 0, AArch64II::MO_PAGE);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::LDRXui), REG)
      .addReg(REG)
      .addGlobalAddress(cookie, 0, AArch64II::MO_PAGEOFF | AArch64II::MO_NC);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::EORXrr), REG)
      .addReg(REG)
      .addReg(AArch64::LR);
}

void AArch64ReturnProtectorLowering::insertReturnProtectorEpilogue(
    MachineFunction &MF, MachineInstr &MI, GlobalVariable *cookie) const {

  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc MBBDL = MI.getDebugLoc();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  MBB.addLiveIn(AArch64::X9);
  // REG holds the cookie we calculated in prologue. We use X9 as a
  // scratch reg to pull the random data. XOR REG with LR should yield
  // the random data again. Compare REG with X9 to check.
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::EORXrr), REG)
      .addReg(REG)
      .addReg(AArch64::LR);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::ADRP), AArch64::X9)
      .addGlobalAddress(cookie, 0, AArch64II::MO_PAGE);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::LDRXui), AArch64::X9)
      .addReg(AArch64::X9)
      .addGlobalAddress(cookie, 0, AArch64II::MO_PAGEOFF | AArch64II::MO_NC);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::SUBSXrr), REG)
      .addReg(REG)
      .addReg(AArch64::X9);
  BuildMI(MBB, MI, MBBDL, TII->get(AArch64::RETGUARD_JMP_TRAP)).addReg(REG);
}

bool AArch64ReturnProtectorLowering::opcodeIsReturn(unsigned opcode) const {
  switch (opcode) {
  case AArch64::RET:
  case AArch64::RET_ReallyLR:
    return true;
  default:
    return false;
  }
}

void AArch64ReturnProtectorLowering::fillTempRegisters(
    MachineFunction &MF, std::vector<unsigned> &TempRegs) const {

  TempRegs.push_back(AArch64::X15);
  TempRegs.push_back(AArch64::X14);
  TempRegs.push_back(AArch64::X13);
  TempRegs.push_back(AArch64::X12);
  TempRegs.push_back(AArch64::X11);
  TempRegs.push_back(AArch64::X10);
}

void AArch64ReturnProtectorLowering::saveReturnProtectorRegister(
    MachineFunction &MF, std::vector<CalleeSavedInfo> &CSI) const {

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.getReturnProtectorNeeded())
    return;

  if (!MFI.hasReturnProtectorRegister())
    llvm_unreachable("Saving unset return protector register");

  unsigned Reg = MFI.getReturnProtectorRegister();
  if (!MFI.getReturnProtectorNeedsStore()) {
    for (auto &MBB : MF) {
      if (!MBB.isLiveIn(Reg))
        MBB.addLiveIn(Reg);
    }
    return;
  }

  // CSI Reg order is important for pairing registers later.
  // The expected order of the CSI is given by getCalleeSavedRegs(),
  // which for us returns a list of GPRs and FPRs in ascending
  // order. Since our temp regs are all before the usual callee
  // saved regs, we can just insert our reg first.
  CSI.insert(CSI.begin(), CalleeSavedInfo(MFI.getReturnProtectorRegister()));
}
