//=== lib/CodeGen/GlobalISel/AArch64PreLegalizerCombiner.cpp --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass does combining of machine instructions at the generic MI level,
// before the legalizer.
//
//===----------------------------------------------------------------------===//

#include "AArch64GlobalISelUtils.h"
#include "AArch64TargetMachine.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/CombinerInfo.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Debug.h"

#define GET_GICOMBINER_DEPS
#include "AArch64GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_DEPS

#define DEBUG_TYPE "aarch64-prelegalizer-combiner"

using namespace llvm;
using namespace MIPatternMatch;

namespace {

#define GET_GICOMBINER_TYPES
#include "AArch64GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_TYPES

/// Return true if a G_FCONSTANT instruction is known to be better-represented
/// as a G_CONSTANT.
bool matchFConstantToConstant(MachineInstr &MI, MachineRegisterInfo &MRI) {
  assert(MI.getOpcode() == TargetOpcode::G_FCONSTANT);
  Register DstReg = MI.getOperand(0).getReg();
  const unsigned DstSize = MRI.getType(DstReg).getSizeInBits();
  if (DstSize != 32 && DstSize != 64)
    return false;

  // When we're storing a value, it doesn't matter what register bank it's on.
  // Since not all floating point constants can be materialized using a fmov,
  // it makes more sense to just use a GPR.
  return all_of(MRI.use_nodbg_instructions(DstReg),
                [](const MachineInstr &Use) { return Use.mayStore(); });
}

/// Change a G_FCONSTANT into a G_CONSTANT.
void applyFConstantToConstant(MachineInstr &MI) {
  assert(MI.getOpcode() == TargetOpcode::G_FCONSTANT);
  MachineIRBuilder MIB(MI);
  const APFloat &ImmValAPF = MI.getOperand(1).getFPImm()->getValueAPF();
  MIB.buildConstant(MI.getOperand(0).getReg(), ImmValAPF.bitcastToAPInt());
  MI.eraseFromParent();
}

/// Try to match a G_ICMP of a G_TRUNC with zero, in which the truncated bits
/// are sign bits. In this case, we can transform the G_ICMP to directly compare
/// the wide value with a zero.
bool matchICmpRedundantTrunc(MachineInstr &MI, MachineRegisterInfo &MRI,
                             GISelKnownBits *KB, Register &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_ICMP && KB);

  auto Pred = (CmpInst::Predicate)MI.getOperand(1).getPredicate();
  if (!ICmpInst::isEquality(Pred))
    return false;

  Register LHS = MI.getOperand(2).getReg();
  LLT LHSTy = MRI.getType(LHS);
  if (!LHSTy.isScalar())
    return false;

  Register RHS = MI.getOperand(3).getReg();
  Register WideReg;

  if (!mi_match(LHS, MRI, m_GTrunc(m_Reg(WideReg))) ||
      !mi_match(RHS, MRI, m_SpecificICst(0)))
    return false;

  LLT WideTy = MRI.getType(WideReg);
  if (KB->computeNumSignBits(WideReg) <=
      WideTy.getSizeInBits() - LHSTy.getSizeInBits())
    return false;

  MatchInfo = WideReg;
  return true;
}

void applyICmpRedundantTrunc(MachineInstr &MI, MachineRegisterInfo &MRI,
                             MachineIRBuilder &Builder,
                             GISelChangeObserver &Observer, Register &WideReg) {
  assert(MI.getOpcode() == TargetOpcode::G_ICMP);

  LLT WideTy = MRI.getType(WideReg);
  // We're going to directly use the wide register as the LHS, and then use an
  // equivalent size zero for RHS.
  Builder.setInstrAndDebugLoc(MI);
  auto WideZero = Builder.buildConstant(WideTy, 0);
  Observer.changingInstr(MI);
  MI.getOperand(2).setReg(WideReg);
  MI.getOperand(3).setReg(WideZero.getReg(0));
  Observer.changedInstr(MI);
}

