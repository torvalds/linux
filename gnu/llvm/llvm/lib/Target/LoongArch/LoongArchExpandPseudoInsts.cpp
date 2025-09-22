//===-- LoongArchExpandPseudoInsts.cpp - Expand pseudo instructions -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions.
//
//===----------------------------------------------------------------------===//

#include "LoongArch.h"
#include "LoongArchInstrInfo.h"
#include "LoongArchTargetMachine.h"
#include "MCTargetDesc/LoongArchBaseInfo.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

#define LOONGARCH_PRERA_EXPAND_PSEUDO_NAME                                     \
  "LoongArch Pre-RA pseudo instruction expansion pass"
#define LOONGARCH_EXPAND_PSEUDO_NAME                                           \
  "LoongArch pseudo instruction expansion pass"

namespace {

class LoongArchPreRAExpandPseudo : public MachineFunctionPass {
public:
  const LoongArchInstrInfo *TII;
  static char ID;

  LoongArchPreRAExpandPseudo() : MachineFunctionPass(ID) {
    initializeLoongArchPreRAExpandPseudoPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
  StringRef getPassName() const override {
    return LOONGARCH_PRERA_EXPAND_PSEUDO_NAME;
  }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandPcalau12iInstPair(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               MachineBasicBlock::iterator &NextMBBI,
                               unsigned FlagsHi, unsigned SecondOpcode,
                               unsigned FlagsLo);
  bool expandLoadAddressPcrel(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressGot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSLE(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSIE(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSLD(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSGD(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSDesc(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                MachineBasicBlock::iterator &NextMBBI);
};

char LoongArchPreRAExpandPseudo::ID = 0;

bool LoongArchPreRAExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII =
      static_cast<const LoongArchInstrInfo *>(MF.getSubtarget().getInstrInfo());
  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= expandMBB(MBB);
  return Modified;
}

bool LoongArchPreRAExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool LoongArchPreRAExpandPseudo::expandMI(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  switch (MBBI->getOpcode()) {
  case LoongArch::PseudoLA_PCREL:
    return expandLoadAddressPcrel(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_GOT:
    return expandLoadAddressGot(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_LE:
    return expandLoadAddressTLSLE(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_IE:
    return expandLoadAddressTLSIE(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_LD:
    return expandLoadAddressTLSLD(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_GD:
    return expandLoadAddressTLSGD(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_DESC_PC:
    return expandLoadAddressTLSDesc(MBB, MBBI, NextMBBI);
  }
  return false;
}

bool LoongArchPreRAExpandPseudo::expandPcalau12iInstPair(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI, unsigned FlagsHi,
    unsigned SecondOpcode, unsigned FlagsLo) {
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg =
      MF->getRegInfo().createVirtualRegister(&LoongArch::GPRRegClass);
  MachineOperand &Symbol = MI.getOperand(1);

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PCALAU12I), ScratchReg)
      .addDisp(Symbol, 0, FlagsHi);

  MachineInstr *SecondMI =
      BuildMI(MBB, MBBI, DL, TII->get(SecondOpcode), DestReg)
          .addReg(ScratchReg)
          .addDisp(Symbol, 0, FlagsLo);

  if (MI.hasOneMemOperand())
    SecondMI->addMemOperand(*MF, *MI.memoperands_begin());

  MI.eraseFromParent();
  return true;
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressPcrel(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $rd, %pc_hi20(sym)
  // addi.w/d $rd, $rd, %pc_lo12(sym)
  MachineFunction *MF = MBB.getParent();
  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? LoongArch::ADDI_D : LoongArch::ADDI_W;
  return expandPcalau12iInstPair(MBB, MBBI, NextMBBI, LoongArchII::MO_PCREL_HI,
                                 SecondOpcode, LoongArchII::MO_PCREL_LO);
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressGot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $rd, %got_pc_hi20(sym)
  // ld.w/d $rd, $rd, %got_pc_lo12(sym)
  MachineFunction *MF = MBB.getParent();
  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? LoongArch::LD_D : LoongArch::LD_W;
  return expandPcalau12iInstPair(MBB, MBBI, NextMBBI, LoongArchII::MO_GOT_PC_HI,
                                 SecondOpcode, LoongArchII::MO_GOT_PC_LO);
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressTLSLE(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // lu12i.w $rd, %le_hi20(sym)
  // ori $rd, $rd, %le_lo12(sym)
  //
  // And additionally if generating code using the large code model:
  //
  // lu32i.d $rd, %le64_lo20(sym)
  // lu52i.d $rd, $rd, %le64_hi12(sym)
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  bool Large = MF->getTarget().getCodeModel() == CodeModel::Large;
  Register DestReg = MI.getOperand(0).getReg();
  Register Parts01 =
      Large ? MF->getRegInfo().createVirtualRegister(&LoongArch::GPRRegClass)
            : DestReg;
  Register Part1 =
      MF->getRegInfo().createVirtualRegister(&LoongArch::GPRRegClass);
  MachineOperand &Symbol = MI.getOperand(1);

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU12I_W), Part1)
      .addDisp(Symbol, 0, LoongArchII::MO_LE_HI);

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::ORI), Parts01)
      .addReg(Part1, RegState::Kill)
      .addDisp(Symbol, 0, LoongArchII::MO_LE_LO);

  if (Large) {
    Register Parts012 =
        MF->getRegInfo().createVirtualRegister(&LoongArch::GPRRegClass);

    BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU32I_D), Parts012)
        // "rj" is needed due to InstrInfo pattern requirement.
        .addReg(Parts01, RegState::Kill)
        .addDisp(Symbol, 0, LoongArchII::MO_LE64_LO);
    BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU52I_D), DestReg)
        .addReg(Parts012, RegState::Kill)
        .addDisp(Symbol, 0, LoongArchII::MO_LE64_HI);
  }

  MI.eraseFromParent();
  return true;
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressTLSIE(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $rd, %ie_pc_hi20(sym)
  // ld.w/d $rd, $rd, %ie_pc_lo12(sym)
  MachineFunction *MF = MBB.getParent();
  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? LoongArch::LD_D : LoongArch::LD_W;
  return expandPcalau12iInstPair(MBB, MBBI, NextMBBI, LoongArchII::MO_IE_PC_HI,
                                 SecondOpcode, LoongArchII::MO_IE_PC_LO);
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressTLSLD(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $rd, %ld_pc_hi20(sym)
  // addi.w/d $rd, $rd, %got_pc_lo12(sym)
  MachineFunction *MF = MBB.getParent();
  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? LoongArch::ADDI_D : LoongArch::ADDI_W;
  return expandPcalau12iInstPair(MBB, MBBI, NextMBBI, LoongArchII::MO_LD_PC_HI,
                                 SecondOpcode, LoongArchII::MO_GOT_PC_LO);
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressTLSGD(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $rd, %gd_pc_hi20(sym)
  // addi.w/d $rd, $rd, %got_pc_lo12(sym)
  MachineFunction *MF = MBB.getParent();
  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned SecondOpcode = STI.is64Bit() ? LoongArch::ADDI_D : LoongArch::ADDI_W;
  return expandPcalau12iInstPair(MBB, MBBI, NextMBBI, LoongArchII::MO_GD_PC_HI,
                                 SecondOpcode, LoongArchII::MO_GOT_PC_LO);
}

bool LoongArchPreRAExpandPseudo::expandLoadAddressTLSDesc(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  // pcalau12i $a0, %desc_pc_hi20(sym)
  // addi.w/d  $a0, $a0, %desc_pc_lo12(sym)
  // ld.w/d    $ra, $a0, %desc_ld(sym)
  // jirl      $ra, $ra, %desc_ld(sym)
  // add.d     $dst, $a0, $tp
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  const auto &STI = MF->getSubtarget<LoongArchSubtarget>();
  unsigned ADD = STI.is64Bit() ? LoongArch::ADD_D : LoongArch::ADD_W;
  unsigned ADDI = STI.is64Bit() ? LoongArch::ADDI_D : LoongArch::ADDI_W;
  unsigned LD = STI.is64Bit() ? LoongArch::LD_D : LoongArch::LD_W;

  Register DestReg = MI.getOperand(0).getReg();
  Register ScratchReg =
      MF->getRegInfo().createVirtualRegister(&LoongArch::GPRRegClass);
  MachineOperand &Symbol = MI.getOperand(1);

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PCALAU12I), ScratchReg)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_PC_HI);

  BuildMI(MBB, MBBI, DL, TII->get(ADDI), LoongArch::R4)
      .addReg(ScratchReg)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_PC_LO);

  BuildMI(MBB, MBBI, DL, TII->get(LD), LoongArch::R1)
      .addReg(LoongArch::R4)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_LD);

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PseudoDESC_CALL), LoongArch::R1)
      .addReg(LoongArch::R1)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_CALL);

  BuildMI(MBB, MBBI, DL, TII->get(ADD), DestReg)
      .addReg(LoongArch::R4)
      .addReg(LoongArch::R2);

  MI.eraseFromParent();
  return true;
}

