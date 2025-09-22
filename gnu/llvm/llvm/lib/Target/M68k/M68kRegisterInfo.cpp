//===-- M68kRegisterInfo.cpp - CPU0 Register Information --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the CPU0 implementation of the TargetRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#include "M68kRegisterInfo.h"

#include "M68k.h"
#include "M68kMachineFunction.h"
#include "M68kSubtarget.h"

#include "MCTargetDesc/M68kMCTargetDesc.h"

#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define GET_REGINFO_TARGET_DESC
#include "M68kGenRegisterInfo.inc"

#define DEBUG_TYPE "m68k-reg-info"

using namespace llvm;

static cl::opt<bool> EnableBasePointer(
    "m68k-use-base-pointer", cl::Hidden, cl::init(true),
    cl::desc("Enable use of a base pointer for complex stack frames"));

// Pin the vtable to this file.
void M68kRegisterInfo::anchor() {}

M68kRegisterInfo::M68kRegisterInfo(const M68kSubtarget &ST)
    // FIXME x26 not sure it this the correct value, it expects RA, but M68k
    // passes IP anyway, how this works?
    : M68kGenRegisterInfo(M68k::A0, 0, 0, M68k::PC), Subtarget(ST) {
  StackPtr = M68k::SP;
  FramePtr = M68k::A6;
  GlobalBasePtr = M68k::A5;
  BasePtr = M68k::A4;
}

//===----------------------------------------------------------------------===//
// Callee Saved Registers methods
//===----------------------------------------------------------------------===//

const MCPhysReg *
M68kRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_STD_SaveList;
}

const uint32_t *
M68kRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const {
  return CSR_STD_RegMask;
}

const TargetRegisterClass *
M68kRegisterInfo::getRegsForTailCall(const MachineFunction &MF) const {
  return &M68k::XR32_TCRegClass;
}

unsigned
M68kRegisterInfo::getMatchingMegaReg(unsigned Reg,
                                     const TargetRegisterClass *RC) const {
  for (MCPhysReg Super : superregs(Reg))
    if (RC->contains(Super))
      return Super;
  return 0;
}

const TargetRegisterClass *
M68kRegisterInfo::getMaximalPhysRegClass(unsigned reg, MVT VT) const {
  assert(Register::isPhysicalRegister(reg) &&
         "reg must be a physical register");

  // Pick the most sub register class of the right type that contains
  // this physreg.
  const TargetRegisterClass *BestRC = nullptr;
  for (regclass_iterator I = regclass_begin(), E = regclass_end(); I != E;
       ++I) {
    const TargetRegisterClass *RC = *I;
    if ((VT == MVT::Other || isTypeLegalForClass(*RC, VT)) &&
        RC->contains(reg) &&
        (!BestRC ||
         (BestRC->hasSubClass(RC) && RC->getNumRegs() > BestRC->getNumRegs())))
      BestRC = RC;
  }

  assert(BestRC && "Couldn't find the register class");
  return BestRC;
}

int M68kRegisterInfo::getRegisterOrder(unsigned Reg,
                                       const TargetRegisterClass &TRC) const {
  for (unsigned i = 0; i < TRC.getNumRegs(); ++i) {
    if (regsOverlap(Reg, TRC.getRegister(i))) {
      return i;
    }
  }
  return -1;
}

int M68kRegisterInfo::getSpillRegisterOrder(unsigned Reg) const {
  int Result = getRegisterOrder(Reg, *getRegClass(M68k::SPILLRegClassID));
  assert(Result >= 0 && "Can not determine spill order");
  return Result;
}

