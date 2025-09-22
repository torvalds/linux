//===-- RISCVExpandAtomicPseudoInsts.cpp - Expand atomic pseudo instrs. ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands atomic pseudo instructions into
// target instructions. This pass should be run at the last possible moment,
// avoiding the possibility for other passes to break the requirements for
// forward progress in the LR/SC block.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVTargetMachine.h"

#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

using namespace llvm;

#define RISCV_EXPAND_ATOMIC_PSEUDO_NAME                                        \
  "RISC-V atomic pseudo instruction expansion pass"

namespace {

class RISCVExpandAtomicPseudo : public MachineFunctionPass {
public:
  const RISCVSubtarget *STI;
  const RISCVInstrInfo *TII;
  static char ID;

  RISCVExpandAtomicPseudo() : MachineFunctionPass(ID) {
    initializeRISCVExpandAtomicPseudoPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return RISCV_EXPAND_ATOMIC_PSEUDO_NAME;
  }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandAtomicBinOp(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, AtomicRMWInst::BinOp,
                         bool IsMasked, int Width,
                         MachineBasicBlock::iterator &NextMBBI);
  bool expandAtomicMinMaxOp(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            AtomicRMWInst::BinOp, bool IsMasked, int Width,
                            MachineBasicBlock::iterator &NextMBBI);
  bool expandAtomicCmpXchg(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, bool IsMasked,
                           int Width, MachineBasicBlock::iterator &NextMBBI);
#ifndef NDEBUG
  unsigned getInstSizeInBytes(const MachineFunction &MF) const {
    unsigned Size = 0;
    for (auto &MBB : MF)
      for (auto &MI : MBB)
        Size += TII->getInstSizeInBytes(MI);
    return Size;
  }
#endif
};

char RISCVExpandAtomicPseudo::ID = 0;

bool RISCVExpandAtomicPseudo::runOnMachineFunction(MachineFunction &MF) {
  STI = &MF.getSubtarget<RISCVSubtarget>();
  TII = STI->getInstrInfo();

#ifndef NDEBUG
  const unsigned OldSize = getInstSizeInBytes(MF);
#endif

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= expandMBB(MBB);

#ifndef NDEBUG
  const unsigned NewSize = getInstSizeInBytes(MF);
  assert(OldSize >= NewSize);
#endif
  return Modified;
}

bool RISCVExpandAtomicPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool RISCVExpandAtomicPseudo::expandMI(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       MachineBasicBlock::iterator &NextMBBI) {
  // RISCVInstrInfo::getInstSizeInBytes expects that the total size of the
  // expanded instructions for each pseudo is correct in the Size field of the
  // tablegen definition for the pseudo.
  switch (MBBI->getOpcode()) {
  case RISCV::PseudoAtomicLoadNand32:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Nand, false, 32,
                             NextMBBI);
  case RISCV::PseudoAtomicLoadNand64:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Nand, false, 64,
                             NextMBBI);
  case RISCV::PseudoMaskedAtomicSwap32:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Xchg, true, 32,
                             NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadAdd32:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Add, true, 32, NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadSub32:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Sub, true, 32, NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadNand32:
    return expandAtomicBinOp(MBB, MBBI, AtomicRMWInst::Nand, true, 32,
                             NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadMax32:
    return expandAtomicMinMaxOp(MBB, MBBI, AtomicRMWInst::Max, true, 32,
                                NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadMin32:
    return expandAtomicMinMaxOp(MBB, MBBI, AtomicRMWInst::Min, true, 32,
                                NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadUMax32:
    return expandAtomicMinMaxOp(MBB, MBBI, AtomicRMWInst::UMax, true, 32,
                                NextMBBI);
  case RISCV::PseudoMaskedAtomicLoadUMin32:
    return expandAtomicMinMaxOp(MBB, MBBI, AtomicRMWInst::UMin, true, 32,
                                NextMBBI);
  case RISCV::PseudoCmpXchg32:
    return expandAtomicCmpXchg(MBB, MBBI, false, 32, NextMBBI);
  case RISCV::PseudoCmpXchg64:
    return expandAtomicCmpXchg(MBB, MBBI, false, 64, NextMBBI);
  case RISCV::PseudoMaskedCmpXchg32:
    return expandAtomicCmpXchg(MBB, MBBI, true, 32, NextMBBI);
  }

  return false;
}

static unsigned getLRForRMW32(AtomicOrdering Ordering,
                              const RISCVSubtarget *Subtarget) {
  switch (Ordering) {
  default:
    llvm_unreachable("Unexpected AtomicOrdering");
  case AtomicOrdering::Monotonic:
    return RISCV::LR_W;
  case AtomicOrdering::Acquire:
    if (Subtarget->hasStdExtZtso())
      return RISCV::LR_W;
    return RISCV::LR_W_AQ;
  case AtomicOrdering::Release:
    return RISCV::LR_W;
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->hasStdExtZtso())
      return RISCV::LR_W;
    return RISCV::LR_W_AQ;
  case AtomicOrdering::SequentiallyConsistent:
    return RISCV::LR_W_AQ_RL;
  }
}

static unsigned getSCForRMW32(AtomicOrdering Ordering,
                              const RISCVSubtarget *Subtarget) {
  switch (Ordering) {
  default:
    llvm_unreachable("Unexpected AtomicOrdering");
  case AtomicOrdering::Monotonic:
    return RISCV::SC_W;
  case AtomicOrdering::Acquire:
    return RISCV::SC_W;
  case AtomicOrdering::Release:
    if (Subtarget->hasStdExtZtso())
      return RISCV::SC_W;
    return RISCV::SC_W_RL;
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->hasStdExtZtso())
      return RISCV::SC_W;
    return RISCV::SC_W_RL;
  case AtomicOrdering::SequentiallyConsistent:
    return RISCV::SC_W_RL;
  }
}

