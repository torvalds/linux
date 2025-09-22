//===- AArch64ExpandPseudoInsts.cpp - Expand pseudo instructions ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that expands pseudo instructions into target
// instructions to allow proper scheduling and other late optimizations.  This
// pass should be run after register allocation but before the post-regalloc
// scheduling pass.
//
//===----------------------------------------------------------------------===//

#include "AArch64ExpandImm.h"
#include "AArch64InstrInfo.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define AARCH64_EXPAND_PSEUDO_NAME "AArch64 pseudo instruction expansion pass"

namespace {

class AArch64ExpandPseudo : public MachineFunctionPass {
public:
  const AArch64InstrInfo *TII;

  static char ID;

  AArch64ExpandPseudo() : MachineFunctionPass(ID) {
    initializeAArch64ExpandPseudoPass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return AARCH64_EXPAND_PSEUDO_NAME; }

private:
  bool expandMBB(MachineBasicBlock &MBB);
  bool expandMI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                MachineBasicBlock::iterator &NextMBBI);
  bool expandMultiVecPseudo(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI,
                            TargetRegisterClass ContiguousClass,
                            TargetRegisterClass StridedClass,
                            unsigned ContiguousOpc, unsigned StridedOpc);
  bool expandMOVImm(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                    unsigned BitSize);

  bool expand_DestructiveOp(MachineInstr &MI, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI);
  bool expandCMP_SWAP(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                      unsigned LdarOp, unsigned StlrOp, unsigned CmpOp,
                      unsigned ExtendImm, unsigned ZeroReg,
                      MachineBasicBlock::iterator &NextMBBI);
  bool expandCMP_SWAP_128(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI,
                          MachineBasicBlock::iterator &NextMBBI);
  bool expandSetTagLoop(MachineBasicBlock &MBB,
                        MachineBasicBlock::iterator MBBI,
                        MachineBasicBlock::iterator &NextMBBI);
  bool expandSVESpillFill(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, unsigned Opc,
                          unsigned N);
  bool expandCALL_RVMARKER(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI);
  bool expandCALL_BTI(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI);
  bool expandStoreSwiftAsyncContext(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MBBI);
  MachineBasicBlock *expandRestoreZA(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI);
  MachineBasicBlock *expandCondSMToggle(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI);
};

} // end anonymous namespace

char AArch64ExpandPseudo::ID = 0;

INITIALIZE_PASS(AArch64ExpandPseudo, "aarch64-expand-pseudo",
                AARCH64_EXPAND_PSEUDO_NAME, false, false)

/// Transfer implicit operands on the pseudo instruction to the
/// instructions created from the expansion.
static void transferImpOps(MachineInstr &OldMI, MachineInstrBuilder &UseMI,
                           MachineInstrBuilder &DefMI) {
  const MCInstrDesc &Desc = OldMI.getDesc();
  for (const MachineOperand &MO :
       llvm::drop_begin(OldMI.operands(), Desc.getNumOperands())) {
    assert(MO.isReg() && MO.getReg());
    if (MO.isUse())
      UseMI.add(MO);
    else
      DefMI.add(MO);
  }
}

/// Expand a MOVi32imm or MOVi64imm pseudo instruction to one or more
/// real move-immediate instructions to synthesize the immediate.
bool AArch64ExpandPseudo::expandMOVImm(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       unsigned BitSize) {
  MachineInstr &MI = *MBBI;
  Register DstReg = MI.getOperand(0).getReg();
  uint64_t RenamableState =
      MI.getOperand(0).isRenamable() ? RegState::Renamable : 0;
  uint64_t Imm = MI.getOperand(1).getImm();

  if (DstReg == AArch64::XZR || DstReg == AArch64::WZR) {
    // Useless def, and we don't want to risk creating an invalid ORR (which
    // would really write to sp).
    MI.eraseFromParent();
    return true;
  }

  SmallVector<AArch64_IMM::ImmInsnModel, 4> Insn;
  AArch64_IMM::expandMOVImm(Imm, BitSize, Insn);
  assert(Insn.size() != 0);

  SmallVector<MachineInstrBuilder, 4> MIBS;
  for (auto I = Insn.begin(), E = Insn.end(); I != E; ++I) {
    bool LastItem = std::next(I) == E;
    switch (I->Opcode)
    {
    default: llvm_unreachable("unhandled!"); break;

    case AArch64::ORRWri:
    case AArch64::ORRXri:
      if (I->Op1 == 0) {
        MIBS.push_back(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
                           .add(MI.getOperand(0))
                           .addReg(BitSize == 32 ? AArch64::WZR : AArch64::XZR)
                           .addImm(I->Op2));
      } else {
        Register DstReg = MI.getOperand(0).getReg();
        bool DstIsDead = MI.getOperand(0).isDead();
        MIBS.push_back(
            BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
                .addReg(DstReg, RegState::Define |
                                    getDeadRegState(DstIsDead && LastItem) |
                                    RenamableState)
                .addReg(DstReg)
                .addImm(I->Op2));
      }
      break;
    case AArch64::ORRWrs:
    case AArch64::ORRXrs: {
      Register DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      MIBS.push_back(
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
              .addReg(DstReg, RegState::Define |
                                  getDeadRegState(DstIsDead && LastItem) |
                                  RenamableState)
              .addReg(DstReg)
              .addReg(DstReg)
              .addImm(I->Op2));
    } break;
    case AArch64::ANDXri:
    case AArch64::EORXri:
      if (I->Op1 == 0) {
        MIBS.push_back(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
                           .add(MI.getOperand(0))
                           .addReg(BitSize == 32 ? AArch64::WZR : AArch64::XZR)
                           .addImm(I->Op2));
      } else {
        Register DstReg = MI.getOperand(0).getReg();
        bool DstIsDead = MI.getOperand(0).isDead();
        MIBS.push_back(
            BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
                .addReg(DstReg, RegState::Define |
                                    getDeadRegState(DstIsDead && LastItem) |
                                    RenamableState)
                .addReg(DstReg)
                .addImm(I->Op2));
      }
      break;
    case AArch64::MOVNWi:
    case AArch64::MOVNXi:
    case AArch64::MOVZWi:
    case AArch64::MOVZXi: {
      bool DstIsDead = MI.getOperand(0).isDead();
      MIBS.push_back(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
        .addReg(DstReg, RegState::Define |
                getDeadRegState(DstIsDead && LastItem) |
                RenamableState)
        .addImm(I->Op1)
        .addImm(I->Op2));
      } break;
    case AArch64::MOVKWi:
    case AArch64::MOVKXi: {
      Register DstReg = MI.getOperand(0).getReg();
      bool DstIsDead = MI.getOperand(0).isDead();
      MIBS.push_back(BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(I->Opcode))
        .addReg(DstReg,
                RegState::Define |
                getDeadRegState(DstIsDead && LastItem) |
                RenamableState)
        .addReg(DstReg)
        .addImm(I->Op1)
        .addImm(I->Op2));
      } break;
    }
  }
  transferImpOps(MI, MIBS.front(), MIBS.back());
  MI.eraseFromParent();
  return true;
}

bool AArch64ExpandPseudo::expandCMP_SWAP(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI, unsigned LdarOp,
    unsigned StlrOp, unsigned CmpOp, unsigned ExtendImm, unsigned ZeroReg,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  MIMetadata MIMD(MI);
  const MachineOperand &Dest = MI.getOperand(0);
  Register StatusReg = MI.getOperand(1).getReg();
  bool StatusDead = MI.getOperand(1).isDead();
  // Duplicating undef operands into 2 instructions does not guarantee the same
  // value on both; However undef should be replaced by xzr anyway.
  assert(!MI.getOperand(2).isUndef() && "cannot handle undef");
  Register AddrReg = MI.getOperand(2).getReg();
  Register DesiredReg = MI.getOperand(3).getReg();
  Register NewReg = MI.getOperand(4).getReg();

  MachineFunction *MF = MBB.getParent();
  auto LoadCmpBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto StoreBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), LoadCmpBB);
  MF->insert(++LoadCmpBB->getIterator(), StoreBB);
  MF->insert(++StoreBB->getIterator(), DoneBB);

  // .Lloadcmp:
  //     mov wStatus, 0
  //     ldaxr xDest, [xAddr]
  //     cmp xDest, xDesired
  //     b.ne .Ldone
  if (!StatusDead)
    BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::MOVZWi), StatusReg)
      .addImm(0).addImm(0);
  BuildMI(LoadCmpBB, MIMD, TII->get(LdarOp), Dest.getReg())
      .addReg(AddrReg);
  BuildMI(LoadCmpBB, MIMD, TII->get(CmpOp), ZeroReg)
      .addReg(Dest.getReg(), getKillRegState(Dest.isDead()))
      .addReg(DesiredReg)
      .addImm(ExtendImm);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::Bcc))
      .addImm(AArch64CC::NE)
      .addMBB(DoneBB)
      .addReg(AArch64::NZCV, RegState::Implicit | RegState::Kill);
  LoadCmpBB->addSuccessor(DoneBB);
  LoadCmpBB->addSuccessor(StoreBB);

  // .Lstore:
  //     stlxr wStatus, xNew, [xAddr]
  //     cbnz wStatus, .Lloadcmp
  BuildMI(StoreBB, MIMD, TII->get(StlrOp), StatusReg)
      .addReg(NewReg)
      .addReg(AddrReg);
  BuildMI(StoreBB, MIMD, TII->get(AArch64::CBNZW))
      .addReg(StatusReg, getKillRegState(StatusDead))
      .addMBB(LoadCmpBB);
  StoreBB->addSuccessor(LoadCmpBB);
  StoreBB->addSuccessor(DoneBB);

  DoneBB->splice(DoneBB->end(), &MBB, MI, MBB.end());
  DoneBB->transferSuccessors(&MBB);

  MBB.addSuccessor(LoadCmpBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Recompute livein lists.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *DoneBB);
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);
  // Do an extra pass around the loop to get loop carried registers right.
  StoreBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  LoadCmpBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);

  return true;
}

