//===- ARCRegisterInfo.cpp - ARC Register Information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARC implementation of the MRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "ARCRegisterInfo.h"
#include "ARC.h"
#include "ARCInstrInfo.h"
#include "ARCMachineFunctionInfo.h"
#include "ARCSubtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "arc-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "ARCGenRegisterInfo.inc"

static void replaceFrameIndex(MachineBasicBlock::iterator II,
                              const ARCInstrInfo &TII, unsigned Reg,
                              unsigned FrameReg, int Offset, int StackSize,
                              int ObjSize, RegScavenger *RS, int SPAdj) {
  assert(RS && "Need register scavenger.");
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc DL = MI.getDebugLoc();
  unsigned BaseReg = FrameReg;
  unsigned KillState = 0;
  if (MI.getOpcode() == ARC::LD_rs9 && (Offset >= 256 || Offset < -256)) {
    // Loads can always be reached with LD_rlimm.
    BuildMI(MBB, II, DL, TII.get(ARC::LD_rlimm), Reg)
        .addReg(BaseReg)
        .addImm(Offset)
        .addMemOperand(*MI.memoperands_begin());
    MBB.erase(II);
    return;
  }

  if (MI.getOpcode() != ARC::GETFI && (Offset >= 256 || Offset < -256)) {
    // We need to use a scratch register to reach the far-away frame indexes.
    BaseReg = RS->FindUnusedReg(&ARC::GPR32RegClass);
    if (!BaseReg) {
      // We can be sure that the scavenged-register slot is within the range
      // of the load offset.
      const TargetRegisterInfo *TRI =
          MBB.getParent()->getSubtarget().getRegisterInfo();
      BaseReg =
          RS->scavengeRegisterBackwards(ARC::GPR32RegClass, II, false, SPAdj);
      assert(BaseReg && "Register scavenging failed.");
      LLVM_DEBUG(dbgs() << "Scavenged register " << printReg(BaseReg, TRI)
                        << " for FrameReg=" << printReg(FrameReg, TRI)
                        << "+Offset=" << Offset << "\n");
      (void)TRI;
      RS->setRegUsed(BaseReg);
    }
    unsigned AddOpc = isUInt<6>(Offset) ? ARC::ADD_rru6 : ARC::ADD_rrlimm;
    BuildMI(MBB, II, DL, TII.get(AddOpc))
        .addReg(BaseReg, RegState::Define)
        .addReg(FrameReg)
        .addImm(Offset);
    Offset = 0;
    KillState = RegState::Kill;
  }
  switch (MI.getOpcode()) {
  case ARC::LD_rs9:
    assert((Offset % 4 == 0) && "LD needs 4 byte alignment.");
    [[fallthrough]];
  case ARC::LDH_rs9:
  case ARC::LDH_X_rs9:
    assert((Offset % 2 == 0) && "LDH needs 2 byte alignment.");
    [[fallthrough]];
  case ARC::LDB_rs9:
  case ARC::LDB_X_rs9:
    LLVM_DEBUG(dbgs() << "Building LDFI\n");
    BuildMI(MBB, II, DL, TII.get(MI.getOpcode()), Reg)
        .addReg(BaseReg, KillState)
        .addImm(Offset)
        .addMemOperand(*MI.memoperands_begin());
    break;
  case ARC::ST_rs9:
    assert((Offset % 4 == 0) && "ST needs 4 byte alignment.");
    [[fallthrough]];
  case ARC::STH_rs9:
    assert((Offset % 2 == 0) && "STH needs 2 byte alignment.");
    [[fallthrough]];
  case ARC::STB_rs9:
    LLVM_DEBUG(dbgs() << "Building STFI\n");
    BuildMI(MBB, II, DL, TII.get(MI.getOpcode()))
        .addReg(Reg, getKillRegState(MI.getOperand(0).isKill()))
        .addReg(BaseReg, KillState)
        .addImm(Offset)
        .addMemOperand(*MI.memoperands_begin());
    break;
  case ARC::GETFI:
    LLVM_DEBUG(dbgs() << "Building GETFI\n");
    BuildMI(MBB, II, DL,
            TII.get(isUInt<6>(Offset) ? ARC::ADD_rru6 : ARC::ADD_rrlimm))
        .addReg(Reg, RegState::Define)
        .addReg(FrameReg)
        .addImm(Offset);
    break;
  default:
    llvm_unreachable("Unhandled opcode.");
  }

  // Erase old instruction.
  MBB.erase(II);
}

