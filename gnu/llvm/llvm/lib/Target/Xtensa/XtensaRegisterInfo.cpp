//===- XtensaRegisterInfo.cpp - Xtensa Register Information ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Xtensa implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "XtensaRegisterInfo.h"
#include "XtensaInstrInfo.h"
#include "XtensaSubtarget.h"
#include "XtensaUtils.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "xtensa-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "XtensaGenRegisterInfo.inc"

using namespace llvm;

XtensaRegisterInfo::XtensaRegisterInfo(const XtensaSubtarget &STI)
    : XtensaGenRegisterInfo(Xtensa::A0), Subtarget(STI) {}

const uint16_t *
XtensaRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_Xtensa_SaveList;
}

const uint32_t *
XtensaRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                         CallingConv::ID) const {
  return CSR_Xtensa_RegMask;
}

BitVector XtensaRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();

  Reserved.set(Xtensa::A0);
  if (TFI->hasFP(MF)) {
    // Reserve frame pointer.
    Reserved.set(getFrameRegister(MF));
  }

  // Reserve stack pointer.
  Reserved.set(Xtensa::SP);
  return Reserved;
}

bool XtensaRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                             int SPAdj, unsigned FIOperandNum,
                                             RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  uint64_t StackSize = MF.getFrameInfo().getStackSize();
  int64_t SPOffset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  int MinCSFI = 0;
  int MaxCSFI = -1;

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }
  // The following stack frame objects are always referenced relative to $sp:
  //  1. Outgoing arguments.
  //  2. Pointer to dynamically allocated stack space.
  //  3. Locations for callee-saved registers.
  //  4. Locations for eh data registers.
  // Everything else is referenced relative to whatever register
  // getFrameRegister() returns.
  unsigned FrameReg;
  if ((FrameIndex >= MinCSFI && FrameIndex <= MaxCSFI))
    FrameReg = Xtensa::SP;
  else
    FrameReg = getFrameRegister(MF);

  // Calculate final offset.
  // - There is no need to change the offset if the frame object is one of the
  //   following: an outgoing argument, pointer to a dynamically allocated
  //   stack space or a $gp restore location,
  // - If the frame object is any of the following, its offset must be adjusted
  //   by adding the size of the stack:
  //   incoming argument, callee-saved register location or local variable.
  bool IsKill = false;
  int64_t Offset =
      SPOffset + (int64_t)StackSize + MI.getOperand(FIOperandNum + 1).getImm();

  bool Valid = isValidAddrOffset(MI, Offset);

  // If MI is not a debug value, make sure Offset fits in the 16-bit immediate
  // field.
  if (!MI.isDebugValue() && !Valid) {
    MachineBasicBlock &MBB = *MI.getParent();
    DebugLoc DL = II->getDebugLoc();
    unsigned ADD = Xtensa::ADD;
    unsigned Reg;
    const XtensaInstrInfo &TII = *static_cast<const XtensaInstrInfo *>(
        MBB.getParent()->getSubtarget().getInstrInfo());

    TII.loadImmediate(MBB, II, &Reg, Offset);
    BuildMI(MBB, II, DL, TII.get(ADD), Reg)
        .addReg(FrameReg)
        .addReg(Reg, RegState::Kill);

    FrameReg = Reg;
    Offset = 0;
    IsKill = true;
  }

  MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false, false, IsKill);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);

  return false;
}

Register XtensaRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? Xtensa::A15 : Xtensa::SP;
}
