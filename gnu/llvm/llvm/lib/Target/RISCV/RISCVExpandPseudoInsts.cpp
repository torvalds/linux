//===-- RISCVExpandPseudoInsts.cpp - Expand pseudo instructions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions. This pass should be run after register allocation but before
// the post-regalloc scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVInstrInfo.h"
#include "RISCVTargetMachine.h"

#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/MC/MCContext.h"

using namespace llvm;

#define RISCV_EXPAND_PSEUDO_NAME "RISC-V pseudo instruction expansion pass"
#define RISCV_PRERA_EXPAND_PSEUDO_NAME "RISC-V Pre-RA pseudo instruction expansion pass"

namespace {

class RISCVExpandPseudo : public MachineFunctionPass {
public:
  const RISCVSubtarget *STI;
  const RISCVInstrInfo *TII;
  static char ID;

  RISCVExpandPseudo() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return RISCV_EXPAND_PSEUDO_NAME; }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandCCOp(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                  MachineBasicBlock::iterator &NextMBBI);
  bool expandVSetVL(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI);
  bool expandVMSET_VMCLR(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, unsigned Opcode);
  bool expandRV32ZdinxStore(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI);
  bool expandRV32ZdinxLoad(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI);
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

char RISCVExpandPseudo::ID = 0;

bool RISCVExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
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

bool RISCVExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool RISCVExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 MachineBasicBlock::iterator &NextMBBI) {
  // RISCVInstrInfo::getInstSizeInBytes expects that the total size of the
  // expanded instructions for each pseudo is correct in the Size field of the
  // tablegen definition for the pseudo.
  switch (MBBI->getOpcode()) {
  case RISCV::PseudoRV32ZdinxSD:
    return expandRV32ZdinxStore(MBB, MBBI);
  case RISCV::PseudoRV32ZdinxLD:
    return expandRV32ZdinxLoad(MBB, MBBI);
  case RISCV::PseudoCCMOVGPRNoX0:
  case RISCV::PseudoCCMOVGPR:
  case RISCV::PseudoCCADD:
  case RISCV::PseudoCCSUB:
  case RISCV::PseudoCCAND:
  case RISCV::PseudoCCOR:
  case RISCV::PseudoCCXOR:
  case RISCV::PseudoCCADDW:
  case RISCV::PseudoCCSUBW:
  case RISCV::PseudoCCSLL:
  case RISCV::PseudoCCSRL:
  case RISCV::PseudoCCSRA:
  case RISCV::PseudoCCADDI:
  case RISCV::PseudoCCSLLI:
  case RISCV::PseudoCCSRLI:
  case RISCV::PseudoCCSRAI:
  case RISCV::PseudoCCANDI:
  case RISCV::PseudoCCORI:
  case RISCV::PseudoCCXORI:
  case RISCV::PseudoCCSLLW:
  case RISCV::PseudoCCSRLW:
  case RISCV::PseudoCCSRAW:
  case RISCV::PseudoCCADDIW:
  case RISCV::PseudoCCSLLIW:
  case RISCV::PseudoCCSRLIW:
  case RISCV::PseudoCCSRAIW:
  case RISCV::PseudoCCANDN:
  case RISCV::PseudoCCORN:
  case RISCV::PseudoCCXNOR:
    return expandCCOp(MBB, MBBI, NextMBBI);
  case RISCV::PseudoVSETVLI:
  case RISCV::PseudoVSETVLIX0:
  case RISCV::PseudoVSETIVLI:
    return expandVSetVL(MBB, MBBI);
  case RISCV::PseudoVMCLR_M_B1:
  case RISCV::PseudoVMCLR_M_B2:
  case RISCV::PseudoVMCLR_M_B4:
  case RISCV::PseudoVMCLR_M_B8:
  case RISCV::PseudoVMCLR_M_B16:
  case RISCV::PseudoVMCLR_M_B32:
  case RISCV::PseudoVMCLR_M_B64:
    // vmclr.m vd => vmxor.mm vd, vd, vd
    return expandVMSET_VMCLR(MBB, MBBI, RISCV::VMXOR_MM);
  case RISCV::PseudoVMSET_M_B1:
  case RISCV::PseudoVMSET_M_B2:
  case RISCV::PseudoVMSET_M_B4:
  case RISCV::PseudoVMSET_M_B8:
  case RISCV::PseudoVMSET_M_B16:
  case RISCV::PseudoVMSET_M_B32:
  case RISCV::PseudoVMSET_M_B64:
    // vmset.m vd => vmxnor.mm vd, vd, vd
    return expandVMSET_VMCLR(MBB, MBBI, RISCV::VMXNOR_MM);
  }

  return false;
}

