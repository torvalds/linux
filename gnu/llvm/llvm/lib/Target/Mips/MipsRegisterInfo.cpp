//===- MipsRegisterInfo.cpp - MIPS Register Information -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the MIPS implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "MipsRegisterInfo.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "Mips.h"
#include "MipsMachineFunction.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "mips-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "MipsGenRegisterInfo.inc"

MipsRegisterInfo::MipsRegisterInfo() : MipsGenRegisterInfo(Mips::RA) {}

unsigned MipsRegisterInfo::getPICCallReg() { return Mips::T9; }

const TargetRegisterClass *
MipsRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                     unsigned Kind) const {
  MipsABIInfo ABI = MF.getSubtarget<MipsSubtarget>().getABI();
  MipsPtrClass PtrClassKind = static_cast<MipsPtrClass>(Kind);

  switch (PtrClassKind) {
  case MipsPtrClass::Default:
    return ABI.ArePtrs64bit() ? &Mips::GPR64RegClass : &Mips::GPR32RegClass;
  case MipsPtrClass::GPR16MM:
    return &Mips::GPRMM16RegClass;
  case MipsPtrClass::StackPointer:
    return ABI.ArePtrs64bit() ? &Mips::SP64RegClass : &Mips::SP32RegClass;
  case MipsPtrClass::GlobalPointer:
    return ABI.ArePtrs64bit() ? &Mips::GP64RegClass : &Mips::GP32RegClass;
  }

  llvm_unreachable("Unknown pointer kind");
}

unsigned
MipsRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                      MachineFunction &MF) const {
  switch (RC->getID()) {
  default:
    return 0;
  case Mips::GPR32RegClassID:
  case Mips::GPR64RegClassID:
  case Mips::DSPRRegClassID: {
    const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
    return 28 - TFI->hasFP(MF);
  }
  case Mips::FGR32RegClassID:
    return 32;
  case Mips::AFGR64RegClassID:
    return 16;
  case Mips::FGR64RegClassID:
    return 32;
  }
}

//===----------------------------------------------------------------------===//
// Callee Saved Registers methods
//===----------------------------------------------------------------------===//

/// Mips Callee Saved Registers
const MCPhysReg *
MipsRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  const MipsSubtarget &Subtarget = MF->getSubtarget<MipsSubtarget>();
  const Function &F = MF->getFunction();
  if (F.hasFnAttribute("interrupt")) {
    if (Subtarget.hasMips64())
      return Subtarget.hasMips64r6() ? CSR_Interrupt_64R6_SaveList
                                     : CSR_Interrupt_64_SaveList;
    else
      return Subtarget.hasMips32r6() ? CSR_Interrupt_32R6_SaveList
                                     : CSR_Interrupt_32_SaveList;
  }

  if (Subtarget.isSingleFloat())
    return CSR_SingleFloatOnly_SaveList;

  if (Subtarget.isABI_N64())
    return CSR_N64_SaveList;

  if (Subtarget.isABI_N32())
    return CSR_N32_SaveList;

  if (Subtarget.isFP64bit())
    return CSR_O32_FP64_SaveList;

  if (Subtarget.isFPXX())
    return CSR_O32_FPXX_SaveList;

  return CSR_O32_SaveList;
}

const uint32_t *
MipsRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const {
  const MipsSubtarget &Subtarget = MF.getSubtarget<MipsSubtarget>();
  if (Subtarget.isSingleFloat())
    return CSR_SingleFloatOnly_RegMask;

  if (Subtarget.isABI_N64())
    return CSR_N64_RegMask;

  if (Subtarget.isABI_N32())
    return CSR_N32_RegMask;

  if (Subtarget.isFP64bit())
    return CSR_O32_FP64_RegMask;

  if (Subtarget.isFPXX())
    return CSR_O32_FPXX_RegMask;

  return CSR_O32_RegMask;
}

const uint32_t *MipsRegisterInfo::getMips16RetHelperMask() {
  return CSR_Mips16RetHelper_RegMask;
}

