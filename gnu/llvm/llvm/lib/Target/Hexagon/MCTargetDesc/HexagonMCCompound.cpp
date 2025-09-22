//=== HexagonMCCompound.cpp - Hexagon Compound checker  -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is looks at a packet and tries to form compound insns
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonBaseInfo.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "MCTargetDesc/HexagonMCShuffler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>

using namespace llvm;
using namespace Hexagon;

#define DEBUG_TYPE "hexagon-mccompound"

enum OpcodeIndex {
  fp0_jump_nt = 0,
  fp0_jump_t,
  fp1_jump_nt,
  fp1_jump_t,
  tp0_jump_nt,
  tp0_jump_t,
  tp1_jump_nt,
  tp1_jump_t
};

static const unsigned tstBitOpcode[8] = {
    J4_tstbit0_fp0_jump_nt, J4_tstbit0_fp0_jump_t,  J4_tstbit0_fp1_jump_nt,
    J4_tstbit0_fp1_jump_t,  J4_tstbit0_tp0_jump_nt, J4_tstbit0_tp0_jump_t,
    J4_tstbit0_tp1_jump_nt, J4_tstbit0_tp1_jump_t};
static const unsigned cmpeqBitOpcode[8] = {
    J4_cmpeq_fp0_jump_nt, J4_cmpeq_fp0_jump_t,  J4_cmpeq_fp1_jump_nt,
    J4_cmpeq_fp1_jump_t,  J4_cmpeq_tp0_jump_nt, J4_cmpeq_tp0_jump_t,
    J4_cmpeq_tp1_jump_nt, J4_cmpeq_tp1_jump_t};
static const unsigned cmpgtBitOpcode[8] = {
    J4_cmpgt_fp0_jump_nt, J4_cmpgt_fp0_jump_t,  J4_cmpgt_fp1_jump_nt,
    J4_cmpgt_fp1_jump_t,  J4_cmpgt_tp0_jump_nt, J4_cmpgt_tp0_jump_t,
    J4_cmpgt_tp1_jump_nt, J4_cmpgt_tp1_jump_t};
static const unsigned cmpgtuBitOpcode[8] = {
    J4_cmpgtu_fp0_jump_nt, J4_cmpgtu_fp0_jump_t,  J4_cmpgtu_fp1_jump_nt,
    J4_cmpgtu_fp1_jump_t,  J4_cmpgtu_tp0_jump_nt, J4_cmpgtu_tp0_jump_t,
    J4_cmpgtu_tp1_jump_nt, J4_cmpgtu_tp1_jump_t};
static const unsigned cmpeqiBitOpcode[8] = {
    J4_cmpeqi_fp0_jump_nt, J4_cmpeqi_fp0_jump_t,  J4_cmpeqi_fp1_jump_nt,
    J4_cmpeqi_fp1_jump_t,  J4_cmpeqi_tp0_jump_nt, J4_cmpeqi_tp0_jump_t,
    J4_cmpeqi_tp1_jump_nt, J4_cmpeqi_tp1_jump_t};
static const unsigned cmpgtiBitOpcode[8] = {
    J4_cmpgti_fp0_jump_nt, J4_cmpgti_fp0_jump_t,  J4_cmpgti_fp1_jump_nt,
    J4_cmpgti_fp1_jump_t,  J4_cmpgti_tp0_jump_nt, J4_cmpgti_tp0_jump_t,
    J4_cmpgti_tp1_jump_nt, J4_cmpgti_tp1_jump_t};
static const unsigned cmpgtuiBitOpcode[8] = {
    J4_cmpgtui_fp0_jump_nt, J4_cmpgtui_fp0_jump_t,  J4_cmpgtui_fp1_jump_nt,
    J4_cmpgtui_fp1_jump_t,  J4_cmpgtui_tp0_jump_nt, J4_cmpgtui_tp0_jump_t,
    J4_cmpgtui_tp1_jump_nt, J4_cmpgtui_tp1_jump_t};
static const unsigned cmpeqn1BitOpcode[8] = {
    J4_cmpeqn1_fp0_jump_nt, J4_cmpeqn1_fp0_jump_t,  J4_cmpeqn1_fp1_jump_nt,
    J4_cmpeqn1_fp1_jump_t,  J4_cmpeqn1_tp0_jump_nt, J4_cmpeqn1_tp0_jump_t,
    J4_cmpeqn1_tp1_jump_nt, J4_cmpeqn1_tp1_jump_t};