class LoongArchExpandPseudo : public MachineFunctionPass {
public:
  const LoongArchInstrInfo *TII;
  static char ID;

  LoongArchExpandPseudo() : MachineFunctionPass(ID) {
    initializeLoongArchExpandPseudoPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override {
    return LOONGARCH_EXPAND_PSEUDO_NAME;
  }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandCopyCFR(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                     MachineBasicBlock::iterator &NextMBBI);
  bool expandLargeAddressLoad(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI,
                              unsigned LastOpcode, unsigned IdentifyingMO);
  bool expandLargeAddressLoad(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI,
                              MachineBasicBlock::iterator &NextMBBI,
                              unsigned LastOpcode, unsigned IdentifyingMO,
                              const MachineOperand &Symbol, Register DestReg,
                              bool EraseFromParent);
  bool expandLoadAddressPcrelLarge(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressGotLarge(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSIELarge(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSLDLarge(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSGDLarge(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI);
  bool expandLoadAddressTLSDescPcLarge(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       MachineBasicBlock::iterator &NextMBBI);
  bool expandFunctionCALL(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          MachineBasicBlock::iterator &NextMBBI,
                          bool IsTailCall);
};

char LoongArchExpandPseudo::ID = 0;

bool LoongArchExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII =
      static_cast<const LoongArchInstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= expandMBB(MBB);

  return Modified;
}

bool LoongArchExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool LoongArchExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     MachineBasicBlock::iterator &NextMBBI) {
  switch (MBBI->getOpcode()) {
  case LoongArch::PseudoCopyCFR:
    return expandCopyCFR(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_PCREL_LARGE:
    return expandLoadAddressPcrelLarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_GOT_LARGE:
    return expandLoadAddressGotLarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_IE_LARGE:
    return expandLoadAddressTLSIELarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_LD_LARGE:
    return expandLoadAddressTLSLDLarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_GD_LARGE:
    return expandLoadAddressTLSGDLarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoLA_TLS_DESC_PC_LARGE:
    return expandLoadAddressTLSDescPcLarge(MBB, MBBI, NextMBBI);
  case LoongArch::PseudoCALL:
  case LoongArch::PseudoCALL_MEDIUM:
  case LoongArch::PseudoCALL_LARGE:
    return expandFunctionCALL(MBB, MBBI, NextMBBI, /*IsTailCall=*/false);
  case LoongArch::PseudoTAIL:
  case LoongArch::PseudoTAIL_MEDIUM:
  case LoongArch::PseudoTAIL_LARGE:
    return expandFunctionCALL(MBB, MBBI, NextMBBI, /*IsTailCall=*/true);
  }

  return false;
}

bool LoongArchExpandPseudo::expandCopyCFR(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();

  // Expand:
  // MBB:
  //    fcmp.caf.s  $dst, $fa0, $fa0 # set $dst 0(false)
  //    bceqz $src, SinkBB
  // FalseBB:
  //    fcmp.cueq.s $dst, $fa0, $fa0 # set $dst 1(true)
  // SinkBB:
  //    fallthrough

  const BasicBlock *LLVM_BB = MBB.getBasicBlock();
  auto *FalseBB = MF->CreateMachineBasicBlock(LLVM_BB);
  auto *SinkBB = MF->CreateMachineBasicBlock(LLVM_BB);

  MF->insert(++MBB.getIterator(), FalseBB);
  MF->insert(++FalseBB->getIterator(), SinkBB);

  Register DestReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();
  // DestReg = 0
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::SET_CFR_FALSE), DestReg);
  // Insert branch instruction.
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::BCEQZ))
      .addReg(SrcReg)
      .addMBB(SinkBB);
  // DestReg = 1
  BuildMI(FalseBB, DL, TII->get(LoongArch::SET_CFR_TRUE), DestReg);