BitVector MipsRegisterInfo::
getReservedRegs(const MachineFunction &MF) const {
  static const MCPhysReg ReservedGPR32[] = {
    Mips::ZERO, Mips::K0, Mips::K1, Mips::SP
  };

  static const MCPhysReg ReservedGPR64[] = {
    Mips::ZERO_64, Mips::K0_64, Mips::K1_64, Mips::SP_64
  };

  BitVector Reserved(getNumRegs());
  const MipsSubtarget &Subtarget = MF.getSubtarget<MipsSubtarget>();

  for (MCPhysReg R : ReservedGPR32)
    Reserved.set(R);

  // Reserve registers for the NaCl sandbox.
  if (Subtarget.isTargetNaCl()) {
    Reserved.set(Mips::T6);   // Reserved for control flow mask.
    Reserved.set(Mips::T7);   // Reserved for memory access mask.
    Reserved.set(Mips::T8);   // Reserved for thread pointer.
  }

  for (MCPhysReg R : ReservedGPR64)
    Reserved.set(R);

  // For mno-abicalls, GP is a program invariant!
  if (!Subtarget.isABICalls()) {
    Reserved.set(Mips::GP);
    Reserved.set(Mips::GP_64);
  }

  if (Subtarget.isFP64bit()) {
    // Reserve all registers in AFGR64.
    for (MCPhysReg Reg : Mips::AFGR64RegClass)
      Reserved.set(Reg);
  } else {
    // Reserve all registers in FGR64.
    for (MCPhysReg Reg : Mips::FGR64RegClass)
      Reserved.set(Reg);
  }
  // Reserve FP if this function should have a dedicated frame pointer register.
  if (Subtarget.getFrameLowering()->hasFP(MF)) {
    if (Subtarget.inMips16Mode())
      Reserved.set(Mips::S0);
    else {
      Reserved.set(Mips::FP);
      Reserved.set(Mips::FP_64);

      // Reserve the base register if we need to both realign the stack and
      // allocate variable-sized objects at runtime. This should test the
      // same conditions as MipsFrameLowering::hasBP().
      if (hasStackRealignment(MF) && MF.getFrameInfo().hasVarSizedObjects()) {
        Reserved.set(Mips::S7);
        Reserved.set(Mips::S7_64);
      }
    }
  }

  // Reserve hardware registers.
  Reserved.set(Mips::HWR29);

  // Reserve DSP control register.
  Reserved.set(Mips::DSPPos);
  Reserved.set(Mips::DSPSCount);
  Reserved.set(Mips::DSPCarry);
  Reserved.set(Mips::DSPEFI);
  Reserved.set(Mips::DSPOutFlag);

  // Reserve MSA control registers.
  for (MCPhysReg Reg : Mips::MSACtrlRegClass)
    Reserved.set(Reg);

  // Reserve RA if in mips16 mode.
  if (Subtarget.inMips16Mode()) {
    const MipsFunctionInfo *MipsFI = MF.getInfo<MipsFunctionInfo>();
    Reserved.set(Mips::RA);
    Reserved.set(Mips::RA_64);
    Reserved.set(Mips::T0);
    Reserved.set(Mips::T1);
    if (MF.getFunction().hasFnAttribute("saveS2") || MipsFI->hasSaveS2())
      Reserved.set(Mips::S2);
  }

  // Reserve GP if small section is used.
  if (Subtarget.useSmallSection()) {
    Reserved.set(Mips::GP);
    Reserved.set(Mips::GP_64);
  }

  return Reserved;
}

// FrameIndex represent objects inside a abstract stack.
// We must replace FrameIndex with an stack/frame pointer
// direct reference.
bool MipsRegisterInfo::
eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                    unsigned FIOperandNum, RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();

  LLVM_DEBUG(errs() << "\nFunction : " << MF.getName() << "\n";
             errs() << "<--------->\n"
                    << MI);

  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  uint64_t stackSize = MF.getFrameInfo().getStackSize();
  int64_t spOffset = MF.getFrameInfo().getObjectOffset(FrameIndex);

  LLVM_DEBUG(errs() << "FrameIndex : " << FrameIndex << "\n"
                    << "spOffset   : " << spOffset << "\n"
                    << "stackSize  : " << stackSize << "\n"
                    << "alignment  : "
                    << DebugStr(MF.getFrameInfo().getObjectAlign(FrameIndex))
                    << "\n");

  eliminateFI(MI, FIOperandNum, FrameIndex, stackSize, spOffset);
  return false;
}

Register MipsRegisterInfo::
getFrameRegister(const MachineFunction &MF) const {
  const MipsSubtarget &Subtarget = MF.getSubtarget<MipsSubtarget>();
  const TargetFrameLowering *TFI = Subtarget.getFrameLowering();
  bool IsN64 =
      static_cast<const MipsTargetMachine &>(MF.getTarget()).getABI().IsN64();

  if (Subtarget.inMips16Mode())
    return TFI->hasFP(MF) ? Mips::S0 : Mips::SP;
  else
    return TFI->hasFP(MF) ? (IsN64 ? Mips::FP_64 : Mips::FP) :
                            (IsN64 ? Mips::SP_64 : Mips::SP);
}

bool MipsRegisterInfo::canRealignStack(const MachineFunction &MF) const {
  // Avoid realigning functions that explicitly do not want to be realigned.
  // Normally, we should report an error when a function should be dynamically
  // realigned but also has the attribute no-realign-stack. Unfortunately,
  // with this attribute, MachineFrameInfo clamps each new object's alignment
  // to that of the stack's alignment as specified by the ABI. As a result,
  // the information of whether we have objects with larger alignment
  // requirement than the stack's alignment is already lost at this point.
  if (!TargetRegisterInfo::canRealignStack(MF))
    return false;

  const MipsSubtarget &Subtarget = MF.getSubtarget<MipsSubtarget>();
  unsigned FP = Subtarget.isGP32bit() ? Mips::FP : Mips::FP_64;
  unsigned BP = Subtarget.isGP32bit() ? Mips::S7 : Mips::S7_64;

  // Support dynamic stack realignment for all targets except Mips16.
  if (Subtarget.inMips16Mode())
    return false;

  // We can't perform dynamic stack realignment if we can't reserve the
  // frame pointer register.
  if (!MF.getRegInfo().canReserveReg(FP))
    return false;

  // We can realign the stack if we know the maximum call frame size and we
  // don't have variable sized objects.
  if (Subtarget.getFrameLowering()->hasReservedCallFrame(MF))
    return true;

  // We have to reserve the base pointer register in the presence of variable
  // sized objects.
  return MF.getRegInfo().canReserveReg(BP);
}
