//===- CombinerHelperVectorOps.cpp-----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements CombinerHelper for G_EXTRACT_VECTOR_ELT,
// G_INSERT_VECTOR_ELT, and G_VSCALE
//
//===----------------------------------------------------------------------===//
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/GlobalISel/Utils.h"
#include "llvm/CodeGen/LowLevelTypeUtils.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/Support/Casting.h"
#include <optional>

#define DEBUG_TYPE "gi-combiner"

using namespace llvm;
using namespace MIPatternMatch;

bool CombinerHelper::matchExtractVectorElement(MachineInstr &MI,
                                               BuildFnTy &MatchInfo) {
  GExtractVectorElement *Extract = cast<GExtractVectorElement>(&MI);

  Register Dst = Extract->getReg(0);
  Register Vector = Extract->getVectorReg();
  Register Index = Extract->getIndexReg();
  LLT DstTy = MRI.getType(Dst);
  LLT VectorTy = MRI.getType(Vector);

  // The vector register can be def'd by various ops that have vector as its
  // type. They can all be used for constant folding, scalarizing,
  // canonicalization, or combining based on symmetry.
  //
  // vector like ops
  // * build vector
  // * build vector trunc
  // * shuffle vector
  // * splat vector
  // * concat vectors
  // * insert/extract vector element
  // * insert/extract subvector
  // * vector loads
  // * scalable vector loads
  //
  // compute like ops
  // * binary ops
  // * unary ops
  //  * exts and truncs
  //  * casts
  //  * fneg
  // * select
  // * phis
  // * cmps
  // * freeze
  // * bitcast
  // * undef

  // We try to get the value of the Index register.
  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Index, MRI);
  std::optional<APInt> IndexC = std::nullopt;

  if (MaybeIndex)
    IndexC = MaybeIndex->Value;

  // Fold extractVectorElement(Vector, TOOLARGE) -> undef
  if (IndexC && VectorTy.isFixedVector() &&
      IndexC->uge(VectorTy.getNumElements()) &&
      isLegalOrBeforeLegalizer({TargetOpcode::G_IMPLICIT_DEF, {DstTy}})) {
    // For fixed-length vectors, it's invalid to extract out-of-range elements.
    MatchInfo = [=](MachineIRBuilder &B) { B.buildUndef(Dst); };
    return true;
  }

  return false;
}

bool CombinerHelper::matchExtractVectorElementWithDifferentIndices(
    const MachineOperand &MO, BuildFnTy &MatchInfo) {
  MachineInstr *Root = getDefIgnoringCopies(MO.getReg(), MRI);
  GExtractVectorElement *Extract = cast<GExtractVectorElement>(Root);

  //
  //  %idx1:_(s64) = G_CONSTANT i64 1
  //  %idx2:_(s64) = G_CONSTANT i64 2
  //  %insert:_(<2 x s32>) = G_INSERT_VECTOR_ELT_ELT %bv(<2 x s32>),
  //  %value(s32), %idx2(s64) %extract:_(s32) = G_EXTRACT_VECTOR_ELT %insert(<2
  //  x s32>), %idx1(s64)
  //
  //  -->
  //
  //  %insert:_(<2 x s32>) = G_INSERT_VECTOR_ELT_ELT %bv(<2 x s32>),
  //  %value(s32), %idx2(s64) %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x
  //  s32>), %idx1(s64)
  //
  //

  Register Index = Extract->getIndexReg();

  // We try to get the value of the Index register.
  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Index, MRI);
  std::optional<APInt> IndexC = std::nullopt;

  if (!MaybeIndex)
    return false;
  else
    IndexC = MaybeIndex->Value;

  Register Vector = Extract->getVectorReg();

  GInsertVectorElement *Insert =
      getOpcodeDef<GInsertVectorElement>(Vector, MRI);
  if (!Insert)
    return false;

  Register Dst = Extract->getReg(0);

  std::optional<ValueAndVReg> MaybeInsertIndex =
      getIConstantVRegValWithLookThrough(Insert->getIndexReg(), MRI);

  if (MaybeInsertIndex && MaybeInsertIndex->Value != *IndexC) {
    // There is no one-use check. We have to keep the insert. When both Index
    // registers are constants and not equal, we can look into the Vector
    // register of the insert.
    MatchInfo = [=](MachineIRBuilder &B) {
      B.buildExtractVectorElement(Dst, Insert->getVectorReg(), Index);
    };
    return true;
  }

  return false;
}