bool AArch64ExpandPseudo::expandCMP_SWAP_128(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  MIMetadata MIMD(MI);
  MachineOperand &DestLo = MI.getOperand(0);
  MachineOperand &DestHi = MI.getOperand(1);
  Register StatusReg = MI.getOperand(2).getReg();
  bool StatusDead = MI.getOperand(2).isDead();
  // Duplicating undef operands into 2 instructions does not guarantee the same
  // value on both; However undef should be replaced by xzr anyway.
  assert(!MI.getOperand(3).isUndef() && "cannot handle undef");
  Register AddrReg = MI.getOperand(3).getReg();
  Register DesiredLoReg = MI.getOperand(4).getReg();
  Register DesiredHiReg = MI.getOperand(5).getReg();
  Register NewLoReg = MI.getOperand(6).getReg();
  Register NewHiReg = MI.getOperand(7).getReg();

  unsigned LdxpOp, StxpOp;

  switch (MI.getOpcode()) {
  case AArch64::CMP_SWAP_128_MONOTONIC:
    LdxpOp = AArch64::LDXPX;
    StxpOp = AArch64::STXPX;
    break;
  case AArch64::CMP_SWAP_128_RELEASE:
    LdxpOp = AArch64::LDXPX;
    StxpOp = AArch64::STLXPX;
    break;
  case AArch64::CMP_SWAP_128_ACQUIRE:
    LdxpOp = AArch64::LDAXPX;
    StxpOp = AArch64::STXPX;
    break;
  case AArch64::CMP_SWAP_128:
    LdxpOp = AArch64::LDAXPX;
    StxpOp = AArch64::STLXPX;
    break;
  default:
    llvm_unreachable("Unexpected opcode");
  }

  MachineFunction *MF = MBB.getParent();
  auto LoadCmpBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto StoreBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto FailBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), LoadCmpBB);
  MF->insert(++LoadCmpBB->getIterator(), StoreBB);
  MF->insert(++StoreBB->getIterator(), FailBB);
  MF->insert(++FailBB->getIterator(), DoneBB);

  // .Lloadcmp:
  //     ldaxp xDestLo, xDestHi, [xAddr]
  //     cmp xDestLo, xDesiredLo
  //     sbcs xDestHi, xDesiredHi
  //     b.ne .Ldone
  BuildMI(LoadCmpBB, MIMD, TII->get(LdxpOp))
      .addReg(DestLo.getReg(), RegState::Define)
      .addReg(DestHi.getReg(), RegState::Define)
      .addReg(AddrReg);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::SUBSXrs), AArch64::XZR)
      .addReg(DestLo.getReg(), getKillRegState(DestLo.isDead()))
      .addReg(DesiredLoReg)
      .addImm(0);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::CSINCWr), StatusReg)
    .addUse(AArch64::WZR)
    .addUse(AArch64::WZR)
    .addImm(AArch64CC::EQ);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::SUBSXrs), AArch64::XZR)
      .addReg(DestHi.getReg(), getKillRegState(DestHi.isDead()))
      .addReg(DesiredHiReg)
      .addImm(0);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::CSINCWr), StatusReg)
      .addUse(StatusReg, RegState::Kill)
      .addUse(StatusReg, RegState::Kill)
      .addImm(AArch64CC::EQ);
  BuildMI(LoadCmpBB, MIMD, TII->get(AArch64::CBNZW))
      .addUse(StatusReg, getKillRegState(StatusDead))
      .addMBB(FailBB);
  LoadCmpBB->addSuccessor(FailBB);
  LoadCmpBB->addSuccessor(StoreBB);

  // .Lstore:
  //     stlxp wStatus, xNewLo, xNewHi, [xAddr]
  //     cbnz wStatus, .Lloadcmp
  BuildMI(StoreBB, MIMD, TII->get(StxpOp), StatusReg)
      .addReg(NewLoReg)
      .addReg(NewHiReg)
      .addReg(AddrReg);
  BuildMI(StoreBB, MIMD, TII->get(AArch64::CBNZW))
      .addReg(StatusReg, getKillRegState(StatusDead))
      .addMBB(LoadCmpBB);
  BuildMI(StoreBB, MIMD, TII->get(AArch64::B)).addMBB(DoneBB);
  StoreBB->addSuccessor(LoadCmpBB);
  StoreBB->addSuccessor(DoneBB);

  // .Lfail:
  //     stlxp wStatus, xDestLo, xDestHi, [xAddr]
  //     cbnz wStatus, .Lloadcmp
  BuildMI(FailBB, MIMD, TII->get(StxpOp), StatusReg)
      .addReg(DestLo.getReg())
      .addReg(DestHi.getReg())
      .addReg(AddrReg);
  BuildMI(FailBB, MIMD, TII->get(AArch64::CBNZW))
      .addReg(StatusReg, getKillRegState(StatusDead))
      .addMBB(LoadCmpBB);
  FailBB->addSuccessor(LoadCmpBB);
  FailBB->addSuccessor(DoneBB);

  DoneBB->splice(DoneBB->end(), &MBB, MI, MBB.end());
  DoneBB->transferSuccessors(&MBB);

  MBB.addSuccessor(LoadCmpBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();

  // Recompute liveness bottom up.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *DoneBB);
  computeAndAddLiveIns(LiveRegs, *FailBB);
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);

  // Do an extra pass in the loop to get the loop carried dependencies right.
  FailBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *FailBB);
  StoreBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *StoreBB);
  LoadCmpBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *LoadCmpBB);

  return true;
}