bool RISCVExpandPseudo::expandCCOp(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI) {

  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  MachineBasicBlock *TrueBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  MachineBasicBlock *MergeBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), TrueBB);
  MF->insert(++TrueBB->getIterator(), MergeBB);

  // We want to copy the "true" value when the condition is true which means
  // we need to invert the branch condition to jump over TrueBB when the
  // condition is false.
  auto CC = static_cast<RISCVCC::CondCode>(MI.getOperand(3).getImm());
  CC = RISCVCC::getOppositeBranchCondition(CC);

  // Insert branch instruction.
  BuildMI(MBB, MBBI, DL, TII->getBrCond(CC))
      .addReg(MI.getOperand(1).getReg())
      .addReg(MI.getOperand(2).getReg())
      .addMBB(MergeBB);

  Register DestReg = MI.getOperand(0).getReg();
  assert(MI.getOperand(4).getReg() == DestReg);

  if (MI.getOpcode() == RISCV::PseudoCCMOVGPR ||
      MI.getOpcode() == RISCV::PseudoCCMOVGPRNoX0) {
    // Add MV.
    BuildMI(TrueBB, DL, TII->get(RISCV::ADDI), DestReg)
        .add(MI.getOperand(5))
        .addImm(0);
  } else {
    unsigned NewOpc;
    switch (MI.getOpcode()) {
    default:
      llvm_unreachable("Unexpected opcode!");
    case RISCV::PseudoCCADD:   NewOpc = RISCV::ADD;   break;
    case RISCV::PseudoCCSUB:   NewOpc = RISCV::SUB;   break;
    case RISCV::PseudoCCSLL:   NewOpc = RISCV::SLL;   break;
    case RISCV::PseudoCCSRL:   NewOpc = RISCV::SRL;   break;
    case RISCV::PseudoCCSRA:   NewOpc = RISCV::SRA;   break;
    case RISCV::PseudoCCAND:   NewOpc = RISCV::AND;   break;
    case RISCV::PseudoCCOR:    NewOpc = RISCV::OR;    break;
    case RISCV::PseudoCCXOR:   NewOpc = RISCV::XOR;   break;
    case RISCV::PseudoCCADDI:  NewOpc = RISCV::ADDI;  break;
    case RISCV::PseudoCCSLLI:  NewOpc = RISCV::SLLI;  break;
    case RISCV::PseudoCCSRLI:  NewOpc = RISCV::SRLI;  break;
    case RISCV::PseudoCCSRAI:  NewOpc = RISCV::SRAI;  break;
    case RISCV::PseudoCCANDI:  NewOpc = RISCV::ANDI;  break;
    case RISCV::PseudoCCORI:   NewOpc = RISCV::ORI;   break;
    case RISCV::PseudoCCXORI:  NewOpc = RISCV::XORI;  break;
    case RISCV::PseudoCCADDW:  NewOpc = RISCV::ADDW;  break;
    case RISCV::PseudoCCSUBW:  NewOpc = RISCV::SUBW;  break;
    case RISCV::PseudoCCSLLW:  NewOpc = RISCV::SLLW;  break;
    case RISCV::PseudoCCSRLW:  NewOpc = RISCV::SRLW;  break;
    case RISCV::PseudoCCSRAW:  NewOpc = RISCV::SRAW;  break;
    case RISCV::PseudoCCADDIW: NewOpc = RISCV::ADDIW; break;
    case RISCV::PseudoCCSLLIW: NewOpc = RISCV::SLLIW; break;
    case RISCV::PseudoCCSRLIW: NewOpc = RISCV::SRLIW; break;
    case RISCV::PseudoCCSRAIW: NewOpc = RISCV::SRAIW; break;
    case RISCV::PseudoCCANDN:  NewOpc = RISCV::ANDN;  break;
    case RISCV::PseudoCCORN:   NewOpc = RISCV::ORN;   break;
    case RISCV::PseudoCCXNOR:  NewOpc = RISCV::XNOR;  break;
    }
    BuildMI(TrueBB, DL, TII->get(NewOpc), DestReg)
        .add(MI.getOperand(5))
        .add(MI.getOperand(6));
  }

  TrueBB->addSuccessor(MergeBB);

  MergeBB->splice(MergeBB->end(), &MBB, MI, MBB.end());
  MergeBB->transferSuccessors(&MBB);

  MBB.addSuccessor(TrueBB);
  MBB.addSuccessor(MergeBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Make sure live-ins are correctly attached to this new basic block.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *TrueBB);
  computeAndAddLiveIns(LiveRegs, *MergeBB);

  return true;
}