/// \returns true if it is possible to fold a constant into a G_GLOBAL_VALUE.
///
/// e.g.
///
/// %g = G_GLOBAL_VALUE @x -> %g = G_GLOBAL_VALUE @x + cst
bool matchFoldGlobalOffset(MachineInstr &MI, MachineRegisterInfo &MRI,
                           std::pair<uint64_t, uint64_t> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_GLOBAL_VALUE);
  MachineFunction &MF = *MI.getMF();
  auto &GlobalOp = MI.getOperand(1);
  auto *GV = GlobalOp.getGlobal();
  if (GV->isThreadLocal())
    return false;

  // Don't allow anything that could represent offsets etc.
  if (MF.getSubtarget<AArch64Subtarget>().ClassifyGlobalReference(
          GV, MF.getTarget()) != AArch64II::MO_NO_FLAG)
    return false;

  // Look for a G_GLOBAL_VALUE only used by G_PTR_ADDs against constants:
  //
  //  %g = G_GLOBAL_VALUE @x
  //  %ptr1 = G_PTR_ADD %g, cst1
  //  %ptr2 = G_PTR_ADD %g, cst2
  //  ...
  //  %ptrN = G_PTR_ADD %g, cstN
  //
  // Identify the *smallest* constant. We want to be able to form this:
  //
  //  %offset_g = G_GLOBAL_VALUE @x + min_cst
  //  %g = G_PTR_ADD %offset_g, -min_cst
  //  %ptr1 = G_PTR_ADD %g, cst1
  //  ...
  Register Dst = MI.getOperand(0).getReg();
  uint64_t MinOffset = -1ull;
  for (auto &UseInstr : MRI.use_nodbg_instructions(Dst)) {
    if (UseInstr.getOpcode() != TargetOpcode::G_PTR_ADD)
      return false;
    auto Cst = getIConstantVRegValWithLookThrough(
        UseInstr.getOperand(2).getReg(), MRI);
    if (!Cst)
      return false;
    MinOffset = std::min(MinOffset, Cst->Value.getZExtValue());
  }

  // Require that the new offset is larger than the existing one to avoid
  // infinite loops.
  uint64_t CurrOffset = GlobalOp.getOffset();
  uint64_t NewOffset = MinOffset + CurrOffset;
  if (NewOffset <= CurrOffset)
    return false;

  // Check whether folding this offset is legal. It must not go out of bounds of
  // the referenced object to avoid violating the code model, and must be
  // smaller than 2^20 because this is the largest offset expressible in all
  // object formats. (The IMAGE_REL_ARM64_PAGEBASE_REL21 relocation in COFF
  // stores an immediate signed 21 bit offset.)
  //
  // This check also prevents us from folding negative offsets, which will end
  // up being treated in the same way as large positive ones. They could also
  // cause code model violations, and aren't really common enough to matter.
  if (NewOffset >= (1 << 20))
    return false;

  Type *T = GV->getValueType();
  if (!T->isSized() ||
      NewOffset > GV->getDataLayout().getTypeAllocSize(T))
    return false;
  MatchInfo = std::make_pair(NewOffset, MinOffset);
  return true;
}

void applyFoldGlobalOffset(MachineInstr &MI, MachineRegisterInfo &MRI,
                           MachineIRBuilder &B, GISelChangeObserver &Observer,
                           std::pair<uint64_t, uint64_t> &MatchInfo) {
  // Change:
  //
  //  %g = G_GLOBAL_VALUE @x
  //  %ptr1 = G_PTR_ADD %g, cst1
  //  %ptr2 = G_PTR_ADD %g, cst2
  //  ...
  //  %ptrN = G_PTR_ADD %g, cstN
  //
  // To:
  //
  //  %offset_g = G_GLOBAL_VALUE @x + min_cst
  //  %g = G_PTR_ADD %offset_g, -min_cst
  //  %ptr1 = G_PTR_ADD %g, cst1
  //  ...
  //  %ptrN = G_PTR_ADD %g, cstN
  //
  // Then, the original G_PTR_ADDs should be folded later on so that they look
  // like this:
  //
  //  %ptrN = G_PTR_ADD %offset_g, cstN - min_cst
  uint64_t Offset, MinOffset;
  std::tie(Offset, MinOffset) = MatchInfo;
  B.setInstrAndDebugLoc(*std::next(MI.getIterator()));
  Observer.changingInstr(MI);
  auto &GlobalOp = MI.getOperand(1);
  auto *GV = GlobalOp.getGlobal();
  GlobalOp.ChangeToGA(GV, Offset, GlobalOp.getTargetFlags());
  Register Dst = MI.getOperand(0).getReg();
  Register NewGVDst = MRI.cloneVirtualRegister(Dst);
  MI.getOperand(0).setReg(NewGVDst);
  Observer.changedInstr(MI);
  B.buildPtrAdd(
      Dst, NewGVDst,
      B.buildConstant(LLT::scalar(64), -static_cast<int64_t>(MinOffset)));
}

