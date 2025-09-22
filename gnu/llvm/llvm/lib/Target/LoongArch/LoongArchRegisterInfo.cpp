//===- LoongArchRegisterInfo.cpp - LoongArch Register Information -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the LoongArch implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "LoongArchRegisterInfo.h"
#include "LoongArch.h"
#include "LoongArchInstrInfo.h"
#include "LoongArchSubtarget.h"
#include "MCTargetDesc/LoongArchBaseInfo.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "LoongArchGenRegisterInfo.inc"

LoongArchRegisterInfo::LoongArchRegisterInfo(unsigned HwMode)
    : LoongArchGenRegisterInfo(LoongArch::R1, /*DwarfFlavour*/ 0,
                               /*EHFlavor*/ 0,
                               /*PC*/ 0, HwMode) {}

const MCPhysReg *
LoongArchRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  auto &Subtarget = MF->getSubtarget<LoongArchSubtarget>();

  if (MF->getFunction().getCallingConv() == CallingConv::GHC)
    return CSR_NoRegs_SaveList;
  switch (Subtarget.getTargetABI()) {
  default:
    llvm_unreachable("Unrecognized ABI");
  case LoongArchABI::ABI_ILP32S:
  case LoongArchABI::ABI_LP64S:
    return CSR_ILP32S_LP64S_SaveList;
  case LoongArchABI::ABI_ILP32F:
  case LoongArchABI::ABI_LP64F:
    return CSR_ILP32F_LP64F_SaveList;
  case LoongArchABI::ABI_ILP32D:
  case LoongArchABI::ABI_LP64D:
    return CSR_ILP32D_LP64D_SaveList;
  }
}

const uint32_t *
LoongArchRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                            CallingConv::ID CC) const {
  auto &Subtarget = MF.getSubtarget<LoongArchSubtarget>();

  if (CC == CallingConv::GHC)
    return CSR_NoRegs_RegMask;
  switch (Subtarget.getTargetABI()) {
  default:
    llvm_unreachable("Unrecognized ABI");
  case LoongArchABI::ABI_ILP32S:
  case LoongArchABI::ABI_LP64S:
    return CSR_ILP32S_LP64S_RegMask;
  case LoongArchABI::ABI_ILP32F:
  case LoongArchABI::ABI_LP64F:
    return CSR_ILP32F_LP64F_RegMask;
  case LoongArchABI::ABI_ILP32D:
  case LoongArchABI::ABI_LP64D:
    return CSR_ILP32D_LP64D_RegMask;
  }
}

const uint32_t *LoongArchRegisterInfo::getNoPreservedMask() const {
  return CSR_NoRegs_RegMask;
}

BitVector
LoongArchRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const LoongArchFrameLowering *TFI = getFrameLowering(MF);
  BitVector Reserved(getNumRegs());

  // Use markSuperRegs to ensure any register aliases are also reserved
  markSuperRegs(Reserved, LoongArch::R0);  // zero
  markSuperRegs(Reserved, LoongArch::R2);  // tp
  markSuperRegs(Reserved, LoongArch::R3);  // sp
  markSuperRegs(Reserved, LoongArch::R21); // non-allocatable
  if (TFI->hasFP(MF))
    markSuperRegs(Reserved, LoongArch::R22); // fp
  // Reserve the base register if we need to realign the stack and allocate
  // variable-sized objects at runtime.
  if (TFI->hasBP(MF))
    markSuperRegs(Reserved, LoongArchABI::getBPReg()); // bp

  assert(checkAllSuperRegsMarked(Reserved));
  return Reserved;
}

Register
LoongArchRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? LoongArch::R22 : LoongArch::R3;
}