bool RISCVExpandPseudo::expandVSetVL(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI) {
  assert(MBBI->getNumExplicitOperands() == 3 && MBBI->getNumOperands() >= 5 &&
         "Unexpected instruction format");

  DebugLoc DL = MBBI->getDebugLoc();

  assert((MBBI->getOpcode() == RISCV::PseudoVSETVLI ||
          MBBI->getOpcode() == RISCV::PseudoVSETVLIX0 ||
          MBBI->getOpcode() == RISCV::PseudoVSETIVLI) &&
         "Unexpected pseudo instruction");
  unsigned Opcode;
  if (MBBI->getOpcode() == RISCV::PseudoVSETIVLI)
    Opcode = RISCV::VSETIVLI;
  else
    Opcode = RISCV::VSETVLI;
  const MCInstrDesc &Desc = TII->get(Opcode);
  assert(Desc.getNumOperands() == 3 && "Unexpected instruction format");

  Register DstReg = MBBI->getOperand(0).getReg();
  bool DstIsDead = MBBI->getOperand(0).isDead();
  BuildMI(MBB, MBBI, DL, Desc)
      .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead))
      .add(MBBI->getOperand(1))  // VL
      .add(MBBI->getOperand(2)); // VType

  MBBI->eraseFromParent(); // The pseudo instruction is gone now.
  return true;
}

bool RISCVExpandPseudo::expandVMSET_VMCLR(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator MBBI,
                                          unsigned Opcode) {
  DebugLoc DL = MBBI->getDebugLoc();
  Register DstReg = MBBI->getOperand(0).getReg();
  const MCInstrDesc &Desc = TII->get(Opcode);
  BuildMI(MBB, MBBI, DL, Desc, DstReg)
      .addReg(DstReg, RegState::Undef)
      .addReg(DstReg, RegState::Undef);
  MBBI->eraseFromParent(); // The pseudo instruction is gone now.
  return true;
}

// This function expands the PseudoRV32ZdinxSD for storing a double-precision
// floating-point value into memory by generating an equivalent instruction
// sequence for RV32.
bool RISCVExpandPseudo::expandRV32ZdinxStore(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MBBI) {
  DebugLoc DL = MBBI->getDebugLoc();
  const TargetRegisterInfo *TRI = STI->getRegisterInfo();
  Register Lo =
      TRI->getSubReg(MBBI->getOperand(0).getReg(), RISCV::sub_gpr_even);
  Register Hi =
      TRI->getSubReg(MBBI->getOperand(0).getReg(), RISCV::sub_gpr_odd);

  assert(MBBI->hasOneMemOperand() && "Expected mem operand");
  MachineMemOperand *OldMMO = MBBI->memoperands().front();
  MachineFunction *MF = MBB.getParent();
  MachineMemOperand *MMOLo = MF->getMachineMemOperand(OldMMO, 0, 4);
  MachineMemOperand *MMOHi = MF->getMachineMemOperand(OldMMO, 4, 4);

  BuildMI(MBB, MBBI, DL, TII->get(RISCV::SW))
      .addReg(Lo, getKillRegState(MBBI->getOperand(0).isKill()))
      .addReg(MBBI->getOperand(1).getReg())
      .add(MBBI->getOperand(2))
      .setMemRefs(MMOLo);

  if (MBBI->getOperand(2).isGlobal() || MBBI->getOperand(2).isCPI()) {
    // FIXME: Zdinx RV32 can not work on unaligned scalar memory.
    assert(!STI->enableUnalignedScalarMem());

    assert(MBBI->getOperand(2).getOffset() % 8 == 0);
    MBBI->getOperand(2).setOffset(MBBI->getOperand(2).getOffset() + 4);
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::SW))
        .addReg(Hi, getKillRegState(MBBI->getOperand(0).isKill()))
        .add(MBBI->getOperand(1))
        .add(MBBI->getOperand(2))
        .setMemRefs(MMOHi);
  } else {
    assert(isInt<12>(MBBI->getOperand(2).getImm() + 4));
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::SW))
        .addReg(Hi, getKillRegState(MBBI->getOperand(0).isKill()))
        .add(MBBI->getOperand(1))
        .addImm(MBBI->getOperand(2).getImm() + 4)
        .setMemRefs(MMOHi);
  }
  MBBI->eraseFromParent();
  return true;
}