// Combines vecreduce_add(mul(ext(x), ext(y))) -> vecreduce_add(udot(x, y))
// Or vecreduce_add(ext(x)) -> vecreduce_add(udot(x, 1))
// Similar to performVecReduceAddCombine in SelectionDAG
bool matchExtAddvToUdotAddv(MachineInstr &MI, MachineRegisterInfo &MRI,
                            const AArch64Subtarget &STI,
                            std::tuple<Register, Register, bool> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_VECREDUCE_ADD &&
         "Expected a G_VECREDUCE_ADD instruction");
  assert(STI.hasDotProd() && "Target should have Dot Product feature");

  MachineInstr *I1 = getDefIgnoringCopies(MI.getOperand(1).getReg(), MRI);
  Register DstReg = MI.getOperand(0).getReg();
  Register MidReg = I1->getOperand(0).getReg();
  LLT DstTy = MRI.getType(DstReg);
  LLT MidTy = MRI.getType(MidReg);
  if (DstTy.getScalarSizeInBits() != 32 || MidTy.getScalarSizeInBits() != 32)
    return false;

  LLT SrcTy;
  auto I1Opc = I1->getOpcode();
  if (I1Opc == TargetOpcode::G_MUL) {
    // If result of this has more than 1 use, then there is no point in creating
    // udot instruction
    if (!MRI.hasOneNonDBGUse(MidReg))
      return false;

    MachineInstr *ExtMI1 =
        getDefIgnoringCopies(I1->getOperand(1).getReg(), MRI);
    MachineInstr *ExtMI2 =
        getDefIgnoringCopies(I1->getOperand(2).getReg(), MRI);
    LLT Ext1DstTy = MRI.getType(ExtMI1->getOperand(0).getReg());
    LLT Ext2DstTy = MRI.getType(ExtMI2->getOperand(0).getReg());

    if (ExtMI1->getOpcode() != ExtMI2->getOpcode() || Ext1DstTy != Ext2DstTy)
      return false;
    I1Opc = ExtMI1->getOpcode();
    SrcTy = MRI.getType(ExtMI1->getOperand(1).getReg());
    std::get<0>(MatchInfo) = ExtMI1->getOperand(1).getReg();
    std::get<1>(MatchInfo) = ExtMI2->getOperand(1).getReg();
  } else {
    SrcTy = MRI.getType(I1->getOperand(1).getReg());
    std::get<0>(MatchInfo) = I1->getOperand(1).getReg();
    std::get<1>(MatchInfo) = 0;
  }

  if (I1Opc == TargetOpcode::G_ZEXT)
    std::get<2>(MatchInfo) = 0;
  else if (I1Opc == TargetOpcode::G_SEXT)
    std::get<2>(MatchInfo) = 1;
  else
    return false;

  if (SrcTy.getScalarSizeInBits() != 8 || SrcTy.getNumElements() % 8 != 0)
    return false;

  return true;
}

void applyExtAddvToUdotAddv(MachineInstr &MI, MachineRegisterInfo &MRI,
                            MachineIRBuilder &Builder,
                            GISelChangeObserver &Observer,
                            const AArch64Subtarget &STI,
                            std::tuple<Register, Register, bool> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_VECREDUCE_ADD &&
         "Expected a G_VECREDUCE_ADD instruction");
  assert(STI.hasDotProd() && "Target should have Dot Product feature");

  // Initialise the variables
  unsigned DotOpcode =
      std::get<2>(MatchInfo) ? AArch64::G_SDOT : AArch64::G_UDOT;
  Register Ext1SrcReg = std::get<0>(MatchInfo);

  // If there is one source register, create a vector of 0s as the second
  // source register
  Register Ext2SrcReg;
  if (std::get<1>(MatchInfo) == 0)
    Ext2SrcReg = Builder.buildConstant(MRI.getType(Ext1SrcReg), 1)
                     ->getOperand(0)
                     .getReg();
  else
    Ext2SrcReg = std::get<1>(MatchInfo);

  // Find out how many DOT instructions are needed
  LLT SrcTy = MRI.getType(Ext1SrcReg);
  LLT MidTy;
  unsigned NumOfDotMI;
  if (SrcTy.getNumElements() % 16 == 0) {
    NumOfDotMI = SrcTy.getNumElements() / 16;
    MidTy = LLT::fixed_vector(4, 32);
  } else if (SrcTy.getNumElements() % 8 == 0) {
    NumOfDotMI = SrcTy.getNumElements() / 8;
    MidTy = LLT::fixed_vector(2, 32);
  } else {
    llvm_unreachable("Source type number of elements is not multiple of 8");
  }

  // Handle case where one DOT instruction is needed
  if (NumOfDotMI == 1) {
    auto Zeroes = Builder.buildConstant(MidTy, 0)->getOperand(0).getReg();
    auto Dot = Builder.buildInstr(DotOpcode, {MidTy},
                                  {Zeroes, Ext1SrcReg, Ext2SrcReg});
    Builder.buildVecReduceAdd(MI.getOperand(0), Dot->getOperand(0));
  } else {
    // If not pad the last v8 element with 0s to a v16
    SmallVector<Register, 4> Ext1UnmergeReg;
    SmallVector<Register, 4> Ext2UnmergeReg;
    if (SrcTy.getNumElements() % 16 != 0) {
      SmallVector<Register> Leftover1;
      SmallVector<Register> Leftover2;

      // Split the elements into v16i8 and v8i8
      LLT MainTy = LLT::fixed_vector(16, 8);
      LLT LeftoverTy1, LeftoverTy2;
      if ((!extractParts(Ext1SrcReg, MRI.getType(Ext1SrcReg), MainTy,
                         LeftoverTy1, Ext1UnmergeReg, Leftover1, Builder,
                         MRI)) ||
          (!extractParts(Ext2SrcReg, MRI.getType(Ext2SrcReg), MainTy,
                         LeftoverTy2, Ext2UnmergeReg, Leftover2, Builder,
                         MRI))) {
        llvm_unreachable("Unable to split this vector properly");
      }

      // Pad the leftover v8i8 vector with register of 0s of type v8i8
      Register v8Zeroes = Builder.buildConstant(LLT::fixed_vector(8, 8), 0)
                              ->getOperand(0)
                              .getReg();

      Ext1UnmergeReg.push_back(
          Builder
              .buildMergeLikeInstr(LLT::fixed_vector(16, 8),
                                   {Leftover1[0], v8Zeroes})
              .getReg(0));
      Ext2UnmergeReg.push_back(
          Builder
              .buildMergeLikeInstr(LLT::fixed_vector(16, 8),
                                   {Leftover2[0], v8Zeroes})
              .getReg(0));

    } else {
      // Unmerge the source vectors to v16i8
      unsigned SrcNumElts = SrcTy.getNumElements();
      extractParts(Ext1SrcReg, LLT::fixed_vector(16, 8), SrcNumElts / 16,
                   Ext1UnmergeReg, Builder, MRI);
      extractParts(Ext2SrcReg, LLT::fixed_vector(16, 8), SrcNumElts / 16,
                   Ext2UnmergeReg, Builder, MRI);
    }

    // Build the UDOT instructions
    SmallVector<Register, 2> DotReg;
    unsigned NumElements = 0;
    for (unsigned i = 0; i < Ext1UnmergeReg.size(); i++) {
      LLT ZeroesLLT;
      // Check if it is 16 or 8 elements. Set Zeroes to the according size
      if (MRI.getType(Ext1UnmergeReg[i]).getNumElements() == 16) {
        ZeroesLLT = LLT::fixed_vector(4, 32);
        NumElements += 4;
      } else {
        ZeroesLLT = LLT::fixed_vector(2, 32);
        NumElements += 2;
      }
      auto Zeroes = Builder.buildConstant(ZeroesLLT, 0)->getOperand(0).getReg();
      DotReg.push_back(
          Builder
              .buildInstr(DotOpcode, {MRI.getType(Zeroes)},
                          {Zeroes, Ext1UnmergeReg[i], Ext2UnmergeReg[i]})
              .getReg(0));
    }

    // Merge the output
    auto ConcatMI =
        Builder.buildConcatVectors(LLT::fixed_vector(NumElements, 32), DotReg);

    // Put it through a vector reduction
    Builder.buildVecReduceAdd(MI.getOperand(0).getReg(),
                              ConcatMI->getOperand(0).getReg());
  }

  // Erase the dead instructions
  MI.eraseFromParent();
}