bool CombinerHelper::matchExtractVectorElementWithBuildVector(
    const MachineOperand &MO, BuildFnTy &MatchInfo) {
  MachineInstr *Root = getDefIgnoringCopies(MO.getReg(), MRI);
  GExtractVectorElement *Extract = cast<GExtractVectorElement>(Root);

  //
  //  %zero:_(s64) = G_CONSTANT i64 0
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR %arg1(s32), %arg2(s32)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %zero(s64)
  //
  //  -->
  //
  //  %extract:_(32) = COPY %arg1(s32)
  //
  //
  //
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR %arg1(s32), %arg2(s32)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %opaque(s64)
  //
  //  -->
  //
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR %arg1(s32), %arg2(s32)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %opaque(s64)
  //

  Register Vector = Extract->getVectorReg();

  // We expect a buildVector on the Vector register.
  GBuildVector *Build = getOpcodeDef<GBuildVector>(Vector, MRI);
  if (!Build)
    return false;

  LLT VectorTy = MRI.getType(Vector);

  // There is a one-use check. There are more combines on build vectors.
  EVT Ty(getMVTForLLT(VectorTy));
  if (!MRI.hasOneNonDBGUse(Build->getReg(0)) ||
      !getTargetLowering().aggressivelyPreferBuildVectorSources(Ty))
    return false;

  Register Index = Extract->getIndexReg();

  // If the Index is constant, then we can extract the element from the given
  // offset.
  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Index, MRI);
  if (!MaybeIndex)
    return false;

  // We now know that there is a buildVector def'd on the Vector register and
  // the index is const. The combine will succeed.

  Register Dst = Extract->getReg(0);

  MatchInfo = [=](MachineIRBuilder &B) {
    B.buildCopy(Dst, Build->getSourceReg(MaybeIndex->Value.getZExtValue()));
  };

  return true;
}

bool CombinerHelper::matchExtractVectorElementWithBuildVectorTrunc(
    const MachineOperand &MO, BuildFnTy &MatchInfo) {
  MachineInstr *Root = getDefIgnoringCopies(MO.getReg(), MRI);
  GExtractVectorElement *Extract = cast<GExtractVectorElement>(Root);

  //
  //  %zero:_(s64) = G_CONSTANT i64 0
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR_TRUNC %arg1(s64), %arg2(s64)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %zero(s64)
  //
  //  -->
  //
  //  %extract:_(32) = G_TRUNC %arg1(s64)
  //
  //
  //
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR_TRUNC %arg1(s64), %arg2(s64)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %opaque(s64)
  //
  //  -->
  //
  //  %bv:_(<2 x s32>) = G_BUILD_VECTOR_TRUNC %arg1(s64), %arg2(s64)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %bv(<2 x s32>), %opaque(s64)
  //

  Register Vector = Extract->getVectorReg();

  // We expect a buildVectorTrunc on the Vector register.
  GBuildVectorTrunc *Build = getOpcodeDef<GBuildVectorTrunc>(Vector, MRI);
  if (!Build)
    return false;

  LLT VectorTy = MRI.getType(Vector);

  // There is a one-use check. There are more combines on build vectors.
  EVT Ty(getMVTForLLT(VectorTy));
  if (!MRI.hasOneNonDBGUse(Build->getReg(0)) ||
      !getTargetLowering().aggressivelyPreferBuildVectorSources(Ty))
    return false;

  Register Index = Extract->getIndexReg();

  // If the Index is constant, then we can extract the element from the given
  // offset.
  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Index, MRI);
  if (!MaybeIndex)
    return false;

  // We now know that there is a buildVectorTrunc def'd on the Vector register
  // and the index is const. The combine will succeed.

  Register Dst = Extract->getReg(0);
  LLT DstTy = MRI.getType(Dst);
  LLT SrcTy = MRI.getType(Build->getSourceReg(0));

  // For buildVectorTrunc, the inputs are truncated.
  if (!isLegalOrBeforeLegalizer({TargetOpcode::G_TRUNC, {DstTy, SrcTy}}))
    return false;

  MatchInfo = [=](MachineIRBuilder &B) {
    B.buildTrunc(Dst, Build->getSourceReg(MaybeIndex->Value.getZExtValue()));
  };

  return true;
}