static const unsigned cmpgtn1BitOpcode[8] = {
    J4_cmpgtn1_fp0_jump_nt, J4_cmpgtn1_fp0_jump_t,  J4_cmpgtn1_fp1_jump_nt,
    J4_cmpgtn1_fp1_jump_t,  J4_cmpgtn1_tp0_jump_nt, J4_cmpgtn1_tp0_jump_t,
    J4_cmpgtn1_tp1_jump_nt, J4_cmpgtn1_tp1_jump_t,
};

// enum HexagonII::CompoundGroup
static unsigned getCompoundCandidateGroup(MCInst const &MI, bool IsExtended) {
  unsigned DstReg, SrcReg, Src1Reg, Src2Reg;

  switch (MI.getOpcode()) {
  default:
    return HexagonII::HCG_None;
  //
  // Compound pairs.
  // "p0=cmp.eq(Rs16,Rt16); if (p0.new) jump:nt #r9:2"
  // "Rd16=#U6 ; jump #r9:2"
  // "Rd16=Rs16 ; jump #r9:2"
  //
  case Hexagon::C2_cmpeq:
  case Hexagon::C2_cmpgt:
  case Hexagon::C2_cmpgtu:
    if (IsExtended)
      return HexagonII::HCG_None;
    DstReg = MI.getOperand(0).getReg();
    Src1Reg = MI.getOperand(1).getReg();
    Src2Reg = MI.getOperand(2).getReg();
    if ((Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src2Reg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::C2_cmpeqi:
  case Hexagon::C2_cmpgti:
  case Hexagon::C2_cmpgtui:
    if (IsExtended)
      return HexagonII::HCG_None;
    // P0 = cmp.eq(Rs,#u2)
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();
    if ((Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg) &&
        (HexagonMCInstrInfo::inRange<5>(MI, 2) ||
         HexagonMCInstrInfo::minConstant(MI, 2) == -1))
      return HexagonII::HCG_A;
    break;
  case Hexagon::A2_tfr:
    if (IsExtended)
      return HexagonII::HCG_None;
    // Rd = Rs
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();
    if (HexagonMCInstrInfo::isIntRegForSubInst(DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(SrcReg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::A2_tfrsi:
    if (IsExtended)
      return HexagonII::HCG_None;
    // Rd = #u6
    DstReg = MI.getOperand(0).getReg();
    if (HexagonMCInstrInfo::minConstant(MI, 1) <= 63 &&
        HexagonMCInstrInfo::minConstant(MI, 1) >= 0 &&
        HexagonMCInstrInfo::isIntRegForSubInst(DstReg))
      return HexagonII::HCG_A;
    break;
  case Hexagon::S2_tstbit_i:
    if (IsExtended)
      return HexagonII::HCG_None;
    DstReg = MI.getOperand(0).getReg();
    Src1Reg = MI.getOperand(1).getReg();
    if ((Hexagon::P0 == DstReg || Hexagon::P1 == DstReg) &&
        HexagonMCInstrInfo::isIntRegForSubInst(Src1Reg) &&
        HexagonMCInstrInfo::minConstant(MI, 2) == 0)
      return HexagonII::HCG_A;
    break;
  // The fact that .new form is used pretty much guarantees
  // that predicate register will match. Nevertheless,
  // there could be some false positives without additional
  // checking.
  case Hexagon::J2_jumptnew:
  case Hexagon::J2_jumpfnew:
  case Hexagon::J2_jumptnewpt:
  case Hexagon::J2_jumpfnewpt:
    Src1Reg = MI.getOperand(0).getReg();
    if (Hexagon::P0 == Src1Reg || Hexagon::P1 == Src1Reg)
      return HexagonII::HCG_B;
    break;
  // Transfer and jump:
  // Rd=#U6 ; jump #r9:2
  // Rd=Rs ; jump #r9:2
  // Do not test for jump range here.
  case Hexagon::J2_jump:
  case Hexagon::RESTORE_DEALLOC_RET_JMP_V4:
    return HexagonII::HCG_C;
    break;
  }

  return HexagonII::HCG_None;
}

/// getCompoundOp - Return the index from 0-7 into the above opcode lists.
static unsigned getCompoundOp(MCInst const &HMCI) {
  const MCOperand &Predicate = HMCI.getOperand(0);
  unsigned PredReg = Predicate.getReg();

  assert((PredReg == Hexagon::P0) || (PredReg == Hexagon::P1) ||
         (PredReg == Hexagon::P2) || (PredReg == Hexagon::P3));

  switch (HMCI.getOpcode()) {
  default:
    llvm_unreachable("Expected match not found.\n");
    break;
  case Hexagon::J2_jumpfnew:
    return (PredReg == Hexagon::P0) ? fp0_jump_nt : fp1_jump_nt;
  case Hexagon::J2_jumpfnewpt:
    return (PredReg == Hexagon::P0) ? fp0_jump_t : fp1_jump_t;
  case Hexagon::J2_jumptnew:
    return (PredReg == Hexagon::P0) ? tp0_jump_nt : tp1_jump_nt;
  case Hexagon::J2_jumptnewpt:
    return (PredReg == Hexagon::P0) ? tp0_jump_t : tp1_jump_t;
  }
}

static MCInst *getCompoundInsn(MCContext &Context, MCInst const &L,
                               MCInst const &R) {
  MCInst *CompoundInsn = nullptr;
  unsigned compoundOpcode;
  MCOperand Rs, Rt;
  int64_t Value;
  bool Success;

  switch (L.getOpcode()) {
  default:
    LLVM_DEBUG(dbgs() << "Possible compound ignored\n");
    return CompoundInsn;

  case Hexagon::A2_tfrsi:
    Rt = L.getOperand(0);
    compoundOpcode = J4_jumpseti;
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);

    CompoundInsn->addOperand(Rt);
    CompoundInsn->addOperand(L.getOperand(1)); // Immediate
    CompoundInsn->addOperand(R.getOperand(0)); // Jump target
    break;

  case Hexagon::A2_tfr:
    Rt = L.getOperand(0);
    Rs = L.getOperand(1);

    compoundOpcode = J4_jumpsetr;
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rt);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(R.getOperand(0)); // Jump target.

    break;

  case Hexagon::C2_cmpeq:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpeq\n");
    Rs = L.getOperand(1);
    Rt = L.getOperand(2);

    compoundOpcode = cmpeqBitOpcode[getCompoundOp(R)];
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(Rt);
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::C2_cmpgt:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpgt\n");
    Rs = L.getOperand(1);
    Rt = L.getOperand(2);

    compoundOpcode = cmpgtBitOpcode[getCompoundOp(R)];
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(Rt);
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::C2_cmpgtu:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpgtu\n");
    Rs = L.getOperand(1);
    Rt = L.getOperand(2);

    compoundOpcode = cmpgtuBitOpcode[getCompoundOp(R)];
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(Rt);
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::C2_cmpeqi:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpeqi\n");
    Success = L.getOperand(2).getExpr()->evaluateAsAbsolute(Value);
    (void)Success;
    assert(Success);
    if (Value == -1)
      compoundOpcode = cmpeqn1BitOpcode[getCompoundOp(R)];
    else
      compoundOpcode = cmpeqiBitOpcode[getCompoundOp(R)];

    Rs = L.getOperand(1);
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(L.getOperand(2));
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::C2_cmpgti:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpgti\n");
    Success = L.getOperand(2).getExpr()->evaluateAsAbsolute(Value);
    (void)Success;
    assert(Success);
    if (Value == -1)
      compoundOpcode = cmpgtn1BitOpcode[getCompoundOp(R)];
    else
      compoundOpcode = cmpgtiBitOpcode[getCompoundOp(R)];

    Rs = L.getOperand(1);
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(L.getOperand(2));
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::C2_cmpgtui:
    LLVM_DEBUG(dbgs() << "CX: C2_cmpgtui\n");
    Rs = L.getOperand(1);
    compoundOpcode = cmpgtuiBitOpcode[getCompoundOp(R)];
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(L.getOperand(2));
    CompoundInsn->addOperand(R.getOperand(1));
    break;

  case Hexagon::S2_tstbit_i:
    LLVM_DEBUG(dbgs() << "CX: S2_tstbit_i\n");
    Rs = L.getOperand(1);
    compoundOpcode = tstBitOpcode[getCompoundOp(R)];
    CompoundInsn = Context.createMCInst();
    CompoundInsn->setOpcode(compoundOpcode);
    CompoundInsn->addOperand(Rs);
    CompoundInsn->addOperand(R.getOperand(1));
    break;
  }

  return CompoundInsn;
}

/// Non-Symmetrical. See if these two instructions are fit for compound pair.
static bool isOrderedCompoundPair(MCInst const &MIa, bool IsExtendedA,
                                  MCInst const &MIb, bool IsExtendedB) {
  unsigned MIaG = getCompoundCandidateGroup(MIa, IsExtendedA);
  unsigned MIbG = getCompoundCandidateGroup(MIb, IsExtendedB);
  // We have two candidates - check that this is the same register
  // we are talking about.
  unsigned Opca = MIa.getOpcode();
  if (MIaG == HexagonII::HCG_A && MIbG == HexagonII::HCG_C &&
      (Opca == Hexagon::A2_tfr || Opca == Hexagon::A2_tfrsi))
    return true;
  return ((MIaG == HexagonII::HCG_A && MIbG == HexagonII::HCG_B) &&
          (MIa.getOperand(0).getReg() == MIb.getOperand(0).getReg()));
}

static bool lookForCompound(MCInstrInfo const &MCII, MCContext &Context,
                            MCInst &MCI) {
  assert(HexagonMCInstrInfo::isBundle(MCI));
  bool JExtended = false;
  for (MCInst::iterator J =
           MCI.begin() + HexagonMCInstrInfo::bundleInstructionsOffset;
       J != MCI.end(); ++J) {
    MCInst const *JumpInst = J->getInst();
    if (HexagonMCInstrInfo::isImmext(*JumpInst)) {
      JExtended = true;
      continue;
    }
    if (HexagonMCInstrInfo::getType(MCII, *JumpInst) == HexagonII::TypeJ) {
      // Try to pair with another insn (B)undled with jump.
      bool BExtended = false;
      for (MCInst::iterator B =
               MCI.begin() + HexagonMCInstrInfo::bundleInstructionsOffset;
           B != MCI.end(); ++B) {
        MCInst const *Inst = B->getInst();
        if (JumpInst == Inst) {
          BExtended = false;
          continue;
        }
        if (HexagonMCInstrInfo::isImmext(*Inst)) {
          BExtended = true;
          continue;
        }
        LLVM_DEBUG(dbgs() << "J,B: " << JumpInst->getOpcode() << ","
                          << Inst->getOpcode() << "\n");
        if (isOrderedCompoundPair(*Inst, BExtended, *JumpInst, JExtended)) {
          MCInst *CompoundInsn = getCompoundInsn(Context, *Inst, *JumpInst);
          if (CompoundInsn) {
            LLVM_DEBUG(dbgs() << "B: " << Inst->getOpcode() << ","
                              << JumpInst->getOpcode() << " Compounds to "
                              << CompoundInsn->getOpcode() << "\n");
            J->setInst(CompoundInsn);
            MCI.erase(B);
            return true;
          }
        }
        BExtended = false;
      }
    }
    JExtended = false;
  }
  return false;
}

/// tryCompound - Given a bundle check for compound insns when one
/// is found update the contents fo the bundle with the compound insn.
/// If a compound instruction is found then the bundle will have one
/// additional slot.
void HexagonMCInstrInfo::tryCompound(MCInstrInfo const &MCII, MCSubtargetInfo const &STI,
                                     MCContext &Context, MCInst &MCI) {
  assert(HexagonMCInstrInfo::isBundle(MCI) &&
         "Non-Bundle where Bundle expected");

  // By definition a compound must have 2 insn.
  if (MCI.size() < 2)
    return;

  // Create a vector, needed to keep the order of jump instructions.
  MCInst CheckList(MCI);

  // Keep the last known good bundle around in case the shuffle fails.
  MCInst LastValidBundle(MCI);

  bool PreviouslyValid = llvm::HexagonMCShuffle(Context, false, MCII, STI, MCI);

  // Look for compounds until none are found, only update the bundle when
  // a compound is found.
  while (lookForCompound(MCII, Context, CheckList)) {
    // Need to update the bundle.
    MCI = CheckList;

    const bool IsValid = llvm::HexagonMCShuffle(Context, false, MCII, STI, MCI);
    if (PreviouslyValid && !IsValid) {
      LLVM_DEBUG(dbgs() << "Found ERROR\n");
      MCI = LastValidBundle;
    } else if (IsValid) {
      LastValidBundle = MCI;
      PreviouslyValid = true;
    }
  }
}