// Matches {U/S}ADDV(ext(x)) => {U/S}ADDLV(x)
// Ensure that the type coming from the extend instruction is the right size
bool matchExtUaddvToUaddlv(MachineInstr &MI, MachineRegisterInfo &MRI,
                           std::pair<Register, bool> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_VECREDUCE_ADD &&
         "Expected G_VECREDUCE_ADD Opcode");

  // Check if the last instruction is an extend
  MachineInstr *ExtMI = getDefIgnoringCopies(MI.getOperand(1).getReg(), MRI);
  auto ExtOpc = ExtMI->getOpcode();

  if (ExtOpc == TargetOpcode::G_ZEXT)
    std::get<1>(MatchInfo) = 0;
  else if (ExtOpc == TargetOpcode::G_SEXT)
    std::get<1>(MatchInfo) = 1;
  else
    return false;

  // Check if the source register is a valid type
  Register ExtSrcReg = ExtMI->getOperand(1).getReg();
  LLT ExtSrcTy = MRI.getType(ExtSrcReg);
  LLT DstTy = MRI.getType(MI.getOperand(0).getReg());
  if ((DstTy.getScalarSizeInBits() == 16 &&
       ExtSrcTy.getNumElements() % 8 == 0 && ExtSrcTy.getNumElements() < 256) ||
      (DstTy.getScalarSizeInBits() == 32 &&
       ExtSrcTy.getNumElements() % 4 == 0) ||
      (DstTy.getScalarSizeInBits() == 64 &&
       ExtSrcTy.getNumElements() % 4 == 0)) {
    std::get<0>(MatchInfo) = ExtSrcReg;
    return true;
  }
  return false;
}