static unsigned getLRForRMW64(AtomicOrdering Ordering,
                              const RISCVSubtarget *Subtarget) {
  switch (Ordering) {
  default:
    llvm_unreachable("Unexpected AtomicOrdering");
  case AtomicOrdering::Monotonic:
    return RISCV::LR_D;
  case AtomicOrdering::Acquire:
    if (Subtarget->hasStdExtZtso())
      return RISCV::LR_D;
    return RISCV::LR_D_AQ;
  case AtomicOrdering::Release:
    return RISCV::LR_D;
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->hasStdExtZtso())
      return RISCV::LR_D;
    return RISCV::LR_D_AQ;
  case AtomicOrdering::SequentiallyConsistent:
    return RISCV::LR_D_AQ_RL;
  }
}

static unsigned getSCForRMW64(AtomicOrdering Ordering,
                              const RISCVSubtarget *Subtarget) {
  switch (Ordering) {
  default:
    llvm_unreachable("Unexpected AtomicOrdering");
  case AtomicOrdering::Monotonic:
    return RISCV::SC_D;
  case AtomicOrdering::Acquire:
    return RISCV::SC_D;
  case AtomicOrdering::Release:
    if (Subtarget->hasStdExtZtso())
      return RISCV::SC_D;
    return RISCV::SC_D_RL;
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->hasStdExtZtso())
      return RISCV::SC_D;
    return RISCV::SC_D_RL;
  case AtomicOrdering::SequentiallyConsistent:
    return RISCV::SC_D_RL;
  }
}

static unsigned getLRForRMW(AtomicOrdering Ordering, int Width,
                            const RISCVSubtarget *Subtarget) {
  if (Width == 32)
    return getLRForRMW32(Ordering, Subtarget);
  if (Width == 64)
    return getLRForRMW64(Ordering, Subtarget);
  llvm_unreachable("Unexpected LR width\n");
}

static unsigned getSCForRMW(AtomicOrdering Ordering, int Width,
                            const RISCVSubtarget *Subtarget) {
  if (Width == 32)
    return getSCForRMW32(Ordering, Subtarget);
  if (Width == 64)
    return getSCForRMW64(Ordering, Subtarget);
  llvm_unreachable("Unexpected SC width\n");
}