bool CombinerHelper::matchExtractVectorElementWithShuffleVector(
    const MachineOperand &MO, BuildFnTy &MatchInfo) {
  GExtractVectorElement *Extract =
      cast<GExtractVectorElement>(getDefIgnoringCopies(MO.getReg(), MRI));

  //
  //  %zero:_(s64) = G_CONSTANT i64 0
  //  %sv:_(<4 x s32>) = G_SHUFFLE_SHUFFLE %arg1(<4 x s32>), %arg2(<4 x s32>),
  //                     shufflemask(0, 0, 0, 0)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %sv(<4 x s32>), %zero(s64)
  //
  //  -->
  //
  //  %zero1:_(s64) = G_CONSTANT i64 0
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %arg1(<4 x s32>), %zero1(s64)
  //
  //
  //
  //
  //  %three:_(s64) = G_CONSTANT i64 3
  //  %sv:_(<4 x s32>) = G_SHUFFLE_SHUFFLE %arg1(<4 x s32>), %arg2(<4 x s32>),
  //                     shufflemask(0, 0, 0, -1)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %sv(<4 x s32>), %three(s64)
  //
  //  -->
  //
  //  %extract:_(s32) = G_IMPLICIT_DEF
  //
  //
  //
  //
  //
  //  %sv:_(<4 x s32>) = G_SHUFFLE_SHUFFLE %arg1(<4 x s32>), %arg2(<4 x s32>),
  //                     shufflemask(0, 0, 0, -1)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %sv(<4 x s32>), %opaque(s64)
  //
  //  -->
  //
  //  %sv:_(<4 x s32>) = G_SHUFFLE_SHUFFLE %arg1(<4 x s32>), %arg2(<4 x s32>),
  //                     shufflemask(0, 0, 0, -1)
  //  %extract:_(s32) = G_EXTRACT_VECTOR_ELT %sv(<4 x s32>), %opaque(s64)
  //

  // We try to get the value of the Index register.
  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Extract->getIndexReg(), MRI);
  if (!MaybeIndex)
    return false;

  GShuffleVector *Shuffle =
      cast<GShuffleVector>(getDefIgnoringCopies(Extract->getVectorReg(), MRI));

  ArrayRef<int> Mask = Shuffle->getMask();

  unsigned Offset = MaybeIndex->Value.getZExtValue();
  int SrcIdx = Mask[Offset];

  LLT Src1Type = MRI.getType(Shuffle->getSrc1Reg());
  // At the IR level a <1 x ty> shuffle  vector is valid, but we want to extract
  // from a vector.
  assert(Src1Type.isVector() && "expected to extract from a vector");
  unsigned LHSWidth = Src1Type.isVector() ? Src1Type.getNumElements() : 1;

  // Note that there is no one use check.
  Register Dst = Extract->getReg(0);
  LLT DstTy = MRI.getType(Dst);

  if (SrcIdx < 0 &&
      isLegalOrBeforeLegalizer({TargetOpcode::G_IMPLICIT_DEF, {DstTy}})) {
    MatchInfo = [=](MachineIRBuilder &B) { B.buildUndef(Dst); };
    return true;
  }

  // If the legality check failed, then we still have to abort.
  if (SrcIdx < 0)
    return false;

  Register NewVector;

  // We check in which vector and at what offset to look through.
  if (SrcIdx < (int)LHSWidth) {
    NewVector = Shuffle->getSrc1Reg();
    // SrcIdx unchanged
  } else { // SrcIdx >= LHSWidth
    NewVector = Shuffle->getSrc2Reg();
    SrcIdx -= LHSWidth;
  }

  LLT IdxTy = MRI.getType(Extract->getIndexReg());
  LLT NewVectorTy = MRI.getType(NewVector);

  // We check the legality of the look through.
  if (!isLegalOrBeforeLegalizer(
          {TargetOpcode::G_EXTRACT_VECTOR_ELT, {DstTy, NewVectorTy, IdxTy}}) ||
      !isConstantLegalOrBeforeLegalizer({IdxTy}))
    return false;

  // We look through the shuffle vector.
  MatchInfo = [=](MachineIRBuilder &B) {
    auto Idx = B.buildConstant(IdxTy, SrcIdx);
    B.buildExtractVectorElement(Dst, NewVector, Idx);
  };

  return true;
}

