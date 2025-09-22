//===-- BPFRegisterInfo.cpp - BPF Register Information ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the BPF implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "BPFRegisterInfo.h"
#include "BPF.h"
#include "BPFSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_REGINFO_TARGET_DESC
#include "BPFGenRegisterInfo.inc"
using namespace llvm;

static cl::opt<int>
    BPFStackSizeOption("bpf-stack-size",
                       cl::desc("Specify the BPF stack size limit"),
                       cl::init(512));

BPFRegisterInfo::BPFRegisterInfo()
    : BPFGenRegisterInfo(BPF::R0) {}

const MCPhysReg *
BPFRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_SaveList;
}

BitVector BPFRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  markSuperRegs(Reserved, BPF::W10); // [W|R]10 is read only frame pointer
  markSuperRegs(Reserved, BPF::W11); // [W|R]11 is pseudo stack pointer
  return Reserved;
}

static void WarnSize(int Offset, MachineFunction &MF, DebugLoc& DL,
                     MachineBasicBlock& MBB) {
  if (Offset <= -BPFStackSizeOption) {
    if (!DL)
      /* try harder to get some debug loc */
      for (auto &I : MBB)
        if (I.getDebugLoc()) {
          DL = I.getDebugLoc();
          break;
        }

    const Function &F = MF.getFunction();
    DiagnosticInfoUnsupported DiagStackSize(
        F,
        "Looks like the BPF stack limit is exceeded. "
        "Please move large on stack variables into BPF per-cpu array map. For "
        "non-kernel uses, the stack can be increased using -mllvm "
        "-bpf-stack-size.\n",
        DL);
    F.getContext().diagnose(DiagStackSize);
  }
}

bool BPFRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                          int SPAdj, unsigned FIOperandNum,
                                          RegScavenger *RS) const {
  assert(SPAdj == 0 && "Unexpected");

  unsigned i = 0;
  MachineInstr &MI = *II;
  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  DebugLoc DL = MI.getDebugLoc();

  while (!MI.getOperand(i).isFI()) {
    ++i;
    assert(i < MI.getNumOperands() && "Instr doesn't have FrameIndex operand!");
  }

  Register FrameReg = getFrameRegister(MF);
  int FrameIndex = MI.getOperand(i).getIndex();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  if (MI.getOpcode() == BPF::MOV_rr) {
    int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex);

    WarnSize(Offset, MF, DL, MBB);
    MI.getOperand(i).ChangeToRegister(FrameReg, false);
    Register reg = MI.getOperand(i - 1).getReg();
    BuildMI(MBB, ++II, DL, TII.get(BPF::ADD_ri), reg)
        .addReg(reg)
        .addImm(Offset);
    return false;
  }

  int Offset = MF.getFrameInfo().getObjectOffset(FrameIndex) +
               MI.getOperand(i + 1).getImm();

  if (!isInt<32>(Offset))
    llvm_unreachable("bug in frame offset");

  WarnSize(Offset, MF, DL, MBB);

  if (MI.getOpcode() == BPF::FI_ri) {
    // architecture does not really support FI_ri, replace it with
    //    MOV_rr <target_reg>, frame_reg
    //    ADD_ri <target_reg>, imm
    Register reg = MI.getOperand(i - 1).getReg();

    BuildMI(MBB, ++II, DL, TII.get(BPF::MOV_rr), reg)
        .addReg(FrameReg);
    BuildMI(MBB, II, DL, TII.get(BPF::ADD_ri), reg)
        .addReg(reg)
        .addImm(Offset);

    // Remove FI_ri instruction
    MI.eraseFromParent();
  } else {
    MI.getOperand(i).ChangeToRegister(FrameReg, false);
    MI.getOperand(i + 1).ChangeToImmediate(Offset);
  }
  return false;
}

Register BPFRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return BPF::R10;
}