void applyExtUaddvToUaddlv(MachineInstr &MI, MachineRegisterInfo &MRI,
                           MachineIRBuilder &B, GISelChangeObserver &Observer,
                           std::pair<Register, bool> &MatchInfo) {
  assert(MI.getOpcode() == TargetOpcode::G_VECREDUCE_ADD &&
         "Expected G_VECREDUCE_ADD Opcode");

  unsigned Opc = std::get<1>(MatchInfo) ? AArch64::G_SADDLV : AArch64::G_UADDLV;
  Register SrcReg = std::get<0>(MatchInfo);
  Register DstReg = MI.getOperand(0).getReg();
  LLT SrcTy = MRI.getType(SrcReg);
  LLT DstTy = MRI.getType(DstReg);

  // If SrcTy has more elements than expected, split them into multiple
  // insructions and sum the results
  LLT MainTy;
  SmallVector<Register, 1> WorkingRegisters;
  unsigned SrcScalSize = SrcTy.getScalarSizeInBits();
  unsigned SrcNumElem = SrcTy.getNumElements();
  if ((SrcScalSize == 8 && SrcNumElem > 16) ||
      (SrcScalSize == 16 && SrcNumElem > 8) ||
      (SrcScalSize == 32 && SrcNumElem > 4)) {

    LLT LeftoverTy;
    SmallVector<Register, 4> LeftoverRegs;
    if (SrcScalSize == 8)
      MainTy = LLT::fixed_vector(16, 8);
    else if (SrcScalSize == 16)
      MainTy = LLT::fixed_vector(8, 16);
    else if (SrcScalSize == 32)
      MainTy = LLT::fixed_vector(4, 32);
    else
      llvm_unreachable("Source's Scalar Size not supported");

    // Extract the parts and put each extracted sources through U/SADDLV and put
    // the values inside a small vec
    extractParts(SrcReg, SrcTy, MainTy, LeftoverTy, WorkingRegisters,
                 LeftoverRegs, B, MRI);
    for (unsigned I = 0; I < LeftoverRegs.size(); I++) {
      WorkingRegisters.push_back(LeftoverRegs[I]);
    }
  } else {
    WorkingRegisters.push_back(SrcReg);
    MainTy = SrcTy;
  }

  unsigned MidScalarSize = MainTy.getScalarSizeInBits() * 2;
  LLT MidScalarLLT = LLT::scalar(MidScalarSize);
  Register zeroReg = B.buildConstant(LLT::scalar(64), 0).getReg(0);
  for (unsigned I = 0; I < WorkingRegisters.size(); I++) {
    // If the number of elements is too small to build an instruction, extend
    // its size before applying addlv
    LLT WorkingRegTy = MRI.getType(WorkingRegisters[I]);
    if ((WorkingRegTy.getScalarSizeInBits() == 8) &&
        (WorkingRegTy.getNumElements() == 4)) {
      WorkingRegisters[I] =
          B.buildInstr(std::get<1>(MatchInfo) ? TargetOpcode::G_SEXT
                                              : TargetOpcode::G_ZEXT,
                       {LLT::fixed_vector(4, 16)}, {WorkingRegisters[I]})
              .getReg(0);
    }

    // Generate the {U/S}ADDLV instruction, whose output is always double of the
    // Src's Scalar size
    LLT addlvTy = MidScalarSize <= 32 ? LLT::fixed_vector(4, 32)
                                      : LLT::fixed_vector(2, 64);
    Register addlvReg =
        B.buildInstr(Opc, {addlvTy}, {WorkingRegisters[I]}).getReg(0);

    // The output from {U/S}ADDLV gets placed in the lowest lane of a v4i32 or
    // v2i64 register.
    //     i16, i32 results uses v4i32 registers
    //     i64      results uses v2i64 registers
    // Therefore we have to extract/truncate the the value to the right type
    if (MidScalarSize == 32 || MidScalarSize == 64) {
      WorkingRegisters[I] = B.buildInstr(AArch64::G_EXTRACT_VECTOR_ELT,
                                         {MidScalarLLT}, {addlvReg, zeroReg})
                                .getReg(0);
    } else {
      Register extractReg = B.buildInstr(AArch64::G_EXTRACT_VECTOR_ELT,
                                         {LLT::scalar(32)}, {addlvReg, zeroReg})
                                .getReg(0);
      WorkingRegisters[I] =
          B.buildTrunc({MidScalarLLT}, {extractReg}).getReg(0);
    }
  }

  Register outReg;
  if (WorkingRegisters.size() > 1) {
    outReg = B.buildAdd(MidScalarLLT, WorkingRegisters[0], WorkingRegisters[1])
                 .getReg(0);
    for (unsigned I = 2; I < WorkingRegisters.size(); I++) {
      outReg = B.buildAdd(MidScalarLLT, outReg, WorkingRegisters[I]).getReg(0);
    }
  } else {
    outReg = WorkingRegisters[0];
  }

  if (DstTy.getScalarSizeInBits() > MidScalarSize) {
    // Handle the scalar value if the DstTy's Scalar Size is more than double
    // Src's ScalarType
    B.buildInstr(std::get<1>(MatchInfo) ? TargetOpcode::G_SEXT
                                        : TargetOpcode::G_ZEXT,
                 {DstReg}, {outReg});
  } else {
    B.buildCopy(DstReg, outReg);
  }

  MI.eraseFromParent();
}

// Pushes ADD/SUB through extend instructions to decrease the number of extend
// instruction at the end by allowing selection of {s|u}addl sooner