  FalseBB->addSuccessor(SinkBB);

  SinkBB->splice(SinkBB->end(), &MBB, MI, MBB.end());
  SinkBB->transferSuccessors(&MBB);

  MBB.addSuccessor(FalseBB);
  MBB.addSuccessor(SinkBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Make sure live-ins are correctly attached to this new basic block.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *FalseBB);
  computeAndAddLiveIns(LiveRegs, *SinkBB);

  return true;
}

bool LoongArchExpandPseudo::expandLargeAddressLoad(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI, unsigned LastOpcode,
    unsigned IdentifyingMO) {
  MachineInstr &MI = *MBBI;
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LastOpcode, IdentifyingMO,
                                MI.getOperand(2), MI.getOperand(0).getReg(),
                                true);
}

bool LoongArchExpandPseudo::expandLargeAddressLoad(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI, unsigned LastOpcode,
    unsigned IdentifyingMO, const MachineOperand &Symbol, Register DestReg,
    bool EraseFromParent) {
  // Code Sequence:
  //
  // Part1: pcalau12i  $dst, %MO1(sym)
  // Part0: addi.d     $t8, $zero, %MO0(sym)
  // Part2: lu32i.d    $t8, %MO2(sym)
  // Part3: lu52i.d    $t8, $t8, %MO3(sym)
  // Fin:   LastOpcode $dst, $t8, $dst

  unsigned MO0, MO1, MO2, MO3;
  switch (IdentifyingMO) {
  default:
    llvm_unreachable("unsupported identifying MO");
  case LoongArchII::MO_PCREL_LO:
    MO0 = IdentifyingMO;
    MO1 = LoongArchII::MO_PCREL_HI;
    MO2 = LoongArchII::MO_PCREL64_LO;
    MO3 = LoongArchII::MO_PCREL64_HI;
    break;
  case LoongArchII::MO_GOT_PC_HI:
  case LoongArchII::MO_LD_PC_HI:
  case LoongArchII::MO_GD_PC_HI:
    // These cases relocate just like the GOT case, except for Part1.
    MO0 = LoongArchII::MO_GOT_PC_LO;
    MO1 = IdentifyingMO;
    MO2 = LoongArchII::MO_GOT_PC64_LO;
    MO3 = LoongArchII::MO_GOT_PC64_HI;
    break;
  case LoongArchII::MO_IE_PC_LO:
    MO0 = IdentifyingMO;
    MO1 = LoongArchII::MO_IE_PC_HI;
    MO2 = LoongArchII::MO_IE_PC64_LO;
    MO3 = LoongArchII::MO_IE_PC64_HI;
    break;
  }

  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  Register ScratchReg = LoongArch::R20; // $t8

  assert(MBB.getParent()->getSubtarget<LoongArchSubtarget>().is64Bit() &&
         "Large code model requires LA64");

  auto Part1 = BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PCALAU12I), DestReg);
  auto Part0 = BuildMI(MBB, MBBI, DL, TII->get(LoongArch::ADDI_D), ScratchReg)
                   .addReg(LoongArch::R0);
  auto Part2 = BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU32I_D), ScratchReg)
                   // "rj" is needed due to InstrInfo pattern requirement.
                   .addReg(ScratchReg);
  auto Part3 = BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU52I_D), ScratchReg)
                   .addReg(ScratchReg);
  BuildMI(MBB, MBBI, DL, TII->get(LastOpcode), DestReg)
      .addReg(ScratchReg)
      .addReg(DestReg);

  if (Symbol.getType() == MachineOperand::MO_ExternalSymbol) {
    const char *SymName = Symbol.getSymbolName();
    Part0.addExternalSymbol(SymName, MO0);
    Part1.addExternalSymbol(SymName, MO1);
    Part2.addExternalSymbol(SymName, MO2);
    Part3.addExternalSymbol(SymName, MO3);
  } else {
    Part0.addDisp(Symbol, 0, MO0);
    Part1.addDisp(Symbol, 0, MO1);
    Part2.addDisp(Symbol, 0, MO2);
    Part3.addDisp(Symbol, 0, MO3);
  }

  if (EraseFromParent)
    MI.eraseFromParent();

  return true;
}