static void doAtomicBinOpExpansion(const RISCVInstrInfo *TII, MachineInstr &MI,
                                   DebugLoc DL, MachineBasicBlock *ThisMBB,
                                   MachineBasicBlock *LoopMBB,
                                   MachineBasicBlock *DoneMBB,
                                   AtomicRMWInst::BinOp BinOp, int Width,
                                   const RISCVSubtarget *STI) {
  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg = MI.getOperand(1).getReg();
  Register AddrReg = MI.getOperand(2).getReg();
  Register IncrReg = MI.getOperand(3).getReg();
  AtomicOrdering Ordering =
      static_cast<AtomicOrdering>(MI.getOperand(4).getImm());

  // .loop:
  //   lr.[w|d] dest, (addr)
  //   binop scratch, dest, val
  //   sc.[w|d] scratch, scratch, (addr)
  //   bnez scratch, loop
  BuildMI(LoopMBB, DL, TII->get(getLRForRMW(Ordering, Width, STI)), DestReg)
      .addReg(AddrReg);
  switch (BinOp) {
  default:
    llvm_unreachable("Unexpected AtomicRMW BinOp");
  case AtomicRMWInst::Nand:
    BuildMI(LoopMBB, DL, TII->get(RISCV::AND), ScratchReg)
        .addReg(DestReg)
        .addReg(IncrReg);
    BuildMI(LoopMBB, DL, TII->get(RISCV::XORI), ScratchReg)
        .addReg(ScratchReg)
        .addImm(-1);
    break;
  }
  BuildMI(LoopMBB, DL, TII->get(getSCForRMW(Ordering, Width, STI)), ScratchReg)
      .addReg(AddrReg)
      .addReg(ScratchReg);
  BuildMI(LoopMBB, DL, TII->get(RISCV::BNE))
      .addReg(ScratchReg)
      .addReg(RISCV::X0)
      .addMBB(LoopMBB);
}

static void insertMaskedMerge(const RISCVInstrInfo *TII, DebugLoc DL,
                              MachineBasicBlock *MBB, Register DestReg,
                              Register OldValReg, Register NewValReg,
                              Register MaskReg, Register ScratchReg) {
  assert(OldValReg != ScratchReg && "OldValReg and ScratchReg must be unique");
  assert(OldValReg != MaskReg && "OldValReg and MaskReg must be unique");
  assert(ScratchReg != MaskReg && "ScratchReg and MaskReg must be unique");

  // We select bits from newval and oldval using:
  // https://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge
  // r = oldval ^ ((oldval ^ newval) & masktargetdata);
  BuildMI(MBB, DL, TII->get(RISCV::XOR), ScratchReg)
      .addReg(OldValReg)
      .addReg(NewValReg);
  BuildMI(MBB, DL, TII->get(RISCV::AND), ScratchReg)
      .addReg(ScratchReg)
      .addReg(MaskReg);
  BuildMI(MBB, DL, TII->get(RISCV::XOR), DestReg)
      .addReg(OldValReg)
      .addReg(ScratchReg);
}