bool CombinerHelper::matchInsertVectorElementOOB(MachineInstr &MI,
                                                 BuildFnTy &MatchInfo) {
  GInsertVectorElement *Insert = cast<GInsertVectorElement>(&MI);

  Register Dst = Insert->getReg(0);
  LLT DstTy = MRI.getType(Dst);
  Register Index = Insert->getIndexReg();

  if (!DstTy.isFixedVector())
    return false;

  std::optional<ValueAndVReg> MaybeIndex =
      getIConstantVRegValWithLookThrough(Index, MRI);

  if (MaybeIndex && MaybeIndex->Value.uge(DstTy.getNumElements()) &&
      isLegalOrBeforeLegalizer({TargetOpcode::G_IMPLICIT_DEF, {DstTy}})) {
    MatchInfo = [=](MachineIRBuilder &B) { B.buildUndef(Dst); };
    return true;
  }

  return false;
}

bool CombinerHelper::matchAddOfVScale(const MachineOperand &MO,
                                      BuildFnTy &MatchInfo) {
  GAdd *Add = cast<GAdd>(MRI.getVRegDef(MO.getReg()));
  GVScale *LHSVScale = cast<GVScale>(MRI.getVRegDef(Add->getLHSReg()));
  GVScale *RHSVScale = cast<GVScale>(MRI.getVRegDef(Add->getRHSReg()));

  Register Dst = Add->getReg(0);

  if (!MRI.hasOneNonDBGUse(LHSVScale->getReg(0)) ||
      !MRI.hasOneNonDBGUse(RHSVScale->getReg(0)))
    return false;

  MatchInfo = [=](MachineIRBuilder &B) {
    B.buildVScale(Dst, LHSVScale->getSrc() + RHSVScale->getSrc());
  };

  return true;
}

bool CombinerHelper::matchMulOfVScale(const MachineOperand &MO,
                                      BuildFnTy &MatchInfo) {
  GMul *Mul = cast<GMul>(MRI.getVRegDef(MO.getReg()));
  GVScale *LHSVScale = cast<GVScale>(MRI.getVRegDef(Mul->getLHSReg()));

  std::optional<APInt> MaybeRHS = getIConstantVRegVal(Mul->getRHSReg(), MRI);
  if (!MaybeRHS)
    return false;

  Register Dst = MO.getReg();

  if (!MRI.hasOneNonDBGUse(LHSVScale->getReg(0)))
    return false;

  MatchInfo = [=](MachineIRBuilder &B) {
    B.buildVScale(Dst, LHSVScale->getSrc() * *MaybeRHS);
  };

  return true;
}

bool CombinerHelper::matchSubOfVScale(const MachineOperand &MO,
                                      BuildFnTy &MatchInfo) {
  GSub *Sub = cast<GSub>(MRI.getVRegDef(MO.getReg()));
  GVScale *RHSVScale = cast<GVScale>(MRI.getVRegDef(Sub->getRHSReg()));

  Register Dst = MO.getReg();
  LLT DstTy = MRI.getType(Dst);

  if (!MRI.hasOneNonDBGUse(RHSVScale->getReg(0)) ||
      !isLegalOrBeforeLegalizer({TargetOpcode::G_ADD, DstTy}))
    return false;

  MatchInfo = [=](MachineIRBuilder &B) {
    auto VScale = B.buildVScale(DstTy, -RHSVScale->getSrc());
    B.buildAdd(Dst, Sub->getLHSReg(), VScale, Sub->getFlags());
  };

  return true;
}

bool CombinerHelper::matchShlOfVScale(const MachineOperand &MO,
                                      BuildFnTy &MatchInfo) {
  GShl *Shl = cast<GShl>(MRI.getVRegDef(MO.getReg()));
  GVScale *LHSVScale = cast<GVScale>(MRI.getVRegDef(Shl->getSrcReg()));

  std::optional<APInt> MaybeRHS = getIConstantVRegVal(Shl->getShiftReg(), MRI);
  if (!MaybeRHS)
    return false;

  Register Dst = MO.getReg();
  LLT DstTy = MRI.getType(Dst);

  if (!MRI.hasOneNonDBGUse(LHSVScale->getReg(0)) ||
      !isLegalOrBeforeLegalizer({TargetOpcode::G_VSCALE, DstTy}))
    return false;

  MatchInfo = [=](MachineIRBuilder &B) {
    B.buildVScale(Dst, LHSVScale->getSrc().shl(*MaybeRHS));
  };

  return true;
}