// i32 add(i32 ext i8, i32 ext i8) => i32 ext(i16 add(i16 ext i8, i16 ext i8))
bool matchPushAddSubExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        Register DstReg, Register SrcReg1, Register SrcReg2) {
  assert((MI.getOpcode() == TargetOpcode::G_ADD ||
          MI.getOpcode() == TargetOpcode::G_SUB) &&
         "Expected a G_ADD or G_SUB instruction\n");

  // Deal with vector types only
  LLT DstTy = MRI.getType(DstReg);
  if (!DstTy.isVector())
    return false;

  // Return true if G_{S|Z}EXT instruction is more than 2* source
  Register ExtDstReg = MI.getOperand(1).getReg();
  LLT Ext1SrcTy = MRI.getType(SrcReg1);
  LLT Ext2SrcTy = MRI.getType(SrcReg2);
  unsigned ExtDstScal = MRI.getType(ExtDstReg).getScalarSizeInBits();
  unsigned Ext1SrcScal = Ext1SrcTy.getScalarSizeInBits();
  if (((Ext1SrcScal == 8 && ExtDstScal == 32) ||
       ((Ext1SrcScal == 8 || Ext1SrcScal == 16) && ExtDstScal == 64)) &&
      Ext1SrcTy == Ext2SrcTy)
    return true;

  return false;
}

void applyPushAddSubExt(MachineInstr &MI, MachineRegisterInfo &MRI,
                        MachineIRBuilder &B, bool isSExt, Register DstReg,
                        Register SrcReg1, Register SrcReg2) {
  LLT SrcTy = MRI.getType(SrcReg1);
  LLT MidTy = SrcTy.changeElementSize(SrcTy.getScalarSizeInBits() * 2);
  unsigned Opc = isSExt ? TargetOpcode::G_SEXT : TargetOpcode::G_ZEXT;
  Register Ext1Reg = B.buildInstr(Opc, {MidTy}, {SrcReg1}).getReg(0);
  Register Ext2Reg = B.buildInstr(Opc, {MidTy}, {SrcReg2}).getReg(0);
  Register AddReg =
      B.buildInstr(MI.getOpcode(), {MidTy}, {Ext1Reg, Ext2Reg}).getReg(0);

  // G_SUB has to sign-extend the result.
  // G_ADD needs to sext from sext and can sext or zext from zext, so the
  // original opcode is used.
  if (MI.getOpcode() == TargetOpcode::G_ADD)
    B.buildInstr(Opc, {DstReg}, {AddReg});
  else
    B.buildSExt(DstReg, AddReg);

  MI.eraseFromParent();
}