/// \brief Expand Pseudos to Instructions with destructive operands.
///
/// This mechanism uses MOVPRFX instructions for zeroing the false lanes
/// or for fixing relaxed register allocation conditions to comply with
/// the instructions register constraints. The latter case may be cheaper
/// than setting the register constraints in the register allocator,
/// since that will insert regular MOV instructions rather than MOVPRFX.
///
/// Example (after register allocation):
///
///   FSUB_ZPZZ_ZERO_B Z0, Pg, Z1, Z0
///
/// * The Pseudo FSUB_ZPZZ_ZERO_B maps to FSUB_ZPmZ_B.
/// * We cannot map directly to FSUB_ZPmZ_B because the register
///   constraints of the instruction are not met.
/// * Also the _ZERO specifies the false lanes need to be zeroed.
///
/// We first try to see if the destructive operand == result operand,
/// if not, we try to swap the operands, e.g.
///
///   FSUB_ZPmZ_B  Z0, Pg/m, Z0, Z1
///
/// But because FSUB_ZPmZ is not commutative, this is semantically
/// different, so we need a reverse instruction:
///
///   FSUBR_ZPmZ_B  Z0, Pg/m, Z0, Z1
///
/// Then we implement the zeroing of the false lanes of Z0 by adding
/// a zeroing MOVPRFX instruction:
///
///   MOVPRFX_ZPzZ_B Z0, Pg/z, Z0
///   FSUBR_ZPmZ_B   Z0, Pg/m, Z0, Z1
///
/// Note that this can only be done for _ZERO or _UNDEF variants where
/// we can guarantee the false lanes to be zeroed (by implementing this)
/// or that they are undef (don't care / not used), otherwise the
/// swapping of operands is illegal because the operation is not
/// (or cannot be emulated to be) fully commutative.
bool AArch64ExpandPseudo::expand_DestructiveOp(
                            MachineInstr &MI,
                            MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI) {
  unsigned Opcode = AArch64::getSVEPseudoMap(MI.getOpcode());
  uint64_t DType = TII->get(Opcode).TSFlags & AArch64::DestructiveInstTypeMask;
  uint64_t FalseLanes = MI.getDesc().TSFlags & AArch64::FalseLanesMask;
  bool FalseZero = FalseLanes == AArch64::FalseLanesZero;
  Register DstReg = MI.getOperand(0).getReg();
  bool DstIsDead = MI.getOperand(0).isDead();
  bool UseRev = false;
  unsigned PredIdx, DOPIdx, SrcIdx, Src2Idx;

  switch (DType) {
  case AArch64::DestructiveBinaryComm:
  case AArch64::DestructiveBinaryCommWithRev:
    if (DstReg == MI.getOperand(3).getReg()) {
      // FSUB Zd, Pg, Zs1, Zd  ==> FSUBR   Zd, Pg/m, Zd, Zs1
      std::tie(PredIdx, DOPIdx, SrcIdx) = std::make_tuple(1, 3, 2);
      UseRev = true;
      break;
    }
    [[fallthrough]];
  case AArch64::DestructiveBinary:
  case AArch64::DestructiveBinaryImm:
    std::tie(PredIdx, DOPIdx, SrcIdx) = std::make_tuple(1, 2, 3);
    break;
  case AArch64::DestructiveUnaryPassthru:
    std::tie(PredIdx, DOPIdx, SrcIdx) = std::make_tuple(2, 3, 3);
    break;
  case AArch64::DestructiveTernaryCommWithRev:
    std::tie(PredIdx, DOPIdx, SrcIdx, Src2Idx) = std::make_tuple(1, 2, 3, 4);
    if (DstReg == MI.getOperand(3).getReg()) {
      // FMLA Zd, Pg, Za, Zd, Zm ==> FMAD Zdn, Pg, Zm, Za
      std::tie(PredIdx, DOPIdx, SrcIdx, Src2Idx) = std::make_tuple(1, 3, 4, 2);
      UseRev = true;
    } else if (DstReg == MI.getOperand(4).getReg()) {
      // FMLA Zd, Pg, Za, Zm, Zd ==> FMAD Zdn, Pg, Zm, Za
      std::tie(PredIdx, DOPIdx, SrcIdx, Src2Idx) = std::make_tuple(1, 4, 3, 2);
      UseRev = true;
    }
    break;
  default:
    llvm_unreachable("Unsupported Destructive Operand type");
  }

  // MOVPRFX can only be used if the destination operand
  // is the destructive operand, not as any other operand,
  // so the Destructive Operand must be unique.
  bool DOPRegIsUnique = false;
  switch (DType) {
  case AArch64::DestructiveBinary:
    DOPRegIsUnique = DstReg != MI.getOperand(SrcIdx).getReg();
    break;
  case AArch64::DestructiveBinaryComm:
  case AArch64::DestructiveBinaryCommWithRev:
    DOPRegIsUnique =
      DstReg != MI.getOperand(DOPIdx).getReg() ||
      MI.getOperand(DOPIdx).getReg() != MI.getOperand(SrcIdx).getReg();
    break;
  case AArch64::DestructiveUnaryPassthru:
  case AArch64::DestructiveBinaryImm:
    DOPRegIsUnique = true;
    break;
  case AArch64::DestructiveTernaryCommWithRev:
    DOPRegIsUnique =
        DstReg != MI.getOperand(DOPIdx).getReg() ||
        (MI.getOperand(DOPIdx).getReg() != MI.getOperand(SrcIdx).getReg() &&
         MI.getOperand(DOPIdx).getReg() != MI.getOperand(Src2Idx).getReg());
    break;
  }

  // Resolve the reverse opcode
  if (UseRev) {
    int NewOpcode;
    // e.g. DIV -> DIVR
    if ((NewOpcode = AArch64::getSVERevInstr(Opcode)) != -1)
      Opcode = NewOpcode;
    // e.g. DIVR -> DIV
    else if ((NewOpcode = AArch64::getSVENonRevInstr(Opcode)) != -1)
      Opcode = NewOpcode;
  }

  // Get the right MOVPRFX
  uint64_t ElementSize = TII->getElementSizeForOpcode(Opcode);
  unsigned MovPrfx, LSLZero, MovPrfxZero;
  switch (ElementSize) {
  case AArch64::ElementSizeNone:
  case AArch64::ElementSizeB:
    MovPrfx = AArch64::MOVPRFX_ZZ;
    LSLZero = AArch64::LSL_ZPmI_B;
    MovPrfxZero = AArch64::MOVPRFX_ZPzZ_B;
    break;
  case AArch64::ElementSizeH:
    MovPrfx = AArch64::MOVPRFX_ZZ;
    LSLZero = AArch64::LSL_ZPmI_H;
    MovPrfxZero = AArch64::MOVPRFX_ZPzZ_H;
    break;
  case AArch64::ElementSizeS:
    MovPrfx = AArch64::MOVPRFX_ZZ;
    LSLZero = AArch64::LSL_ZPmI_S;
    MovPrfxZero = AArch64::MOVPRFX_ZPzZ_S;
    break;
  case AArch64::ElementSizeD:
    MovPrfx = AArch64::MOVPRFX_ZZ;
    LSLZero = AArch64::LSL_ZPmI_D;
    MovPrfxZero = AArch64::MOVPRFX_ZPzZ_D;
    break;
  default:
    llvm_unreachable("Unsupported ElementSize");
  }

  //
  // Create the destructive operation (if required)
  //
  MachineInstrBuilder PRFX, DOP;
  if (FalseZero) {
    // If we cannot prefix the requested instruction we'll instead emit a
    // prefixed_zeroing_mov for DestructiveBinary.
    assert((DOPRegIsUnique || DType == AArch64::DestructiveBinary ||
            DType == AArch64::DestructiveBinaryComm ||
            DType == AArch64::DestructiveBinaryCommWithRev) &&
           "The destructive operand should be unique");
    assert(ElementSize != AArch64::ElementSizeNone &&
           "This instruction is unpredicated");

    // Merge source operand into destination register
    PRFX = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(MovPrfxZero))
               .addReg(DstReg, RegState::Define)
               .addReg(MI.getOperand(PredIdx).getReg())
               .addReg(MI.getOperand(DOPIdx).getReg());

    // After the movprfx, the destructive operand is same as Dst
    DOPIdx = 0;

    // Create the additional LSL to zero the lanes when the DstReg is not
    // unique. Zeros the lanes in z0 that aren't active in p0 with sequence
    // movprfx z0.b, p0/z, z0.b; lsl z0.b, p0/m, z0.b, #0;
    if ((DType == AArch64::DestructiveBinary ||
         DType == AArch64::DestructiveBinaryComm ||
         DType == AArch64::DestructiveBinaryCommWithRev) &&
        !DOPRegIsUnique) {
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(LSLZero))
          .addReg(DstReg, RegState::Define)
          .add(MI.getOperand(PredIdx))
          .addReg(DstReg)
          .addImm(0);
    }
  } else if (DstReg != MI.getOperand(DOPIdx).getReg()) {
    assert(DOPRegIsUnique && "The destructive operand should be unique");
    PRFX = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(MovPrfx))
               .addReg(DstReg, RegState::Define)
               .addReg(MI.getOperand(DOPIdx).getReg());
    DOPIdx = 0;
  }

  //
  // Create the destructive operation
  //
  DOP = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opcode))
    .addReg(DstReg, RegState::Define | getDeadRegState(DstIsDead));

  switch (DType) {
  case AArch64::DestructiveUnaryPassthru:
    DOP.addReg(MI.getOperand(DOPIdx).getReg(), RegState::Kill)
        .add(MI.getOperand(PredIdx))
        .add(MI.getOperand(SrcIdx));
    break;
  case AArch64::DestructiveBinary:
  case AArch64::DestructiveBinaryImm:
  case AArch64::DestructiveBinaryComm:
  case AArch64::DestructiveBinaryCommWithRev:
    DOP.add(MI.getOperand(PredIdx))
       .addReg(MI.getOperand(DOPIdx).getReg(), RegState::Kill)
       .add(MI.getOperand(SrcIdx));
    break;
  case AArch64::DestructiveTernaryCommWithRev:
    DOP.add(MI.getOperand(PredIdx))
        .addReg(MI.getOperand(DOPIdx).getReg(), RegState::Kill)
        .add(MI.getOperand(SrcIdx))
        .add(MI.getOperand(Src2Idx));
    break;
  }

  if (PRFX) {
    finalizeBundle(MBB, PRFX->getIterator(), MBBI->getIterator());
    transferImpOps(MI, PRFX, DOP);
  } else
    transferImpOps(MI, DOP, DOP);

  MI.eraseFromParent();
  return true;
}