bool LoongArchRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                                int SPAdj,
                                                unsigned FIOperandNum,
                                                RegScavenger *RS) const {
  // TODO: this implementation is a temporary placeholder which does just
  // enough to allow other aspects of code generation to be tested.

  assert(SPAdj == 0 && "Unexpected non-zero SPAdj value");

  MachineInstr &MI = *II;
  assert(MI.getOperand(FIOperandNum + 1).isImm() &&
         "Unexpected FI-consuming insn");

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const LoongArchSubtarget &STI = MF.getSubtarget<LoongArchSubtarget>();
  const LoongArchInstrInfo *TII = STI.getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  DebugLoc DL = MI.getDebugLoc();
  bool IsLA64 = STI.is64Bit();
  unsigned MIOpc = MI.getOpcode();

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  Register FrameReg;
  StackOffset Offset =
      TFI->getFrameIndexReference(MF, FrameIndex, FrameReg) +
      StackOffset::getFixed(MI.getOperand(FIOperandNum + 1).getImm());

  bool FrameRegIsKill = false;

  if (!isInt<12>(Offset.getFixed())) {
    unsigned Addi = IsLA64 ? LoongArch::ADDI_D : LoongArch::ADDI_W;
    unsigned Add = IsLA64 ? LoongArch::ADD_D : LoongArch::ADD_W;

    // The offset won't fit in an immediate, so use a scratch register instead.
    // Modify Offset and FrameReg appropriately.
    Register ScratchReg = MRI.createVirtualRegister(&LoongArch::GPRRegClass);
    TII->movImm(MBB, II, DL, ScratchReg, Offset.getFixed());
    if (MIOpc == Addi) {
      BuildMI(MBB, II, DL, TII->get(Add), MI.getOperand(0).getReg())
          .addReg(FrameReg)
          .addReg(ScratchReg, RegState::Kill);
      MI.eraseFromParent();
      return true;
    }
    BuildMI(MBB, II, DL, TII->get(Add), ScratchReg)
        .addReg(FrameReg)
        .addReg(ScratchReg, RegState::Kill);
    Offset = StackOffset::getFixed(0);
    FrameReg = ScratchReg;
    FrameRegIsKill = true;
  }

  // Spill CFRs.
  if (MIOpc == LoongArch::PseudoST_CFR) {
    Register ScratchReg = MRI.createVirtualRegister(&LoongArch::GPRRegClass);
    BuildMI(MBB, II, DL, TII->get(LoongArch::MOVCF2GR), ScratchReg)
        .add(MI.getOperand(0));
    BuildMI(MBB, II, DL, TII->get(IsLA64 ? LoongArch::ST_D : LoongArch::ST_W))
        .addReg(ScratchReg, RegState::Kill)
        .addReg(FrameReg)
        .addImm(Offset.getFixed());
    MI.eraseFromParent();
    return true;
  }

  // Reload CFRs.
  if (MIOpc == LoongArch::PseudoLD_CFR) {
    Register ScratchReg = MRI.createVirtualRegister(&LoongArch::GPRRegClass);
    BuildMI(MBB, II, DL, TII->get(IsLA64 ? LoongArch::LD_D : LoongArch::LD_W),
            ScratchReg)
        .addReg(FrameReg)
        .addImm(Offset.getFixed());
    BuildMI(MBB, II, DL, TII->get(LoongArch::MOVGR2CF))
        .add(MI.getOperand(0))
        .addReg(ScratchReg, RegState::Kill);
    MI.eraseFromParent();
    return true;
  }

  MI.getOperand(FIOperandNum)
      .ChangeToRegister(FrameReg, false, false, FrameRegIsKill);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset.getFixed());
  return false;
}

bool LoongArchRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;

  const MachineRegisterInfo *MRI = &MF.getRegInfo();
  const LoongArchFrameLowering *TFI = getFrameLowering(MF);

  // Stack realignment requires a frame pointer.  If we already started
  // register allocation with frame pointer elimination, it is too late now.
  if (!MRI->canReserveReg(LoongArch::R22))
    return false;

  // We may also need a base pointer if there are dynamic allocas or stack
  // pointer adjustments around calls.
  if (TFI->hasReservedCallFrame(MF))
    return true;

  // A base pointer is required and allowed.  Check that it isn't too late to
  // reserve it.
  return MRI->canReserveReg(LoongArchABI::getBPReg());
}