// This function expands PseudoRV32ZdinxLoad for loading a double-precision
// floating-point value from memory into an equivalent instruction sequence for
// RV32.
bool RISCVExpandPseudo::expandRV32ZdinxLoad(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MBBI) {
  DebugLoc DL = MBBI->getDebugLoc();
  const TargetRegisterInfo *TRI = STI->getRegisterInfo();
  Register Lo =
      TRI->getSubReg(MBBI->getOperand(0).getReg(), RISCV::sub_gpr_even);
  Register Hi =
      TRI->getSubReg(MBBI->getOperand(0).getReg(), RISCV::sub_gpr_odd);

  assert(MBBI->hasOneMemOperand() && "Expected mem operand");
  MachineMemOperand *OldMMO = MBBI->memoperands().front();
  MachineFunction *MF = MBB.getParent();
  MachineMemOperand *MMOLo = MF->getMachineMemOperand(OldMMO, 0, 4);
  MachineMemOperand *MMOHi = MF->getMachineMemOperand(OldMMO, 4, 4);

  // If the register of operand 1 is equal to the Lo register, then swap the
  // order of loading the Lo and Hi statements.
  bool IsOp1EqualToLo = Lo == MBBI->getOperand(1).getReg();
  // Order: Lo, Hi
  if (!IsOp1EqualToLo) {
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::LW), Lo)
        .addReg(MBBI->getOperand(1).getReg())
        .add(MBBI->getOperand(2))
        .setMemRefs(MMOLo);
  }

  if (MBBI->getOperand(2).isGlobal() || MBBI->getOperand(2).isCPI()) {
    auto Offset = MBBI->getOperand(2).getOffset();
    assert(MBBI->getOperand(2).getOffset() % 8 == 0);
    MBBI->getOperand(2).setOffset(Offset + 4);
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::LW), Hi)
        .addReg(MBBI->getOperand(1).getReg())
        .add(MBBI->getOperand(2))
        .setMemRefs(MMOHi);
    MBBI->getOperand(2).setOffset(Offset);
  } else {
    assert(isInt<12>(MBBI->getOperand(2).getImm() + 4));
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::LW), Hi)
        .addReg(MBBI->getOperand(1).getReg())
        .addImm(MBBI->getOperand(2).getImm() + 4)
        .setMemRefs(MMOHi);
  }

  // Order: Hi, Lo
  if (IsOp1EqualToLo) {
    BuildMI(MBB, MBBI, DL, TII->get(RISCV::LW), Lo)
        .addReg(MBBI->getOperand(1).getReg())
        .add(MBBI->getOperand(2))
        .setMemRefs(MMOLo);
  }

  MBBI->eraseFromParent();
  return true;
}

class RISCVPreRAExpandPseudo : public MachineFunctionPass {
public:
  const RISCVSubtarget *STI;
  const RISCVInstrInfo *TII;
  static char ID;