bool AArch64ExpandPseudo::expandSetTagLoop(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  DebugLoc DL = MI.getDebugLoc();
  Register SizeReg = MI.getOperand(0).getReg();
  Register AddressReg = MI.getOperand(1).getReg();

  MachineFunction *MF = MBB.getParent();

  bool ZeroData = MI.getOpcode() == AArch64::STZGloop_wback;
  const unsigned OpCode1 =
      ZeroData ? AArch64::STZGPostIndex : AArch64::STGPostIndex;
  const unsigned OpCode2 =
      ZeroData ? AArch64::STZ2GPostIndex : AArch64::ST2GPostIndex;

  unsigned Size = MI.getOperand(2).getImm();
  assert(Size > 0 && Size % 16 == 0);
  if (Size % (16 * 2) != 0) {
    BuildMI(MBB, MBBI, DL, TII->get(OpCode1), AddressReg)
        .addReg(AddressReg)
        .addReg(AddressReg)
        .addImm(1);
    Size -= 16;
  }
  MachineBasicBlock::iterator I =
      BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVi64imm), SizeReg)
          .addImm(Size);
  expandMOVImm(MBB, I, 64);

  auto LoopBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());
  auto DoneBB = MF->CreateMachineBasicBlock(MBB.getBasicBlock());

  MF->insert(++MBB.getIterator(), LoopBB);
  MF->insert(++LoopBB->getIterator(), DoneBB);

  BuildMI(LoopBB, DL, TII->get(OpCode2))
      .addDef(AddressReg)
      .addReg(AddressReg)
      .addReg(AddressReg)
      .addImm(2)
      .cloneMemRefs(MI)
      .setMIFlags(MI.getFlags());
  BuildMI(LoopBB, DL, TII->get(AArch64::SUBSXri))
      .addDef(SizeReg)
      .addReg(SizeReg)
      .addImm(16 * 2)
      .addImm(0);
  BuildMI(LoopBB, DL, TII->get(AArch64::Bcc))
      .addImm(AArch64CC::NE)
      .addMBB(LoopBB)
      .addReg(AArch64::NZCV, RegState::Implicit | RegState::Kill);

  LoopBB->addSuccessor(LoopBB);
  LoopBB->addSuccessor(DoneBB);

  DoneBB->splice(DoneBB->end(), &MBB, MI, MBB.end());
  DoneBB->transferSuccessors(&MBB);

  MBB.addSuccessor(LoopBB);

  NextMBBI = MBB.end();
  MI.eraseFromParent();
  // Recompute liveness bottom up.
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *DoneBB);
  computeAndAddLiveIns(LiveRegs, *LoopBB);
  // Do an extra pass in the loop to get the loop carried dependencies right.
  // FIXME: is this necessary?
  LoopBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *LoopBB);
  DoneBB->clearLiveIns();
  computeAndAddLiveIns(LiveRegs, *DoneBB);

  return true;
}

bool AArch64ExpandPseudo::expandSVESpillFill(MachineBasicBlock &MBB,
                                             MachineBasicBlock::iterator MBBI,
                                             unsigned Opc, unsigned N) {
  assert((Opc == AArch64::LDR_ZXI || Opc == AArch64::STR_ZXI ||
          Opc == AArch64::LDR_PXI || Opc == AArch64::STR_PXI) &&
         "Unexpected opcode");
  unsigned RState = (Opc == AArch64::LDR_ZXI || Opc == AArch64::LDR_PXI)
                        ? RegState::Define
                        : 0;
  unsigned sub0 = (Opc == AArch64::LDR_ZXI || Opc == AArch64::STR_ZXI)
                      ? AArch64::zsub0
                      : AArch64::psub0;
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  MachineInstr &MI = *MBBI;
  for (unsigned Offset = 0; Offset < N; ++Offset) {
    int ImmOffset = MI.getOperand(2).getImm() + Offset;
    bool Kill = (Offset + 1 == N) ? MI.getOperand(1).isKill() : false;
    assert(ImmOffset >= -256 && ImmOffset < 256 &&
           "Immediate spill offset out of range");
    BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc))
        .addReg(TRI->getSubReg(MI.getOperand(0).getReg(), sub0 + Offset),
                RState)
        .addReg(MI.getOperand(1).getReg(), getKillRegState(Kill))
        .addImm(ImmOffset);
  }
  MI.eraseFromParent();
  return true;
}

// Create a call with the passed opcode and explicit operands, copying over all
// the implicit operands from *MBBI, starting at the regmask.
static MachineInstr *createCallWithOps(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator MBBI,
                                       const AArch64InstrInfo *TII,
                                       unsigned Opcode,
                                       ArrayRef<MachineOperand> ExplicitOps,
                                       unsigned RegMaskStartIdx) {
  // Build the MI, with explicit operands first (including the call target).
  MachineInstr *Call = BuildMI(MBB, MBBI, MBBI->getDebugLoc(), TII->get(Opcode))
                           .add(ExplicitOps)
                           .getInstr();

  // Register arguments are added during ISel, but cannot be added as explicit
  // operands of the branch as it expects to be B <target> which is only one
  // operand. Instead they are implicit operands used by the branch.
  while (!MBBI->getOperand(RegMaskStartIdx).isRegMask()) {
    const MachineOperand &MOP = MBBI->getOperand(RegMaskStartIdx);
    assert(MOP.isReg() && "can only add register operands");
    Call->addOperand(MachineOperand::CreateReg(
        MOP.getReg(), /*Def=*/false, /*Implicit=*/true, /*isKill=*/false,
        /*isDead=*/false, /*isUndef=*/MOP.isUndef()));
    RegMaskStartIdx++;
  }
  for (const MachineOperand &MO :
       llvm::drop_begin(MBBI->operands(), RegMaskStartIdx))
    Call->addOperand(MO);

  return Call;
}

// Create a call to CallTarget, copying over all the operands from *MBBI,
// starting at the regmask.
static MachineInstr *createCall(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MBBI,
                                const AArch64InstrInfo *TII,
                                MachineOperand &CallTarget,
                                unsigned RegMaskStartIdx) {
  unsigned Opc = CallTarget.isGlobal() ? AArch64::BL : AArch64::BLR;

  assert((CallTarget.isGlobal() || CallTarget.isReg()) &&
         "invalid operand for regular call");
  return createCallWithOps(MBB, MBBI, TII, Opc, CallTarget, RegMaskStartIdx);
}

bool AArch64ExpandPseudo::expandCALL_RVMARKER(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) {
  // Expand CALL_RVMARKER pseudo to:
  // - a branch to the call target, followed by
  // - the special `mov x29, x29` marker, and
  // - another branch, to the runtime function
  // Mark the sequence as bundle, to avoid passes moving other code in between.
  MachineInstr &MI = *MBBI;
  MachineOperand &RVTarget = MI.getOperand(0);
  assert(RVTarget.isGlobal() && "invalid operand for attached call");

  MachineInstr *OriginalCall = nullptr;

  if (MI.getOpcode() == AArch64::BLRA_RVMARKER) {
    // ptrauth call.
    const MachineOperand &CallTarget = MI.getOperand(1);
    const MachineOperand &Key = MI.getOperand(2);
    const MachineOperand &IntDisc = MI.getOperand(3);
    const MachineOperand &AddrDisc = MI.getOperand(4);

    assert((Key.getImm() == AArch64PACKey::IA ||
            Key.getImm() == AArch64PACKey::IB) &&
           "Invalid auth call key");

    MachineOperand Ops[] = {CallTarget, Key, IntDisc, AddrDisc};

    OriginalCall = createCallWithOps(MBB, MBBI, TII, AArch64::BLRA, Ops,
                                     /*RegMaskStartIdx=*/5);
  } else {
    assert(MI.getOpcode() == AArch64::BLR_RVMARKER && "unknown rvmarker MI");
    OriginalCall = createCall(MBB, MBBI, TII, MI.getOperand(1),
                              // Regmask starts after the RV and call targets.
                              /*RegMaskStartIdx=*/2);
  }

  BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ORRXrs))
                     .addReg(AArch64::FP, RegState::Define)
                     .addReg(AArch64::XZR)
                     .addReg(AArch64::FP)
                     .addImm(0);

  auto *RVCall = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::BL))
                     .add(RVTarget)
                     .getInstr();

  if (MI.shouldUpdateCallSiteInfo())
    MBB.getParent()->moveCallSiteInfo(&MI, OriginalCall);

  MI.eraseFromParent();
  finalizeBundle(MBB, OriginalCall->getIterator(),
                 std::next(RVCall->getIterator()));
  return true;
}

bool AArch64ExpandPseudo::expandCALL_BTI(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MBBI) {
  // Expand CALL_BTI pseudo to:
  // - a branch to the call target
  // - a BTI instruction
  // Mark the sequence as a bundle, to avoid passes moving other code in
  // between.
  MachineInstr &MI = *MBBI;
  MachineInstr *Call = createCall(MBB, MBBI, TII, MI.getOperand(0),
                                  // Regmask starts after the call target.
                                  /*RegMaskStartIdx=*/1);

  Call->setCFIType(*MBB.getParent(), MI.getCFIType());

  MachineInstr *BTI =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::HINT))
          // BTI J so that setjmp can to BR to this.
          .addImm(36)
          .getInstr();

  if (MI.shouldUpdateCallSiteInfo())
    MBB.getParent()->moveCallSiteInfo(&MI, Call);

  MI.eraseFromParent();
  finalizeBundle(MBB, Call->getIterator(), std::next(BTI->getIterator()));
  return true;
}