static void doMaskedAtomicBinOpExpansion(const RISCVInstrInfo *TII,
                                         MachineInstr &MI, DebugLoc DL,
                                         MachineBasicBlock *ThisMBB,
                                         MachineBasicBlock *LoopMBB,
                                         MachineBasicBlock *DoneMBB,
                                         AtomicRMWInst::BinOp BinOp, int Width,
                                         const RISCVSubtarget *STI) {
  assert(Width == 32 && "Should never need to expand masked 64-bit operations");
  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg = MI.getOperand(1).getReg();
  Register AddrReg = MI.getOperand(2).getReg();
  Register IncrReg = MI.getOperand(3).getReg();
  Register MaskReg = MI.getOperand(4).getReg();
  AtomicOrdering Ordering =
      static_cast<AtomicOrdering>(MI.getOperand(5).getImm());

  // .loop:
  //   lr.w destreg, (alignedaddr)
  //   binop scratch, destreg, incr
  //   xor scratch, destreg, scratch
  //   and scratch, scratch, masktargetdata
  //   xor scratch, destreg, scratch
  //   sc.w scratch, scratch, (alignedaddr)
  //   bnez scratch, loop
  BuildMI(LoopMBB, DL, TII->get(getLRForRMW32(Ordering, STI)), DestReg)
      .addReg(AddrReg);
  switch (BinOp) {
  default:
    llvm_unreachable("Unexpected AtomicRMW BinOp");
  case AtomicRMWInst::Xchg:
    BuildMI(LoopMBB, DL, TII->get(RISCV::ADDI), ScratchReg)
        .addReg(IncrReg)
        .addImm(0);
    break;
  case AtomicRMWInst::Add:
    BuildMI(LoopMBB, DL, TII->get(RISCV::ADD), ScratchReg)
        .addReg(DestReg)
        .addReg(IncrReg);
    break;
  case AtomicRMWInst::Sub:
    BuildMI(LoopMBB, DL, TII->get(RISCV::SUB), ScratchReg)
        .addReg(DestReg)
        .addReg(IncrReg);
    break;
  case AtomicRMWInst::Nand:
    BuildMI(LoopMBB, DL, TII->get(RISCV::AND), ScratchReg)
        .addReg(DestReg)
        .addReg(IncrReg);
    BuildMI(LoopMBB, DL, TII->get(RISCV::XORI), ScratchReg)
        .addReg(ScratchReg)
        .addImm(-1);
    break;
  }

  insertMaskedMerge(TII, DL, LoopMBB, ScratchReg, DestReg, ScratchReg, MaskReg,
                    ScratchReg);

  BuildMI(LoopMBB, DL, TII->get(getSCForRMW32(Ordering, STI)), ScratchReg)
      .addReg(AddrReg)
      .addReg(ScratchReg);
  BuildMI(LoopMBB, DL, TII->get(RISCV::BNE))
      .addReg(ScratchReg)
      .addReg(RISCV::X0)
      .addMBB(LoopMBB);
}

bool RISCVExpandAtomicPseudo::expandAtomicBinOp(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    AtomicRMWInst::BinOp BinOp, bool IsMasked, int Width,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  MachineFunction *MF = MBB.getParent();
  auto LoopMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  // Insert new MBBs.
  MF->insert(++MBB.getIterator(), LoopMBB);
  MF->insert(++LoopMBB->getIterator(), DoneMBB);

  // Set up successors and transfer remaining instructions to DoneMBB.
  LoopMBB->addSuccessor(LoopMBB);
  LoopMBB->addSuccessor(DoneMBB);
  DoneMBB->splice(DoneMBB->end(), &MBB, MI, MBB.end());
  DoneMBB->transferSuccessors(&MBB);
  MBB.addSuccessor(LoopMBB);

  if (!IsMasked)
    doAtomicBinOpExpansion(TII, MI, DL, &MBB, LoopMBB, DoneMBB, BinOp, Width,
                           STI);
  else
    doMaskedAtomicBinOpExpansion(TII, MI, DL, &MBB, LoopMBB, DoneMBB, BinOp,
                                 Width, STI);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *LoopMBB);
  computeAndAddLiveIns(LiveRegs, *DoneMBB);

  return true;
}

static void insertSext(const RISCVInstrInfo *TII, DebugLoc DL,
                       MachineBasicBlock *MBB, Register ValReg,
                       Register ShamtReg) {
  BuildMI(MBB, DL, TII->get(RISCV::SLL), ValReg)
      .addReg(ValReg)
      .addReg(ShamtReg);
  BuildMI(MBB, DL, TII->get(RISCV::SRA), ValReg)
      .addReg(ValReg)
      .addReg(ShamtReg);
}