  RISCVPreRAExpandPseudo() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  StringRef getPassName() const override {
    return RISCV_PRERA_EXPAND_PSEUDO_NAME;
  }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandAuipcInstPair(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI,
                           MachineBasicBlock::iterator &NextMBBI,
                           unsigned FlagsHi, unsigned SecondOpcode);
  bool expandLoadLocalAddress(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadGlobalAddress(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadTLSIEAddress(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadTLSGDAddress(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadTLSDescAddress(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                MachineBasicBlock::iterator &NextMBBI);

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

char RISCVPreRAExpandPseudo::ID = 0;

bool RISCVPreRAExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
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

bool RISCVPreRAExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool RISCVPreRAExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MBBI,
                                      MachineBasicBlock::iterator &NextMBBI) {

  switch (MBBI->getOpcode()) {
  case RISCV::PseudoLLA:
    return expandLoadLocalAddress(MBB, MBBI, NextMBBI);
  case RISCV::PseudoLGA:
    return expandLoadGlobalAddress(MBB, MBBI, NextMBBI);
  case RISCV::PseudoLA_TLS_IE:
    return expandLoadTLSIEAddress(MBB, MBBI, NextMBBI);
  case RISCV::PseudoLA_TLS_GD:
    return expandLoadTLSGDAddress(MBB, MBBI, NextMBBI);
  case RISCV::PseudoLA_TLSDESC:
    return expandLoadTLSDescAddress(MBB, MBBI, NextMBBI);
  }
  return false;
}

bool RISCVPreRAExpandPseudo::expandAuipcInstPair(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI, unsigned FlagsHi,
    unsigned SecondOpcode) {
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg =
      MF->getRegInfo().createVirtualRegister(&RISCV::GPRRegClass);

  MachineOperand &Symbol = MI.getOperand(1);
  Symbol.setTargetFlags(FlagsHi);
  MCSymbol *AUIPCSymbol = MF->getContext().createNamedTempSymbol("pcrel_hi");

  MachineInstr *MIAUIPC =
      BuildMI(MBB, MBBI, DL, TII->get(RISCV::AUIPC), ScratchReg).add(Symbol);
  MIAUIPC->setPreInstrSymbol(*MF, AUIPCSymbol);

  MachineInstr *SecondMI =
      BuildMI(MBB, MBBI, DL, TII->get(SecondOpcode), DestReg)
          .addReg(ScratchReg)
          .addSym(AUIPCSymbol, RISCVII::MO_PCREL_LO);

  if (MI.hasOneMemOperand())
    SecondMI->addMemOperand(*MF, *MI.memoperands_begin());

  MI.eraseFromParent();
  return true;
}

bool RISCVPreRAExpandPseudo::expandLoadLocalAddress(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  return expandAuipcInstPair(MBB, MBBI, NextMBBI, RISCVII::MO_PCREL_HI,
                             RISCV::ADDI);
}

bool RISCVPreRAExpandPseudo::expandLoadGlobalAddress(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  unsigned SecondOpcode = STI->is64Bit() ? RISCV::LD : RISCV::LW;
  return expandAuipcInstPair(MBB, MBBI, NextMBBI, RISCVII::MO_GOT_HI,
                             SecondOpcode);
}

bool RISCVPreRAExpandPseudo::expandLoadTLSIEAddress(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  unsigned SecondOpcode = STI->is64Bit() ? RISCV::LD : RISCV::LW;
  return expandAuipcInstPair(MBB, MBBI, NextMBBI, RISCVII::MO_TLS_GOT_HI,
                             SecondOpcode);
}

bool RISCVPreRAExpandPseudo::expandLoadTLSGDAddress(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  return expandAuipcInstPair(MBB, MBBI, NextMBBI, RISCVII::MO_TLS_GD_HI,
                             RISCV::ADDI);
}

bool RISCVPreRAExpandPseudo::expandLoadTLSDescAddress(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  const auto &STI = MF->getSubtarget<RISCVSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? RISCV::LD : RISCV::LW;

  Register FinalReg = MI.getOperand(0).getReg();
  Register DestReg =
      MF->getRegInfo().createVirtualRegister(&RISCV::GPRRegClass);
  Register ScratchReg =
      MF->getRegInfo().createVirtualRegister(&RISCV::GPRRegClass);

  MachineOperand &Symbol = MI.getOperand(1);
  Symbol.setTargetFlags(RISCVII::MO_TLSDESC_HI);
  MCSymbol *AUIPCSymbol = MF->getContext().createNamedTempSymbol("tlsdesc_hi");

  MachineInstr *MIAUIPC =
      BuildMI(MBB, MBBI, DL, TII->get(RISCV::AUIPC), ScratchReg).add(Symbol);
  MIAUIPC->setPreInstrSymbol(*MF, AUIPCSymbol);

  BuildMI(MBB, MBBI, DL, TII->get(SecondOpcode), DestReg)
      .addReg(ScratchReg)
      .addSym(AUIPCSymbol, RISCVII::MO_TLSDESC_LOAD_LO);

  BuildMI(MBB, MBBI, DL, TII->get(RISCV::ADDI), RISCV::X10)
      .addReg(ScratchReg)
      .addSym(AUIPCSymbol, RISCVII::MO_TLSDESC_ADD_LO);

  BuildMI(MBB, MBBI, DL, TII->get(RISCV::PseudoTLSDESCCall), RISCV::X5)
      .addReg(DestReg)
      .addImm(0)
      .addSym(AUIPCSymbol, RISCVII::MO_TLSDESC_CALL);

  BuildMI(MBB, MBBI, DL, TII->get(RISCV::ADD), FinalReg)
      .addReg(RISCV::X10)
      .addReg(RISCV::X4);

  MI.eraseFromParent();
  return true;
}

} // end of anonymous namespace

INITIALIZE_PASS(RISCVExpandPseudo, "riscv-expand-pseudo",
                RISCV_EXPAND_PSEUDO_NAME, false, false)

INITIALIZE_PASS(RISCVPreRAExpandPseudo, "riscv-prera-expand-pseudo",
                RISCV_PRERA_EXPAND_PSEUDO_NAME, false, false)

namespace llvm {

FunctionPass *createRISCVExpandPseudoPass() { return new RISCVExpandPseudo(); }
FunctionPass *createRISCVPreRAExpandPseudoPass() { return new RISCVPreRAExpandPseudo(); }

} // end of namespace llvm