bool AArch64ExpandPseudo::expandStoreSwiftAsyncContext(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI) {
  Register CtxReg = MBBI->getOperand(0).getReg();
  Register BaseReg = MBBI->getOperand(1).getReg();
  int Offset = MBBI->getOperand(2).getImm();
  DebugLoc DL(MBBI->getDebugLoc());
  auto &STI = MBB.getParent()->getSubtarget<AArch64Subtarget>();

  if (STI.getTargetTriple().getArchName() != "arm64e") {
    BuildMI(MBB, MBBI, DL, TII->get(AArch64::STRXui))
        .addUse(CtxReg)
        .addUse(BaseReg)
        .addImm(Offset / 8)
        .setMIFlag(MachineInstr::FrameSetup);
    MBBI->eraseFromParent();
    return true;
  }

  // We need to sign the context in an address-discriminated way. 0xc31a is a
  // fixed random value, chosen as part of the ABI.
  //     add x16, xBase, #Offset
  //     movk x16, #0xc31a, lsl #48
  //     mov x17, x22/xzr
  //     pacdb x17, x16
  //     str x17, [xBase, #Offset]
  unsigned Opc = Offset >= 0 ? AArch64::ADDXri : AArch64::SUBXri;
  BuildMI(MBB, MBBI, DL, TII->get(Opc), AArch64::X16)
      .addUse(BaseReg)
      .addImm(abs(Offset))
      .addImm(0)
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::MOVKXi), AArch64::X16)
      .addUse(AArch64::X16)
      .addImm(0xc31a)
      .addImm(48)
      .setMIFlag(MachineInstr::FrameSetup);
  // We're not allowed to clobber X22 (and couldn't clobber XZR if we tried), so
  // move it somewhere before signing.
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::ORRXrs), AArch64::X17)
      .addUse(AArch64::XZR)
      .addUse(CtxReg)
      .addImm(0)
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::PACDB), AArch64::X17)
      .addUse(AArch64::X17)
      .addUse(AArch64::X16)
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MBBI, DL, TII->get(AArch64::STRXui))
      .addUse(AArch64::X17)
      .addUse(BaseReg)
      .addImm(Offset / 8)
      .setMIFlag(MachineInstr::FrameSetup);

  MBBI->eraseFromParent();
  return true;
}

MachineBasicBlock *
AArch64ExpandPseudo::expandRestoreZA(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  assert((std::next(MBBI) != MBB.end() ||
          MI.getParent()->successors().begin() !=
              MI.getParent()->successors().end()) &&
         "Unexpected unreachable in block that restores ZA");

  // Compare TPIDR2_EL0 value against 0.
  DebugLoc DL = MI.getDebugLoc();
  MachineInstrBuilder Cbz = BuildMI(MBB, MBBI, DL, TII->get(AArch64::CBZX))
                                .add(MI.getOperand(0));

  // Split MBB and create two new blocks:
  //  - MBB now contains all instructions before RestoreZAPseudo.
  //  - SMBB contains the RestoreZAPseudo instruction only.
  //  - EndBB contains all instructions after RestoreZAPseudo.
  MachineInstr &PrevMI = *std::prev(MBBI);
  MachineBasicBlock *SMBB = MBB.splitAt(PrevMI, /*UpdateLiveIns*/ true);
  MachineBasicBlock *EndBB = std::next(MI.getIterator()) == SMBB->end()
                                 ? *SMBB->successors().begin()
                                 : SMBB->splitAt(MI, /*UpdateLiveIns*/ true);

  // Add the SMBB label to the TB[N]Z instruction & create a branch to EndBB.
  Cbz.addMBB(SMBB);
  BuildMI(&MBB, DL, TII->get(AArch64::B))
      .addMBB(EndBB);
  MBB.addSuccessor(EndBB);

  // Replace the pseudo with a call (BL).
  MachineInstrBuilder MIB =
      BuildMI(*SMBB, SMBB->end(), DL, TII->get(AArch64::BL));
  MIB.addReg(MI.getOperand(1).getReg(), RegState::Implicit);
  for (unsigned I = 2; I < MI.getNumOperands(); ++I)
    MIB.add(MI.getOperand(I));
  BuildMI(SMBB, DL, TII->get(AArch64::B)).addMBB(EndBB);

  MI.eraseFromParent();
  return EndBB;
}

MachineBasicBlock *
AArch64ExpandPseudo::expandCondSMToggle(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MBBI) {
  MachineInstr &MI = *MBBI;
  // In the case of a smstart/smstop before a unreachable, just remove the pseudo.
  // Exception handling code generated by Clang may introduce unreachables and it
  // seems unnecessary to restore pstate.sm when that happens. Note that it is
  // not just an optimisation, the code below expects a successor instruction/block
  // in order to split the block at MBBI.
  if (std::next(MBBI) == MBB.end() &&
      MI.getParent()->successors().begin() ==
          MI.getParent()->successors().end()) {
    MI.eraseFromParent();
    return &MBB;
  }

  // Expand the pseudo into smstart or smstop instruction. The pseudo has the
  // following operands:
  //
  //   MSRpstatePseudo <za|sm|both>, <0|1>, condition[, pstate.sm], <regmask>
  //
  // The pseudo is expanded into a conditional smstart/smstop, with a
  // check if pstate.sm (register) equals the expected value, and if not,
  // invokes the smstart/smstop.
  //
  // As an example, the following block contains a normal call from a
  // streaming-compatible function:
  //
  // OrigBB:
  //   MSRpstatePseudo 3, 0, IfCallerIsStreaming, %0, <regmask>  <- Cond SMSTOP
  //   bl @normal_callee
  //   MSRpstatePseudo 3, 1, IfCallerIsStreaming, %0, <regmask>  <- Cond SMSTART
  //
  // ...which will be transformed into:
  //
  // OrigBB:
  //   TBNZx %0:gpr64, 0, SMBB
  //   b EndBB
  //
  // SMBB:
  //   MSRpstatesvcrImm1 3, 0, <regmask>                  <- SMSTOP
  //
  // EndBB:
  //   bl @normal_callee
  //   MSRcond_pstatesvcrImm1 3, 1, <regmask>             <- SMSTART
  //
  DebugLoc DL = MI.getDebugLoc();

  // Create the conditional branch based on the third operand of the
  // instruction, which tells us if we are wrapping a normal or streaming
  // function.
  // We test the live value of pstate.sm and toggle pstate.sm if this is not the
  // expected value for the callee (0 for a normal callee and 1 for a streaming
  // callee).
  unsigned Opc;
  switch (MI.getOperand(2).getImm()) {
  case AArch64SME::Always:
    llvm_unreachable("Should have matched to instruction directly");
  case AArch64SME::IfCallerIsStreaming:
    Opc = AArch64::TBNZW;
    break;
  case AArch64SME::IfCallerIsNonStreaming:
    Opc = AArch64::TBZW;
    break;
  }
  auto PStateSM = MI.getOperand(3).getReg();
  auto TRI = MBB.getParent()->getSubtarget().getRegisterInfo();
  unsigned SMReg32 = TRI->getSubReg(PStateSM, AArch64::sub_32);
  MachineInstrBuilder Tbx =
      BuildMI(MBB, MBBI, DL, TII->get(Opc)).addReg(SMReg32).addImm(0);

  // Split MBB and create two new blocks:
  //  - MBB now contains all instructions before MSRcond_pstatesvcrImm1.
  //  - SMBB contains the MSRcond_pstatesvcrImm1 instruction only.
  //  - EndBB contains all instructions after MSRcond_pstatesvcrImm1.
  MachineInstr &PrevMI = *std::prev(MBBI);
  MachineBasicBlock *SMBB = MBB.splitAt(PrevMI, /*UpdateLiveIns*/ true);
  MachineBasicBlock *EndBB = std::next(MI.getIterator()) == SMBB->end()
                                 ? *SMBB->successors().begin()
                                 : SMBB->splitAt(MI, /*UpdateLiveIns*/ true);

  // Add the SMBB label to the TB[N]Z instruction & create a branch to EndBB.
  Tbx.addMBB(SMBB);
  BuildMI(&MBB, DL, TII->get(AArch64::B))
      .addMBB(EndBB);
  MBB.addSuccessor(EndBB);

  // Create the SMSTART/SMSTOP (MSRpstatesvcrImm1) instruction in SMBB.
  MachineInstrBuilder MIB = BuildMI(*SMBB, SMBB->begin(), MI.getDebugLoc(),
                                    TII->get(AArch64::MSRpstatesvcrImm1));
  // Copy all but the second and third operands of MSRcond_pstatesvcrImm1 (as
  // these contain the CopyFromReg for the first argument and the flag to
  // indicate whether the callee is streaming or normal).
  MIB.add(MI.getOperand(0));
  MIB.add(MI.getOperand(1));
  for (unsigned i = 4; i < MI.getNumOperands(); ++i)
    MIB.add(MI.getOperand(i));

  BuildMI(SMBB, DL, TII->get(AArch64::B)).addMBB(EndBB);

  MI.eraseFromParent();
  return EndBB;
}

bool AArch64ExpandPseudo::expandMultiVecPseudo(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
    TargetRegisterClass ContiguousClass, TargetRegisterClass StridedClass,
    unsigned ContiguousOp, unsigned StridedOpc) {
  MachineInstr &MI = *MBBI;
  Register Tuple = MI.getOperand(0).getReg();

  auto ContiguousRange = ContiguousClass.getRegisters();
  auto StridedRange = StridedClass.getRegisters();
  unsigned Opc;
  if (llvm::is_contained(ContiguousRange, Tuple.asMCReg())) {
    Opc = ContiguousOp;
  } else if (llvm::is_contained(StridedRange, Tuple.asMCReg())) {
    Opc = StridedOpc;
  } else
    llvm_unreachable("Cannot expand Multi-Vector pseudo");

  MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(Opc))
                                .add(MI.getOperand(0))
                                .add(MI.getOperand(1))
                                .add(MI.getOperand(2))
                                .add(MI.getOperand(3));
  transferImpOps(MI, MIB, MIB);
  MI.eraseFromParent();
  return true;
}