bool tryToSimplifyUADDO(MachineInstr &MI, MachineIRBuilder &B,
                        CombinerHelper &Helper, GISelChangeObserver &Observer) {
  // Try simplify G_UADDO with 8 or 16 bit operands to wide G_ADD and TBNZ if
  // result is only used in the no-overflow case. It is restricted to cases
  // where we know that the high-bits of the operands are 0. If there's an
  // overflow, then the 9th or 17th bit must be set, which can be checked
  // using TBNZ.
  //
  // Change (for UADDOs on 8 and 16 bits):
  //
  //   %z0 = G_ASSERT_ZEXT _
  //   %op0 = G_TRUNC %z0
  //   %z1 = G_ASSERT_ZEXT _
  //   %op1 = G_TRUNC %z1
  //   %val, %cond = G_UADDO %op0, %op1
  //   G_BRCOND %cond, %error.bb
  //
  // error.bb:
  //   (no successors and no uses of %val)
  //
  // To:
  //
  //   %z0 = G_ASSERT_ZEXT _
  //   %z1 = G_ASSERT_ZEXT _
  //   %add = G_ADD %z0, %z1
  //   %val = G_TRUNC %add
  //   %bit = G_AND %add, 1 << scalar-size-in-bits(%op1)
  //   %cond = G_ICMP NE, %bit, 0
  //   G_BRCOND %cond, %error.bb

  auto &MRI = *B.getMRI();

  MachineOperand *DefOp0 = MRI.getOneDef(MI.getOperand(2).getReg());
  MachineOperand *DefOp1 = MRI.getOneDef(MI.getOperand(3).getReg());
  Register Op0Wide;
  Register Op1Wide;
  if (!mi_match(DefOp0->getParent(), MRI, m_GTrunc(m_Reg(Op0Wide))) ||
      !mi_match(DefOp1->getParent(), MRI, m_GTrunc(m_Reg(Op1Wide))))
    return false;
  LLT WideTy0 = MRI.getType(Op0Wide);
  LLT WideTy1 = MRI.getType(Op1Wide);
  Register ResVal = MI.getOperand(0).getReg();
  LLT OpTy = MRI.getType(ResVal);
  MachineInstr *Op0WideDef = MRI.getVRegDef(Op0Wide);
  MachineInstr *Op1WideDef = MRI.getVRegDef(Op1Wide);

  unsigned OpTySize = OpTy.getScalarSizeInBits();
  // First check that the G_TRUNC feeding the G_UADDO are no-ops, because the
  // inputs have been zero-extended.
  if (Op0WideDef->getOpcode() != TargetOpcode::G_ASSERT_ZEXT ||
      Op1WideDef->getOpcode() != TargetOpcode::G_ASSERT_ZEXT ||
      OpTySize != Op0WideDef->getOperand(2).getImm() ||
      OpTySize != Op1WideDef->getOperand(2).getImm())
    return false;

  // Only scalar UADDO with either 8 or 16 bit operands are handled.
  if (!WideTy0.isScalar() || !WideTy1.isScalar() || WideTy0 != WideTy1 ||
      OpTySize >= WideTy0.getScalarSizeInBits() ||
      (OpTySize != 8 && OpTySize != 16))
    return false;

  // The overflow-status result must be used by a branch only.
  Register ResStatus = MI.getOperand(1).getReg();
  if (!MRI.hasOneNonDBGUse(ResStatus))
    return false;
  MachineInstr *CondUser = &*MRI.use_instr_nodbg_begin(ResStatus);
  if (CondUser->getOpcode() != TargetOpcode::G_BRCOND)
    return false;

  // Make sure the computed result is only used in the no-overflow blocks.
  MachineBasicBlock *CurrentMBB = MI.getParent();
  MachineBasicBlock *FailMBB = CondUser->getOperand(1).getMBB();
  if (!FailMBB->succ_empty() || CondUser->getParent() != CurrentMBB)
    return false;
  if (any_of(MRI.use_nodbg_instructions(ResVal),
             [&MI, FailMBB, CurrentMBB](MachineInstr &I) {
               return &MI != &I &&
                      (I.getParent() == FailMBB || I.getParent() == CurrentMBB);
             }))
    return false;

  // Remove G_ADDO.
  B.setInstrAndDebugLoc(*MI.getNextNode());
  MI.eraseFromParent();

  // Emit wide add.
  Register AddDst = MRI.cloneVirtualRegister(Op0Wide);
  B.buildInstr(TargetOpcode::G_ADD, {AddDst}, {Op0Wide, Op1Wide});

  // Emit check of the 9th or 17th bit and update users (the branch). This will
  // later be folded to TBNZ.
  Register CondBit = MRI.cloneVirtualRegister(Op0Wide);
  B.buildAnd(
      CondBit, AddDst,
      B.buildConstant(LLT::scalar(32), OpTySize == 8 ? 1 << 8 : 1 << 16));
  B.buildICmp(CmpInst::ICMP_NE, ResStatus, CondBit,
              B.buildConstant(LLT::scalar(32), 0));

  // Update ZEXts users of the result value. Because all uses are in the
  // no-overflow case, we know that the top bits are 0 and we can ignore ZExts.
  B.buildZExtOrTrunc(ResVal, AddDst);
  for (MachineOperand &U : make_early_inc_range(MRI.use_operands(ResVal))) {
    Register WideReg;
    if (mi_match(U.getParent(), MRI, m_GZExt(m_Reg(WideReg)))) {
      auto OldR = U.getParent()->getOperand(0).getReg();
      Observer.erasingInstr(*U.getParent());
      U.getParent()->eraseFromParent();
      Helper.replaceRegWith(MRI, OldR, AddDst);
    }
  }

  return true;
}

class AArch64PreLegalizerCombinerImpl : public Combiner {
protected:
  // TODO: Make CombinerHelper methods const.
  mutable CombinerHelper Helper;
  const AArch64PreLegalizerCombinerImplRuleConfig &RuleConfig;
  const AArch64Subtarget &STI;

public:
  AArch64PreLegalizerCombinerImpl(
      MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
      GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
      const AArch64PreLegalizerCombinerImplRuleConfig &RuleConfig,
      const AArch64Subtarget &STI, MachineDominatorTree *MDT,
      const LegalizerInfo *LI);

  static const char *getName() { return "AArch6400PreLegalizerCombiner"; }

  bool tryCombineAll(MachineInstr &I) const override;

  bool tryCombineAllImpl(MachineInstr &I) const;

private:
#define GET_GICOMBINER_CLASS_MEMBERS
#include "AArch64GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CLASS_MEMBERS
};

#define GET_GICOMBINER_IMPL
#include "AArch64GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_IMPL

AArch64PreLegalizerCombinerImpl::AArch64PreLegalizerCombinerImpl(
    MachineFunction &MF, CombinerInfo &CInfo, const TargetPassConfig *TPC,
    GISelKnownBits &KB, GISelCSEInfo *CSEInfo,
    const AArch64PreLegalizerCombinerImplRuleConfig &RuleConfig,
    const AArch64Subtarget &STI, MachineDominatorTree *MDT,
    const LegalizerInfo *LI)
    : Combiner(MF, CInfo, TPC, &KB, CSEInfo),
      Helper(Observer, B, /*IsPreLegalize*/ true, &KB, MDT, LI),
      RuleConfig(RuleConfig), STI(STI),
#define GET_GICOMBINER_CONSTRUCTOR_INITS
#include "AArch64GenPreLegalizeGICombiner.inc"
#undef GET_GICOMBINER_CONSTRUCTOR_INITS
{
}