bool LoongArchExpandPseudo::expandLoadAddressPcrelLarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Emit the 5-insn large address load sequence with the `%pc` family of
  // relocs.
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LoongArch::ADD_D,
                                LoongArchII::MO_PCREL_LO);
}

bool LoongArchExpandPseudo::expandLoadAddressGotLarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Emit the 5-insn large address load sequence with the `%got_pc` family
  // of relocs, loading the result from GOT with `ldx.d` in the end.
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LoongArch::LDX_D,
                                LoongArchII::MO_GOT_PC_HI);
}

bool LoongArchExpandPseudo::expandLoadAddressTLSIELarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Emit the 5-insn large address load sequence with the `%ie_pc` family
  // of relocs, loading the result with `ldx.d` in the end.
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LoongArch::LDX_D,
                                LoongArchII::MO_IE_PC_LO);
}

bool LoongArchExpandPseudo::expandLoadAddressTLSLDLarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Emit the 5-insn large address load sequence with the `%got_pc` family
  // of relocs, with the `pcalau12i` insn relocated with `%ld_pc_hi20`.
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LoongArch::ADD_D,
                                LoongArchII::MO_LD_PC_HI);
}

bool LoongArchExpandPseudo::expandLoadAddressTLSGDLarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Emit the 5-insn large address load sequence with the `%got_pc` family
  // of relocs, with the `pcalau12i` insn relocated with `%gd_pc_hi20`.
  return expandLargeAddressLoad(MBB, MBBI, NextMBBI, LoongArch::ADD_D,
                                LoongArchII::MO_GD_PC_HI);
}