/// If MBBI references a pseudo instruction that should be expanded here,
/// do the expansion and return true.  Otherwise return false.
bool AArch64ExpandPseudo::expandMI(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI,
                                   MachineBasicBlock::iterator &NextMBBI) {
  MachineInstr &MI = *MBBI;
  unsigned Opcode = MI.getOpcode();

  // Check if we can expand the destructive op
  int OrigInstr = AArch64::getSVEPseudoMap(MI.getOpcode());
  if (OrigInstr != -1) {
    auto &Orig = TII->get(OrigInstr);
    if ((Orig.TSFlags & AArch64::DestructiveInstTypeMask) !=
        AArch64::NotDestructive) {
      return expand_DestructiveOp(MI, MBB, MBBI);
    }
  }

  switch (Opcode) {
  default:
    break;

  case AArch64::BSPv8i8:
  case AArch64::BSPv16i8: {
    Register DstReg = MI.getOperand(0).getReg();
    if (DstReg == MI.getOperand(3).getReg()) {
      // Expand to BIT
      BuildMI(MBB, MBBI, MI.getDebugLoc(),
              TII->get(Opcode == AArch64::BSPv8i8 ? AArch64::BITv8i8
                                                  : AArch64::BITv16i8))
          .add(MI.getOperand(0))
          .add(MI.getOperand(3))
          .add(MI.getOperand(2))
          .add(MI.getOperand(1));
    } else if (DstReg == MI.getOperand(2).getReg()) {
      // Expand to BIF
      BuildMI(MBB, MBBI, MI.getDebugLoc(),
              TII->get(Opcode == AArch64::BSPv8i8 ? AArch64::BIFv8i8
                                                  : AArch64::BIFv16i8))
          .add(MI.getOperand(0))
          .add(MI.getOperand(2))
          .add(MI.getOperand(3))
          .add(MI.getOperand(1));
    } else {
      // Expand to BSL, use additional move if required
      if (DstReg == MI.getOperand(1).getReg()) {
        BuildMI(MBB, MBBI, MI.getDebugLoc(),
                TII->get(Opcode == AArch64::BSPv8i8 ? AArch64::BSLv8i8
                                                    : AArch64::BSLv16i8))
            .add(MI.getOperand(0))
            .add(MI.getOperand(1))
            .add(MI.getOperand(2))
            .add(MI.getOperand(3));
      } else {
        BuildMI(MBB, MBBI, MI.getDebugLoc(),
                TII->get(Opcode == AArch64::BSPv8i8 ? AArch64::ORRv8i8
                                                    : AArch64::ORRv16i8))
            .addReg(DstReg,
                    RegState::Define |
                        getRenamableRegState(MI.getOperand(0).isRenamable()))
            .add(MI.getOperand(1))
            .add(MI.getOperand(1));
        BuildMI(MBB, MBBI, MI.getDebugLoc(),
                TII->get(Opcode == AArch64::BSPv8i8 ? AArch64::BSLv8i8
                                                    : AArch64::BSLv16i8))
            .add(MI.getOperand(0))
            .addReg(DstReg,
                    RegState::Kill |
                        getRenamableRegState(MI.getOperand(0).isRenamable()))
            .add(MI.getOperand(2))
            .add(MI.getOperand(3));
      }
    }
    MI.eraseFromParent();
    return true;
  }

  case AArch64::ADDWrr:
  case AArch64::SUBWrr:
  case AArch64::ADDXrr:
  case AArch64::SUBXrr:
  case AArch64::ADDSWrr:
  case AArch64::SUBSWrr:
  case AArch64::ADDSXrr:
  case AArch64::SUBSXrr:
  case AArch64::ANDWrr:
  case AArch64::ANDXrr:
  case AArch64::BICWrr:
  case AArch64::BICXrr:
  case AArch64::ANDSWrr:
  case AArch64::ANDSXrr:
  case AArch64::BICSWrr:
  case AArch64::BICSXrr:
  case AArch64::EONWrr:
  case AArch64::EONXrr:
  case AArch64::EORWrr:
  case AArch64::EORXrr:
  case AArch64::ORNWrr:
  case AArch64::ORNXrr:
  case AArch64::ORRWrr:
  case AArch64::ORRXrr: {
    unsigned Opcode;
    switch (MI.getOpcode()) {
    default:
      return false;
    case AArch64::ADDWrr:      Opcode = AArch64::ADDWrs; break;
    case AArch64::SUBWrr:      Opcode = AArch64::SUBWrs; break;
    case AArch64::ADDXrr:      Opcode = AArch64::ADDXrs; break;
    case AArch64::SUBXrr:      Opcode = AArch64::SUBXrs; break;
    case AArch64::ADDSWrr:     Opcode = AArch64::ADDSWrs; break;
    case AArch64::SUBSWrr:     Opcode = AArch64::SUBSWrs; break;
    case AArch64::ADDSXrr:     Opcode = AArch64::ADDSXrs; break;
    case AArch64::SUBSXrr:     Opcode = AArch64::SUBSXrs; break;
    case AArch64::ANDWrr:      Opcode = AArch64::ANDWrs; break;
    case AArch64::ANDXrr:      Opcode = AArch64::ANDXrs; break;
    case AArch64::BICWrr:      Opcode = AArch64::BICWrs; break;
    case AArch64::BICXrr:      Opcode = AArch64::BICXrs; break;
    case AArch64::ANDSWrr:     Opcode = AArch64::ANDSWrs; break;
    case AArch64::ANDSXrr:     Opcode = AArch64::ANDSXrs; break;
    case AArch64::BICSWrr:     Opcode = AArch64::BICSWrs; break;
    case AArch64::BICSXrr:     Opcode = AArch64::BICSXrs; break;
    case AArch64::EONWrr:      Opcode = AArch64::EONWrs; break;
    case AArch64::EONXrr:      Opcode = AArch64::EONXrs; break;
    case AArch64::EORWrr:      Opcode = AArch64::EORWrs; break;
    case AArch64::EORXrr:      Opcode = AArch64::EORXrs; break;
    case AArch64::ORNWrr:      Opcode = AArch64::ORNWrs; break;
    case AArch64::ORNXrr:      Opcode = AArch64::ORNXrs; break;
    case AArch64::ORRWrr:      Opcode = AArch64::ORRWrs; break;
    case AArch64::ORRXrr:      Opcode = AArch64::ORRXrs; break;
    }
    MachineFunction &MF = *MBB.getParent();
    // Try to create new inst without implicit operands added.
    MachineInstr *NewMI = MF.CreateMachineInstr(
        TII->get(Opcode), MI.getDebugLoc(), /*NoImplicit=*/true);
    MBB.insert(MBBI, NewMI);
    MachineInstrBuilder MIB1(MF, NewMI);
    MIB1->setPCSections(MF, MI.getPCSections());
    MIB1.addReg(MI.getOperand(0).getReg(), RegState::Define)
        .add(MI.getOperand(1))
        .add(MI.getOperand(2))
        .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0));
    transferImpOps(MI, MIB1, MIB1);
    if (auto DebugNumber = MI.peekDebugInstrNum())
      NewMI->setDebugInstrNum(DebugNumber);
    MI.eraseFromParent();
    return true;
  }

  case AArch64::LOADgot: {
    MachineFunction *MF = MBB.getParent();
    Register DstReg = MI.getOperand(0).getReg();
    const MachineOperand &MO1 = MI.getOperand(1);
    unsigned Flags = MO1.getTargetFlags();

    if (MF->getTarget().getCodeModel() == CodeModel::Tiny) {
      // Tiny codemodel expand to LDR
      MachineInstrBuilder MIB = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                                        TII->get(AArch64::LDRXl), DstReg);

      if (MO1.isGlobal()) {
        MIB.addGlobalAddress(MO1.getGlobal(), 0, Flags);
      } else if (MO1.isSymbol()) {
        MIB.addExternalSymbol(MO1.getSymbolName(), Flags);
      } else {
        assert(MO1.isCPI() &&
               "Only expect globals, externalsymbols, or constant pools");
        MIB.addConstantPoolIndex(MO1.getIndex(), MO1.getOffset(), Flags);
      }
    } else {
      // Small codemodel expand into ADRP + LDR.
      MachineFunction &MF = *MI.getParent()->getParent();
      DebugLoc DL = MI.getDebugLoc();
      MachineInstrBuilder MIB1 =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ADRP), DstReg);

      MachineInstrBuilder MIB2;
      if (MF.getSubtarget<AArch64Subtarget>().isTargetILP32()) {
        auto TRI = MBB.getParent()->getSubtarget().getRegisterInfo();
        unsigned Reg32 = TRI->getSubReg(DstReg, AArch64::sub_32);
        unsigned DstFlags = MI.getOperand(0).getTargetFlags();
        MIB2 = BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::LDRWui))
                   .addDef(Reg32)
                   .addReg(DstReg, RegState::Kill)
                   .addReg(DstReg, DstFlags | RegState::Implicit);
      } else {
        Register DstReg = MI.getOperand(0).getReg();
        MIB2 = BuildMI(MBB, MBBI, DL, TII->get(AArch64::LDRXui))
                   .add(MI.getOperand(0))
                   .addUse(DstReg, RegState::Kill);
      }

      if (MO1.isGlobal()) {
        MIB1.addGlobalAddress(MO1.getGlobal(), 0, Flags | AArch64II::MO_PAGE);
        MIB2.addGlobalAddress(MO1.getGlobal(), 0,
                              Flags | AArch64II::MO_PAGEOFF | AArch64II::MO_NC);
      } else if (MO1.isSymbol()) {
        MIB1.addExternalSymbol(MO1.getSymbolName(), Flags | AArch64II::MO_PAGE);
        MIB2.addExternalSymbol(MO1.getSymbolName(), Flags |
                                                        AArch64II::MO_PAGEOFF |
                                                        AArch64II::MO_NC);
      } else {
        assert(MO1.isCPI() &&
               "Only expect globals, externalsymbols, or constant pools");
        MIB1.addConstantPoolIndex(MO1.getIndex(), MO1.getOffset(),
                                  Flags | AArch64II::MO_PAGE);
        MIB2.addConstantPoolIndex(MO1.getIndex(), MO1.getOffset(),
                                  Flags | AArch64II::MO_PAGEOFF |
                                      AArch64II::MO_NC);
      }

      transferImpOps(MI, MIB1, MIB2);
    }
    MI.eraseFromParent();
    return true;
  }
  case AArch64::MOVaddrBA: {
    MachineFunction &MF = *MI.getParent()->getParent();
    if (MF.getSubtarget<AArch64Subtarget>().isTargetMachO()) {
      // blockaddress expressions have to come from a constant pool because the
      // largest addend (and hence offset within a function) allowed for ADRP is
      // only 8MB.
      const BlockAddress *BA = MI.getOperand(1).getBlockAddress();
      assert(MI.getOperand(1).getOffset() == 0 && "unexpected offset");

      MachineConstantPool *MCP = MF.getConstantPool();
      unsigned CPIdx = MCP->getConstantPoolIndex(BA, Align(8));

      Register DstReg = MI.getOperand(0).getReg();
      auto MIB1 =
          BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ADRP), DstReg)
              .addConstantPoolIndex(CPIdx, 0, AArch64II::MO_PAGE);
      auto MIB2 = BuildMI(MBB, MBBI, MI.getDebugLoc(),
                          TII->get(AArch64::LDRXui), DstReg)
                      .addUse(DstReg)
                      .addConstantPoolIndex(
                          CPIdx, 0, AArch64II::MO_PAGEOFF | AArch64II::MO_NC);
      transferImpOps(MI, MIB1, MIB2);
      MI.eraseFromParent();
      return true;
    }
  }
    [[fallthrough]];
  case AArch64::MOVaddr:
  case AArch64::MOVaddrJT:
  case AArch64::MOVaddrCP:
  case AArch64::MOVaddrTLS:
  case AArch64::MOVaddrEXT: {
    // Expand into ADRP + ADD.
    Register DstReg = MI.getOperand(0).getReg();
    assert(DstReg != AArch64::XZR);
    MachineInstrBuilder MIB1 =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ADRP), DstReg)
            .add(MI.getOperand(1));

    if (MI.getOperand(1).getTargetFlags() & AArch64II::MO_TAGGED) {
      // MO_TAGGED on the page indicates a tagged address. Set the tag now.
      // We do so by creating a MOVK that sets bits 48-63 of the register to
      // (global address + 0x100000000 - PC) >> 48. This assumes that we're in
      // the small code model so we can assume a binary size of <= 4GB, which
      // makes the untagged PC relative offset positive. The binary must also be
      // loaded into address range [0, 2^48). Both of these properties need to
      // be ensured at runtime when using tagged addresses.
      auto Tag = MI.getOperand(1);
      Tag.setTargetFlags(AArch64II::MO_PREL | AArch64II::MO_G3);
      Tag.setOffset(0x100000000);
      BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::MOVKXi), DstReg)
          .addReg(DstReg)
          .add(Tag)
          .addImm(48);
    }

    MachineInstrBuilder MIB2 =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ADDXri))
            .add(MI.getOperand(0))
            .addReg(DstReg)
            .add(MI.getOperand(2))
            .addImm(0);

    transferImpOps(MI, MIB1, MIB2);
    MI.eraseFromParent();
    return true;
  }
  case AArch64::ADDlowTLS:
    // Produce a plain ADD
    BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::ADDXri))
        .add(MI.getOperand(0))
        .add(MI.getOperand(1))
        .add(MI.getOperand(2))
        .addImm(0);
    MI.eraseFromParent();
    return true;

  case AArch64::MOVbaseTLS: {
    Register DstReg = MI.getOperand(0).getReg();
    auto SysReg = AArch64SysReg::TPIDR_EL0;
    MachineFunction *MF = MBB.getParent();
    if (MF->getSubtarget<AArch64Subtarget>().useEL3ForTP())
      SysReg = AArch64SysReg::TPIDR_EL3;
    else if (MF->getSubtarget<AArch64Subtarget>().useEL2ForTP())
      SysReg = AArch64SysReg::TPIDR_EL2;
    else if (MF->getSubtarget<AArch64Subtarget>().useEL1ForTP())
      SysReg = AArch64SysReg::TPIDR_EL1;
    else if (MF->getSubtarget<AArch64Subtarget>().useROEL0ForTP())
      SysReg = AArch64SysReg::TPIDRRO_EL0;
    BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::MRS), DstReg)
        .addImm(SysReg);
    MI.eraseFromParent();
    return true;
  }

  case AArch64::MOVi32imm:
    return expandMOVImm(MBB, MBBI, 32);
  case AArch64::MOVi64imm:
    return expandMOVImm(MBB, MBBI, 64);
  case AArch64::RET_ReallyLR: {
    // Hiding the LR use with RET_ReallyLR may lead to extra kills in the
    // function and missing live-ins. We are fine in practice because callee
    // saved register handling ensures the register value is restored before
    // RET, but we need the undef flag here to appease the MachineVerifier
    // liveness checks.
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::RET))
          .addReg(AArch64::LR, RegState::Undef);
    transferImpOps(MI, MIB, MIB);
    MI.eraseFromParent();
    return true;
  }
  case AArch64::CMP_SWAP_8:
    return expandCMP_SWAP(MBB, MBBI, AArch64::LDAXRB, AArch64::STLXRB,
                          AArch64::SUBSWrx,
                          AArch64_AM::getArithExtendImm(AArch64_AM::UXTB, 0),
                          AArch64::WZR, NextMBBI);
  case AArch64::CMP_SWAP_16:
    return expandCMP_SWAP(MBB, MBBI, AArch64::LDAXRH, AArch64::STLXRH,
                          AArch64::SUBSWrx,
                          AArch64_AM::getArithExtendImm(AArch64_AM::UXTH, 0),
                          AArch64::WZR, NextMBBI);
  case AArch64::CMP_SWAP_32:
    return expandCMP_SWAP(MBB, MBBI, AArch64::LDAXRW, AArch64::STLXRW,
                          AArch64::SUBSWrs,
                          AArch64_AM::getShifterImm(AArch64_AM::LSL, 0),
                          AArch64::WZR, NextMBBI);
  case AArch64::CMP_SWAP_64:
    return expandCMP_SWAP(MBB, MBBI,
                          AArch64::LDAXRX, AArch64::STLXRX, AArch64::SUBSXrs,
                          AArch64_AM::getShifterImm(AArch64_AM::LSL, 0),
                          AArch64::XZR, NextMBBI);
  case AArch64::CMP_SWAP_128:
  case AArch64::CMP_SWAP_128_RELEASE:
  case AArch64::CMP_SWAP_128_ACQUIRE:
  case AArch64::CMP_SWAP_128_MONOTONIC:
    return expandCMP_SWAP_128(MBB, MBBI, NextMBBI);

  case AArch64::AESMCrrTied:
  case AArch64::AESIMCrrTied: {
    MachineInstrBuilder MIB =
    BuildMI(MBB, MBBI, MI.getDebugLoc(),
            TII->get(Opcode == AArch64::AESMCrrTied ? AArch64::AESMCrr :
                                                      AArch64::AESIMCrr))
      .add(MI.getOperand(0))
      .add(MI.getOperand(1));
    transferImpOps(MI, MIB, MIB);
    MI.eraseFromParent();
    return true;
   }
   case AArch64::IRGstack: {
     MachineFunction &MF = *MBB.getParent();
     const AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
     const AArch64FrameLowering *TFI =
         MF.getSubtarget<AArch64Subtarget>().getFrameLowering();

     // IRG does not allow immediate offset. getTaggedBasePointerOffset should
     // almost always point to SP-after-prologue; if not, emit a longer
     // instruction sequence.
     int BaseOffset = -AFI->getTaggedBasePointerOffset();
     Register FrameReg;
     StackOffset FrameRegOffset = TFI->resolveFrameOffsetReference(
         MF, BaseOffset, false /*isFixed*/, false /*isSVE*/, FrameReg,
         /*PreferFP=*/false,
         /*ForSimm=*/true);
     Register SrcReg = FrameReg;
     if (FrameRegOffset) {
       // Use output register as temporary.
       SrcReg = MI.getOperand(0).getReg();
       emitFrameOffset(MBB, &MI, MI.getDebugLoc(), SrcReg, FrameReg,
                       FrameRegOffset, TII);
     }
     BuildMI(MBB, MBBI, MI.getDebugLoc(), TII->get(AArch64::IRG))
         .add(MI.getOperand(0))
         .addUse(SrcReg)
         .add(MI.getOperand(2));
     MI.eraseFromParent();
     return true;
   }
   case AArch64::TAGPstack: {
     int64_t Offset = MI.getOperand(2).getImm();
     BuildMI(MBB, MBBI, MI.getDebugLoc(),
             TII->get(Offset >= 0 ? AArch64::ADDG : AArch64::SUBG))
         .add(MI.getOperand(0))
         .add(MI.getOperand(1))
         .addImm(std::abs(Offset))
         .add(MI.getOperand(4));
     MI.eraseFromParent();
     return true;
   }
   case AArch64::STGloop_wback:
   case AArch64::STZGloop_wback:
     return expandSetTagLoop(MBB, MBBI, NextMBBI);
   case AArch64::STGloop:
   case AArch64::STZGloop:
     report_fatal_error(
         "Non-writeback variants of STGloop / STZGloop should not "
         "survive past PrologEpilogInserter.");
   case AArch64::STR_ZZZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::STR_ZXI, 4);
   case AArch64::STR_ZZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::STR_ZXI, 3);
   case AArch64::STR_ZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::STR_ZXI, 2);
   case AArch64::STR_PPXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::STR_PXI, 2);
   case AArch64::LDR_ZZZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::LDR_ZXI, 4);
   case AArch64::LDR_ZZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::LDR_ZXI, 3);
   case AArch64::LDR_ZZXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::LDR_ZXI, 2);
   case AArch64::LDR_PPXI:
     return expandSVESpillFill(MBB, MBBI, AArch64::LDR_PXI, 2);
   case AArch64::BLR_RVMARKER:
   case AArch64::BLRA_RVMARKER:
     return expandCALL_RVMARKER(MBB, MBBI);
   case AArch64::BLR_BTI:
     return expandCALL_BTI(MBB, MBBI);
   case AArch64::StoreSwiftAsyncContext:
     return expandStoreSwiftAsyncContext(MBB, MBBI);
   case AArch64::RestoreZAPseudo: {
     auto *NewMBB = expandRestoreZA(MBB, MBBI);
     if (NewMBB != &MBB)
       NextMBBI = MBB.end(); // The NextMBBI iterator is invalidated.
     return true;
   }
   case AArch64::MSRpstatePseudo: {
     auto *NewMBB = expandCondSMToggle(MBB, MBBI);
     if (NewMBB != &MBB)
       NextMBBI = MBB.end(); // The NextMBBI iterator is invalidated.
     return true;
   }
   case AArch64::COALESCER_BARRIER_FPR16:
   case AArch64::COALESCER_BARRIER_FPR32:
   case AArch64::COALESCER_BARRIER_FPR64:
   case AArch64::COALESCER_BARRIER_FPR128:
     MI.eraseFromParent();
     return true;
   case AArch64::LD1B_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LD1B_2Z_IMM, AArch64::LD1B_2Z_STRIDED_IMM);
   case AArch64::LD1H_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LD1H_2Z_IMM, AArch64::LD1H_2Z_STRIDED_IMM);
   case AArch64::LD1W_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LD1W_2Z_IMM, AArch64::LD1W_2Z_STRIDED_IMM);
   case AArch64::LD1D_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LD1D_2Z_IMM, AArch64::LD1D_2Z_STRIDED_IMM);
   case AArch64::LDNT1B_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1B_2Z_IMM, AArch64::LDNT1B_2Z_STRIDED_IMM);
   case AArch64::LDNT1H_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1H_2Z_IMM, AArch64::LDNT1H_2Z_STRIDED_IMM);
   case AArch64::LDNT1W_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1W_2Z_IMM, AArch64::LDNT1W_2Z_STRIDED_IMM);
   case AArch64::LDNT1D_2Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1D_2Z_IMM, AArch64::LDNT1D_2Z_STRIDED_IMM);
   case AArch64::LD1B_2Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR2RegClass,
                                 AArch64::ZPR2StridedRegClass, AArch64::LD1B_2Z,
                                 AArch64::LD1B_2Z_STRIDED);
   case AArch64::LD1H_2Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR2RegClass,
                                 AArch64::ZPR2StridedRegClass, AArch64::LD1H_2Z,
                                 AArch64::LD1H_2Z_STRIDED);
   case AArch64::LD1W_2Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR2RegClass,
                                 AArch64::ZPR2StridedRegClass, AArch64::LD1W_2Z,
                                 AArch64::LD1W_2Z_STRIDED);
   case AArch64::LD1D_2Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR2RegClass,
                                 AArch64::ZPR2StridedRegClass, AArch64::LD1D_2Z,
                                 AArch64::LD1D_2Z_STRIDED);
   case AArch64::LDNT1B_2Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1B_2Z, AArch64::LDNT1B_2Z_STRIDED);
   case AArch64::LDNT1H_2Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1H_2Z, AArch64::LDNT1H_2Z_STRIDED);
   case AArch64::LDNT1W_2Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1W_2Z, AArch64::LDNT1W_2Z_STRIDED);
   case AArch64::LDNT1D_2Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR2RegClass, AArch64::ZPR2StridedRegClass,
         AArch64::LDNT1D_2Z, AArch64::LDNT1D_2Z_STRIDED);
   case AArch64::LD1B_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LD1B_4Z_IMM, AArch64::LD1B_4Z_STRIDED_IMM);
   case AArch64::LD1H_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LD1H_4Z_IMM, AArch64::LD1H_4Z_STRIDED_IMM);
   case AArch64::LD1W_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LD1W_4Z_IMM, AArch64::LD1W_4Z_STRIDED_IMM);
   case AArch64::LD1D_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LD1D_4Z_IMM, AArch64::LD1D_4Z_STRIDED_IMM);
   case AArch64::LDNT1B_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1B_4Z_IMM, AArch64::LDNT1B_4Z_STRIDED_IMM);
   case AArch64::LDNT1H_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1H_4Z_IMM, AArch64::LDNT1H_4Z_STRIDED_IMM);
   case AArch64::LDNT1W_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1W_4Z_IMM, AArch64::LDNT1W_4Z_STRIDED_IMM);
   case AArch64::LDNT1D_4Z_IMM_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1D_4Z_IMM, AArch64::LDNT1D_4Z_STRIDED_IMM);
   case AArch64::LD1B_4Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR4RegClass,
                                 AArch64::ZPR4StridedRegClass, AArch64::LD1B_4Z,
                                 AArch64::LD1B_4Z_STRIDED);
   case AArch64::LD1H_4Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR4RegClass,
                                 AArch64::ZPR4StridedRegClass, AArch64::LD1H_4Z,
                                 AArch64::LD1H_4Z_STRIDED);
   case AArch64::LD1W_4Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR4RegClass,
                                 AArch64::ZPR4StridedRegClass, AArch64::LD1W_4Z,
                                 AArch64::LD1W_4Z_STRIDED);
   case AArch64::LD1D_4Z_PSEUDO:
     return expandMultiVecPseudo(MBB, MBBI, AArch64::ZPR4RegClass,
                                 AArch64::ZPR4StridedRegClass, AArch64::LD1D_4Z,
                                 AArch64::LD1D_4Z_STRIDED);
   case AArch64::LDNT1B_4Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1B_4Z, AArch64::LDNT1B_4Z_STRIDED);
   case AArch64::LDNT1H_4Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1H_4Z, AArch64::LDNT1H_4Z_STRIDED);
   case AArch64::LDNT1W_4Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1W_4Z, AArch64::LDNT1W_4Z_STRIDED);
   case AArch64::LDNT1D_4Z_PSEUDO:
     return expandMultiVecPseudo(
         MBB, MBBI, AArch64::ZPR4RegClass, AArch64::ZPR4StridedRegClass,
         AArch64::LDNT1D_4Z, AArch64::LDNT1D_4Z_STRIDED);
  }
  return false;
}

/// Iterate over the instructions in basic block MBB and expand any
/// pseudo instructions.  Return true if anything was modified.
bool AArch64ExpandPseudo::expandMBB(MachineBasicBlock &MBB) {
  bool Modified = false;

  MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
  while (MBBI != E) {
    MachineBasicBlock::iterator NMBBI = std::next(MBBI);
    Modified |= expandMI(MBB, MBBI, NMBBI);
    MBBI = NMBBI;
  }

  return Modified;
}

bool AArch64ExpandPseudo::runOnMachineFunction(MachineFunction &MF) {
  TII = static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());

  bool Modified = false;
  for (auto &MBB : MF)
    Modified |= expandMBB(MBB);
  return Modified;
}

/// Returns an instance of the pseudo instruction expansion pass.
FunctionPass *llvm::createAArch64ExpandPseudoPass() {
  return new AArch64ExpandPseudo();
}