bool AArch64PreLegalizerCombinerImpl::tryCombineAll(MachineInstr &MI) const {
  if (tryCombineAllImpl(MI))
    return true;

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case TargetOpcode::G_SHUFFLE_VECTOR:
    return Helper.tryCombineShuffleVector(MI);
  case TargetOpcode::G_UADDO:
    return tryToSimplifyUADDO(MI, B, Helper, Observer);
  case TargetOpcode::G_MEMCPY_INLINE:
    return Helper.tryEmitMemcpyInline(MI);
  case TargetOpcode::G_MEMCPY:
  case TargetOpcode::G_MEMMOVE:
  case TargetOpcode::G_MEMSET: {
    // If we're at -O0 set a maxlen of 32 to inline, otherwise let the other
    // heuristics decide.
    unsigned MaxLen = CInfo.EnableOpt ? 0 : 32;
    // Try to inline memcpy type calls if optimizations are enabled.
    if (Helper.tryCombineMemCpyFamily(MI, MaxLen))
      return true;
    if (Opc == TargetOpcode::G_MEMSET)
      return llvm::AArch64GISelUtils::tryEmitBZero(MI, B, CInfo.EnableMinSize);
    return false;
  }
  }

  return false;
}

// Pass boilerplate
// ================

class AArch64PreLegalizerCombiner : public MachineFunctionPass {
public:
  static char ID;

  AArch64PreLegalizerCombiner();

  StringRef getPassName() const override {
    return "AArch64PreLegalizerCombiner";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

private:
  AArch64PreLegalizerCombinerImplRuleConfig RuleConfig;
};
} // end anonymous namespace

void AArch64PreLegalizerCombiner::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetPassConfig>();
  AU.setPreservesCFG();
  getSelectionDAGFallbackAnalysisUsage(AU);
  AU.addRequired<GISelKnownBitsAnalysis>();
  AU.addPreserved<GISelKnownBitsAnalysis>();
  AU.addRequired<MachineDominatorTreeWrapperPass>();
  AU.addPreserved<MachineDominatorTreeWrapperPass>();
  AU.addRequired<GISelCSEAnalysisWrapperPass>();
  AU.addPreserved<GISelCSEAnalysisWrapperPass>();
  MachineFunctionPass::getAnalysisUsage(AU);
}

AArch64PreLegalizerCombiner::AArch64PreLegalizerCombiner()
    : MachineFunctionPass(ID) {
  initializeAArch64PreLegalizerCombinerPass(*PassRegistry::getPassRegistry());

  if (!RuleConfig.parseCommandLineOption())
    report_fatal_error("Invalid rule identifier");
}

bool AArch64PreLegalizerCombiner::runOnMachineFunction(MachineFunction &MF) {
  if (MF.getProperties().hasProperty(
          MachineFunctionProperties::Property::FailedISel))
    return false;
  auto &TPC = getAnalysis<TargetPassConfig>();

  // Enable CSE.
  GISelCSEAnalysisWrapper &Wrapper =
      getAnalysis<GISelCSEAnalysisWrapperPass>().getCSEWrapper();
  auto *CSEInfo = &Wrapper.get(TPC.getCSEConfig());

  const AArch64Subtarget &ST = MF.getSubtarget<AArch64Subtarget>();
  const auto *LI = ST.getLegalizerInfo();

  const Function &F = MF.getFunction();
  bool EnableOpt =
      MF.getTarget().getOptLevel() != CodeGenOptLevel::None && !skipFunction(F);
  GISelKnownBits *KB = &getAnalysis<GISelKnownBitsAnalysis>().get(MF);
  MachineDominatorTree *MDT =
      &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  CombinerInfo CInfo(/*AllowIllegalOps*/ true, /*ShouldLegalizeIllegal*/ false,
                     /*LegalizerInfo*/ nullptr, EnableOpt, F.hasOptSize(),
                     F.hasMinSize());
  AArch64PreLegalizerCombinerImpl Impl(MF, CInfo, &TPC, *KB, CSEInfo,
                                       RuleConfig, ST, MDT, LI);
  return Impl.combineMachineInstrs();
}

char AArch64PreLegalizerCombiner::ID = 0;
INITIALIZE_PASS_BEGIN(AArch64PreLegalizerCombiner, DEBUG_TYPE,
                      "Combine AArch64 machine instrs before legalization",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(TargetPassConfig)
INITIALIZE_PASS_DEPENDENCY(GISelKnownBitsAnalysis)
INITIALIZE_PASS_DEPENDENCY(GISelCSEAnalysisWrapperPass)
INITIALIZE_PASS_END(AArch64PreLegalizerCombiner, DEBUG_TYPE,
                    "Combine AArch64 machine instrs before legalization", false,
                    false)

namespace llvm {
FunctionPass *createAArch64PreLegalizerCombiner() {
  return new AArch64PreLegalizerCombiner();
}
} // end namespace llvm