bool RISCVExpandAtomicPseudo::expandAtomicMinMaxOp(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    AtomicRMWInst::BinOp BinOp, bool IsMasked, int Width,
    MachineBasicBlock::iterator &NextMBBI) {
  assert(IsMasked == true &&
         "Should only need to expand masked atomic max/min");
  assert(Width == 32 && "Should never need to expand masked 64-bit operations");

  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB.getParent();
  auto LoopHeadMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto LoopIfBodyMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto LoopTailMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  // Insert new MBBs.
  MF->insert(++MBB.getIterator(), LoopHeadMBB);
  MF->insert(++LoopHeadMBB->getIterator(), LoopIfBodyMBB);
  MF->insert(++LoopIfBodyMBB->getIterator(), LoopTailMBB);
  MF->insert(++LoopTailMBB->getIterator(), DoneMBB);

  // Set up successors and transfer remaining instructions to DoneMBB.
  LoopHeadMBB->addSuccessor(LoopIfBodyMBB);
  LoopHeadMBB->addSuccessor(LoopTailMBB);
  LoopIfBodyMBB->addSuccessor(LoopTailMBB);
  LoopTailMBB->addSuccessor(LoopHeadMBB);
  LoopTailMBB->addSuccessor(DoneMBB);
  DoneMBB->splice(DoneMBB->end(), &MBB, MI, MBB.end());
  DoneMBB->transferSuccessors(&MBB);
  MBB.addSuccessor(LoopHeadMBB);

  Register DestReg = MI.getOperand(0).getReg();
  Register Scratch1Reg = MI.getOperand(1).getReg();
  Register Scratch2Reg = MI.getOperand(2).getReg();
  Register AddrReg = MI.getOperand(3).getReg();
  Register IncrReg = MI.getOperand(4).getReg();
  Register MaskReg = MI.getOperand(5).getReg();
  bool IsSigned = BinOp == AtomicRMWInst::Min || BinOp == AtomicRMWInst::Max;
  AtomicOrdering Ordering =
      static_cast<AtomicOrdering>(MI.getOperand(IsSigned ? 7 : 6).getImm());

  //
  // .loophead:
  //   lr.w destreg, (alignedaddr)
  //   and scratch2, destreg, mask
  //   mv scratch1, destreg
  //   [sext scratch2 if signed min/max]
  //   ifnochangeneeded scratch2, incr, .looptail
  BuildMI(LoopHeadMBB, DL, TII->get(getLRForRMW32(Ordering, STI)), DestReg)
      .addReg(AddrReg);
  BuildMI(LoopHeadMBB, DL, TII->get(RISCV::AND), Scratch2Reg)
      .addReg(DestReg)
      .addReg(MaskReg);
  BuildMI(LoopHeadMBB, DL, TII->get(RISCV::ADDI), Scratch1Reg)
      .addReg(DestReg)
      .addImm(0);

  switch (BinOp) {
  default:
    llvm_unreachable("Unexpected AtomicRMW BinOp");
  case AtomicRMWInst::Max: {
    insertSext(TII, DL, LoopHeadMBB, Scratch2Reg, MI.getOperand(6).getReg());
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BGE))
        .addReg(Scratch2Reg)
        .addReg(IncrReg)
        .addMBB(LoopTailMBB);
    break;
  }
  case AtomicRMWInst::Min: {
    insertSext(TII, DL, LoopHeadMBB, Scratch2Reg, MI.getOperand(6).getReg());
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BGE))
        .addReg(IncrReg)
        .addReg(Scratch2Reg)
        .addMBB(LoopTailMBB);
    break;
  }
  case AtomicRMWInst::UMax:
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BGEU))
        .addReg(Scratch2Reg)
        .addReg(IncrReg)
        .addMBB(LoopTailMBB);
    break;
  case AtomicRMWInst::UMin:
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BGEU))
        .addReg(IncrReg)
        .addReg(Scratch2Reg)
        .addMBB(LoopTailMBB);
    break;
  }

  // .loopifbody:
  //   xor scratch1, destreg, incr
  //   and scratch1, scratch1, mask
  //   xor scratch1, destreg, scratch1
  insertMaskedMerge(TII, DL, LoopIfBodyMBB, Scratch1Reg, DestReg, IncrReg,
                    MaskReg, Scratch1Reg);

  // .looptail:
  //   sc.w scratch1, scratch1, (addr)
  //   bnez scratch1, loop
  BuildMI(LoopTailMBB, DL, TII->get(getSCForRMW32(Ordering, STI)), Scratch1Reg)
      .addReg(AddrReg)
      .addReg(Scratch1Reg);
  BuildMI(LoopTailMBB, DL, TII->get(RISCV::BNE))
      .addReg(Scratch1Reg)
      .addReg(RISCV::X0)
      .addMBB(LoopHeadMBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *LoopHeadMBB);
  computeAndAddLiveIns(LiveRegs, *LoopIfBodyMBB);
  computeAndAddLiveIns(LiveRegs, *LoopTailMBB);
  computeAndAddLiveIns(LiveRegs, *DoneMBB);

  return true;
}