bool LoongArchExpandPseudo::expandLoadAddressTLSDescPcLarge(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  // Code Sequence:
  //
  // pcalau12i  $a0, %desc_pc_hi20(sym)
  // addi.d     $t8, $zero, %desc_pc_lo12(sym)
  // lu32i.d    $t8, %desc64_pc_lo20(sym)
  // lu52i.d    $t8, $t8, %desc64_pc_hi12(sym)
  // add.d      $a0, $a0, $t8
  // ld.d       $ra, $a0, %desc_ld(sym)
  // jirl       $ra, $ra, %desc_call(sym)
  // add.d      $dst, $a0, $tp

  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  Register DestReg = MI.getOperand(0).getReg();
  MachineOperand &Symbol = MI.getOperand(2);
  Register ScratchReg = LoongArch::R20; // $t8

  assert(MBB.getParent()->getSubtarget<LoongArchSubtarget>().is64Bit() &&
         "Large code model requires LA64");

  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PCALAU12I), LoongArch::R4)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_PC_HI);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::ADDI_D), ScratchReg)
      .addReg(LoongArch::R0)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_PC_LO);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU32I_D), ScratchReg)
      .addReg(ScratchReg)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC64_PC_LO);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LU52I_D), ScratchReg)
      .addReg(ScratchReg)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC64_PC_HI);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::ADD_D), LoongArch::R4)
      .addReg(ScratchReg)
      .addReg(LoongArch::R4);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::LD_D), LoongArch::R1)
      .addReg(LoongArch::R4)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_LD);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PseudoDESC_CALL), LoongArch::R1)
      .addReg(LoongArch::R1)
      .addDisp(Symbol, 0, LoongArchII::MO_DESC_CALL);
  BuildMI(MBB, MBBI, DL, TII->get(LoongArch::ADD_D), DestReg)
      .addReg(LoongArch::R4)
      .addReg(LoongArch::R2);

  MI.eraseFromParent();

  return true;
}