ARCRegisterInfo::ARCRegisterInfo(const ARCSubtarget &ST)
    : ARCGenRegisterInfo(ARC::BLINK), ST(ST) {}

bool ARCRegisterInfo::needsFrameMoves(const MachineFunction &MF) {
  return MF.needsFrameMoves();
}

const MCPhysReg *
ARCRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_ARC_SaveList;
}

BitVector ARCRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());

  Reserved.set(ARC::ILINK);
  Reserved.set(ARC::SP);
  Reserved.set(ARC::GP);
  Reserved.set(ARC::R25);
  Reserved.set(ARC::BLINK);
  Reserved.set(ARC::FP);

  return Reserved;
}

bool ARCRegisterInfo::requiresRegisterScavenging(
    const MachineFunction &MF) const {
  return true;
}

bool ARCRegisterInfo::useFPForScavengingIndex(const MachineFunction &MF) const {
  return true;
}

bool ARCRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");
  MachineInstr &MI = *II;
  MachineOperand &FrameOp = MI.getOperand(FIOperandNum);
  int FrameIndex = FrameOp.getIndex();

  MachineFunction &MF = *MI.getParent()->getParent();
  const ARCInstrInfo &TII = *MF.getSubtarget<ARCSubtarget>().getInstrInfo();
  const ARCFrameLowering *TFI = getFrameLowering(MF);
  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);
  int ObjSize = MF.getFrameInfo().getObjectSize(FrameIndex);
  int StackSize = MF.getFrameInfo().getStackSize();
  int LocalFrameSize = MF.getFrameInfo().getLocalFrameSize();

  LLVM_DEBUG(dbgs() << "\nFunction         : " << MF.getName() << "\n");
  LLVM_DEBUG(dbgs() << "<--------->\n");
  LLVM_DEBUG(dbgs() << MI << "\n");
  LLVM_DEBUG(dbgs() << "FrameIndex         : " << FrameIndex << "\n");
  LLVM_DEBUG(dbgs() << "ObjSize            : " << ObjSize << "\n");
  LLVM_DEBUG(dbgs() << "FrameOffset        : " << Offset << "\n");
  LLVM_DEBUG(dbgs() << "StackSize          : " << StackSize << "\n");
  LLVM_DEBUG(dbgs() << "LocalFrameSize     : " << LocalFrameSize << "\n");
  (void)LocalFrameSize;

  // Special handling of DBG_VALUE instructions.
  if (MI.isDebugValue()) {
    Register FrameReg = getFrameRegister(MF);
    MI.getOperand(FIOperandNum).ChangeToRegister(FrameReg, false /*isDef*/);
    MI.getOperand(FIOperandNum + 1).ChangeToImmediate(Offset);
    return false;
  }

  // fold constant into offset.
  Offset += MI.getOperand(FIOperandNum + 1).getImm();

  // TODO: assert based on the load type:
  // ldb needs no alignment,
  // ldh needs 2 byte alignment
  // ld needs 4 byte alignment
  LLVM_DEBUG(dbgs() << "Offset             : " << Offset << "\n"
                    << "<--------->\n");

  Register Reg = MI.getOperand(0).getReg();
  assert(ARC::GPR32RegClass.contains(Reg) && "Unexpected register operand");

  if (!TFI->hasFP(MF)) {
    Offset = StackSize + Offset;
    if (FrameIndex >= 0)
      assert((Offset >= 0 && Offset < StackSize) && "SP Offset not in bounds.");
  } else {
    if (FrameIndex >= 0) {
      assert((Offset < 0 && -Offset <= StackSize) &&
             "FP Offset not in bounds.");
    }
  }
  replaceFrameIndex(II, TII, Reg, getFrameRegister(MF), Offset, StackSize,
                    ObjSize, RS, SPAdj);
  return true;                  
}

Register ARCRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const ARCFrameLowering *TFI = getFrameLowering(MF);
  return TFI->hasFP(MF) ? ARC::FP : ARC::SP;
}

const uint32_t *
ARCRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                      CallingConv::ID CC) const {
  return CSR_ARC_RegMask;
}