// If a BNE on the cmpxchg comparison result immediately follows the cmpxchg
// operation, it can be folded into the cmpxchg expansion by
// modifying the branch within 'LoopHead' (which performs the same
// comparison). This is a valid transformation because after altering the
// LoopHead's BNE destination, the BNE following the cmpxchg becomes
// redundant and and be deleted. In the case of a masked cmpxchg, an
// appropriate AND and BNE must be matched.
//
// On success, returns true and deletes the matching BNE or AND+BNE, sets the
// LoopHeadBNETarget argument to the target that should be used within the
// loop head, and removes that block as a successor to MBB.
bool tryToFoldBNEOnCmpXchgResult(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 Register DestReg, Register CmpValReg,
                                 Register MaskReg,
                                 MachineBasicBlock *&LoopHeadBNETarget) {
  SmallVector<MachineInstr *> ToErase;
  auto E = MBB.end();
  if (MBBI == E)
    return false;
  MBBI = skipDebugInstructionsForward(MBBI, E);

  // If we have a masked cmpxchg, match AND dst, DestReg, MaskReg.
  if (MaskReg.isValid()) {
    if (MBBI == E || MBBI->getOpcode() != RISCV::AND)
      return false;
    Register ANDOp1 = MBBI->getOperand(1).getReg();
    Register ANDOp2 = MBBI->getOperand(2).getReg();
    if (!(ANDOp1 == DestReg && ANDOp2 == MaskReg) &&
        !(ANDOp1 == MaskReg && ANDOp2 == DestReg))
      return false;
    // We now expect the BNE to use the result of the AND as an operand.
    DestReg = MBBI->getOperand(0).getReg();
    ToErase.push_back(&*MBBI);
    MBBI = skipDebugInstructionsForward(std::next(MBBI), E);
  }

  // Match BNE DestReg, MaskReg.
  if (MBBI == E || MBBI->getOpcode() != RISCV::BNE)
    return false;
  Register BNEOp0 = MBBI->getOperand(0).getReg();
  Register BNEOp1 = MBBI->getOperand(1).getReg();
  if (!(BNEOp0 == DestReg && BNEOp1 == CmpValReg) &&
      !(BNEOp0 == CmpValReg && BNEOp1 == DestReg))
    return false;

  // Make sure the branch is the only user of the AND.
  if (MaskReg.isValid()) {
    if (BNEOp0 == DestReg && !MBBI->getOperand(0).isKill())
      return false;
    if (BNEOp1 == DestReg && !MBBI->getOperand(1).isKill())
      return false;
  }

  ToErase.push_back(&*MBBI);
  LoopHeadBNETarget = MBBI->getOperand(2).getMBB();
  MBBI = skipDebugInstructionsForward(std::next(MBBI), E);
  if (MBBI != E)
    return false;

  MBB.removeSuccessor(LoopHeadBNETarget);
  for (auto *MI : ToErase)
    MI->eraseFromParent();
  return true;
}