bool LoongArchExpandPseudo::expandFunctionCALL(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI, bool IsTailCall) {
  MachineFunction *MF = MBB.getParent();
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  const MachineOperand &Func = MI.getOperand(0);
  MachineInstrBuilder CALL;
  unsigned Opcode;

  switch (MF->getTarget().getCodeModel()) {
  default:
    report_fatal_error("Unsupported code model");
    break;
  case CodeModel::Small: {
    // CALL:
    // bl func
    // TAIL:
    // b func
    Opcode = IsTailCall ? LoongArch::PseudoB_TAIL : LoongArch::BL;
    CALL = BuildMI(MBB, MBBI, DL, TII->get(Opcode)).add(Func);
    break;
  }
  case CodeModel::Medium: {
    // CALL:
    // pcaddu18i  $ra, %call36(func)
    // jirl       $ra, $ra, 0
    // TAIL:
    // pcaddu18i  $t8, %call36(func)
    // jr         $t8
    Opcode =
        IsTailCall ? LoongArch::PseudoJIRL_TAIL : LoongArch::PseudoJIRL_CALL;
    Register ScratchReg = IsTailCall ? LoongArch::R20 : LoongArch::R1;
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, DL, TII->get(LoongArch::PCADDU18I), ScratchReg);

    CALL =
        BuildMI(MBB, MBBI, DL, TII->get(Opcode)).addReg(ScratchReg).addImm(0);

    if (Func.isSymbol())
      MIB.addExternalSymbol(Func.getSymbolName(), LoongArchII::MO_CALL36);
    else
      MIB.addDisp(Func, 0, LoongArchII::MO_CALL36);
    break;
  }
  case CodeModel::Large: {
    // Emit the 5-insn large address load sequence, either directly or
    // indirectly in case of going through the GOT, then JIRL_TAIL or
    // JIRL_CALL to $addr.
    Opcode =
        IsTailCall ? LoongArch::PseudoJIRL_TAIL : LoongArch::PseudoJIRL_CALL;
    Register AddrReg = IsTailCall ? LoongArch::R19 : LoongArch::R1;

    bool UseGOT = Func.getTargetFlags() == LoongArchII::MO_CALL_PLT;
    unsigned MO = UseGOT ? LoongArchII::MO_GOT_PC_HI : LoongArchII::MO_PCREL_LO;
    unsigned LAOpcode = UseGOT ? LoongArch::LDX_D : LoongArch::ADD_D;
    expandLargeAddressLoad(MBB, MBBI, NextMBBI, LAOpcode, MO, Func, AddrReg,
                           false);
    CALL = BuildMI(MBB, MBBI, DL, TII->get(Opcode)).addReg(AddrReg).addImm(0);
    break;
  }
  }

  // Transfer implicit operands.
  CALL.copyImplicitOps(MI);

  // Transfer MI flags.
  CALL.setMIFlags(MI.getFlags());

  MI.eraseFromParent();
  return true;
}

} // end namespace

INITIALIZE_PASS(LoongArchPreRAExpandPseudo, "loongarch-prera-expand-pseudo",
                LOONGARCH_PRERA_EXPAND_PSEUDO_NAME, false, false)

INITIALIZE_PASS(LoongArchExpandPseudo, "loongarch-expand-pseudo",
                LOONGARCH_EXPAND_PSEUDO_NAME, false, false)

namespace llvm {

FunctionPass *createLoongArchPreRAExpandPseudoPass() {
  return new LoongArchPreRAExpandPseudo();
}
FunctionPass *createLoongArchExpandPseudoPass() {
  return new LoongArchExpandPseudo();
}

} // end namespace llvm