BitVector M68kRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  const M68kFrameLowering *TFI = getFrameLowering(MF);

  BitVector Reserved(getNumRegs());

  // Set a register's and its sub-registers and aliases as reserved.
  auto setBitVector = [&Reserved, this](unsigned Reg) {
    for (MCRegAliasIterator I(Reg, this, /* self */ true); I.isValid(); ++I) {
      Reserved.set(*I);
    }
    for (MCPhysReg I : subregs_inclusive(Reg)) {
      Reserved.set(I);
    }
  };

  // Registers reserved by users
  for (size_t Reg = 0, Total = getNumRegs(); Reg != Total; ++Reg) {
    if (MF.getSubtarget<M68kSubtarget>().isRegisterReservedByUser(Reg))
      setBitVector(Reg);
  }

  setBitVector(M68k::PC);
  setBitVector(M68k::SP);

  if (TFI->hasFP(MF)) {
    setBitVector(FramePtr);
  }

  // Set the base-pointer register and its aliases as reserved if needed.
  if (hasBasePointer(MF)) {
    CallingConv::ID CC = MF.getFunction().getCallingConv();
    const uint32_t *RegMask = getCallPreservedMask(MF, CC);
    if (MachineOperand::clobbersPhysReg(RegMask, getBaseRegister()))
      report_fatal_error("Stack realignment in presence of dynamic allocas is "
                         "not supported with"
                         "this calling convention.");

    setBitVector(getBaseRegister());
  }

  return Reserved;
}

bool M68kRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                           int SPAdj, unsigned FIOperandNum,
                                           RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const M68kFrameLowering *TFI = getFrameLowering(MF);

  // We have either (i,An,Rn) or (i,An) EA form
  // NOTE Base contains the FI and we need to backtrace a bit to get Disp
  MachineOperand &Disp = MI.getOperand(FIOperandNum - 1);
  MachineOperand &Base = MI.getOperand(FIOperandNum);

  int Imm = (int)(Disp.getImm());
  int FIndex = (int)(Base.getIndex());

  // FIXME tail call: implement jmp from mem
  bool AfterFPPop = false;

  unsigned BasePtr;
  if (hasBasePointer(MF))
    BasePtr = (FIndex < 0 ? FramePtr : getBaseRegister());
  else if (hasStackRealignment(MF))
    BasePtr = (FIndex < 0 ? FramePtr : StackPtr);
  else if (AfterFPPop)
    BasePtr = StackPtr;
  else
    BasePtr = (TFI->hasFP(MF) ? FramePtr : StackPtr);

  Base.ChangeToRegister(BasePtr, false);

  // Now add the frame object offset to the offset from FP.
  int64_t FIOffset;
  Register IgnoredFrameReg;
  if (AfterFPPop) {
    // Tail call jmp happens after FP is popped.
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    FIOffset = MFI.getObjectOffset(FIndex) - TFI->getOffsetOfLocalArea();
  } else {
    FIOffset =
        TFI->getFrameIndexReference(MF, FIndex, IgnoredFrameReg).getFixed();
  }

  if (BasePtr == StackPtr)
    FIOffset += SPAdj;

  Disp.ChangeToImmediate(FIOffset + Imm);
  return false;
}

bool M68kRegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return false;
}

bool M68kRegisterInfo::trackLivenessAfterRegAlloc(
    const MachineFunction &MF) const {
  return true;
}

static bool CantUseSP(const MachineFrameInfo &MFI) {
  return MFI.hasVarSizedObjects() || MFI.hasOpaqueSPAdjustment();
}

bool M68kRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  if (!EnableBasePointer)
    return false;

  // When we need stack realignment, we can't address the stack from the frame
  // pointer.  When we have dynamic allocas or stack-adjusting inline asm, we
  // can't address variables from the stack pointer.  MS inline asm can
  // reference locals while also adjusting the stack pointer.  When we can't
  // use both the SP and the FP, we need a separate base pointer register.
  bool CantUseFP = hasStackRealignment(MF);
  return CantUseFP && CantUseSP(MFI);
}

bool M68kRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const MachineRegisterInfo *MRI = &MF.getRegInfo();

  // Stack realignment requires a frame pointer.  If we already started
  // register allocation with frame pointer elimination, it is too late now.
  if (!MRI->canReserveReg(FramePtr))
    return false;

  // If a base pointer is necessary. Check that it isn't too late to reserve it.
  if (CantUseSP(MFI))
    return MRI->canReserveReg(BasePtr);

  return true;
}

Register M68kRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  return TFI->hasFP(MF) ? FramePtr : StackPtr;
}

const TargetRegisterClass *M68kRegisterInfo::intRegClass(unsigned size) const {
  return &M68k::DR32RegClass;
}