bool RISCVExpandAtomicPseudo::expandAtomicCmpXchg(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, bool IsMasked,
    int Width, MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB.getParent();
  auto LoopHeadMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto LoopTailMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneMBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg = MI.getOperand(1).getReg();
  Register AddrReg = MI.getOperand(2).getReg();
  Register CmpValReg = MI.getOperand(3).getReg();
  Register NewValReg = MI.getOperand(4).getReg();
  Register MaskReg = IsMasked ? MI.getOperand(5).getReg() : Register();

  MachineBasicBlock *LoopHeadBNETarget = DoneMBB;
  tryToFoldBNEOnCmpXchgResult(MBB, std::next(MBBI), DestReg, CmpValReg, MaskReg,
                              LoopHeadBNETarget);

  // Insert new MBBs.
  MF->insert(++MBB.getIterator(), LoopHeadMBB);
  MF->insert(++LoopHeadMBB->getIterator(), LoopTailMBB);
  MF->insert(++LoopTailMBB->getIterator(), DoneMBB);

  // Set up successors and transfer remaining instructions to DoneMBB.
  LoopHeadMBB->addSuccessor(LoopTailMBB);
  LoopHeadMBB->addSuccessor(LoopHeadBNETarget);
  LoopTailMBB->addSuccessor(DoneMBB);
  LoopTailMBB->addSuccessor(LoopHeadMBB);
  DoneMBB->splice(DoneMBB->end(), &MBB, MI, MBB.end());
  DoneMBB->transferSuccessors(&MBB);
  MBB.addSuccessor(LoopHeadMBB);

  AtomicOrdering Ordering =
      static_cast<AtomicOrdering>(MI.getOperand(IsMasked ? 6 : 5).getImm());

  if (!IsMasked) {
    // .loophead:
    //   lr.[w|d] dest, (addr)
    //   bne dest, cmpval, done
    BuildMI(LoopHeadMBB, DL, TII->get(getLRForRMW(Ordering, Width, STI)),
            DestReg)
        .addReg(AddrReg);
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BNE))
        .addReg(DestReg)
        .addReg(CmpValReg)
        .addMBB(LoopHeadBNETarget);
    // .looptail:
    //   sc.[w|d] scratch, newval, (addr)
    //   bnez scratch, loophead
    BuildMI(LoopTailMBB, DL, TII->get(getSCForRMW(Ordering, Width, STI)),
            ScratchReg)
        .addReg(AddrReg)
        .addReg(NewValReg);
    BuildMI(LoopTailMBB, DL, TII->get(RISCV::BNE))
        .addReg(ScratchReg)
        .addReg(RISCV::X0)
        .addMBB(LoopHeadMBB);
  } else {
    // .loophead:
    //   lr.w dest, (addr)
    //   and scratch, dest, mask
    //   bne scratch, cmpval, done
    Register MaskReg = MI.getOperand(5).getReg();
    BuildMI(LoopHeadMBB, DL, TII->get(getLRForRMW(Ordering, Width, STI)),
            DestReg)
        .addReg(AddrReg);
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::AND), ScratchReg)
        .addReg(DestReg)
        .addReg(MaskReg);
    BuildMI(LoopHeadMBB, DL, TII->get(RISCV::BNE))
        .addReg(ScratchReg)
        .addReg(CmpValReg)
        .addMBB(LoopHeadBNETarget);

    // .looptail:
    //   xor scratch, dest, newval
    //   and scratch, scratch, mask
    //   xor scratch, dest, scratch
    //   sc.w scratch, scratch, (adrr)
    //   bnez scratch, loophead
    insertMaskedMerge(TII, DL, LoopTailMBB, ScratchReg, DestReg, NewValReg,
                      MaskReg, ScratchReg);
    BuildMI(LoopTailMBB, DL, TII->get(getSCForRMW(Ordering, Width, STI)),
            ScratchReg)
        .addReg(AddrReg)
        .addReg(ScratchReg);
    BuildMI(LoopTailMBB, DL, TII->get(RISCV::BNE))
        .addReg(ScratchReg)
        .addReg(RISCV::X0)
        .addMBB(LoopHeadMBB);
  }

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *LoopHeadMBB);
  computeAndAddLiveIns(LiveRegs, *LoopTailMBB);
  computeAndAddLiveIns(LiveRegs, *DoneMBB);

  return true;
}

} // end of anonymous namespace

INITIALIZE_PASS(RISCVExpandAtomicPseudo, "riscv-expand-atomic-pseudo",
                RISCV_EXPAND_ATOMIC_PSEUDO_NAME, false, false)

namespace llvm {

FunctionPass *createRISCVExpandAtomicPseudoPass() {
  return new RISCVExpandAtomicPseudo();
}

} // end of namespace llvm
