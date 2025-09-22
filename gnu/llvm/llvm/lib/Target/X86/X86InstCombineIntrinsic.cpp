//===-- X86InstCombineIntrinsic.cpp - X86 specific InstCombine pass -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements a TargetTransformInfo analysis pass specific to the
/// X86 target machine. It uses the target's detailed information to provide
/// more precise answers to certain TTI queries, while letting the target
/// independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#include "X86TargetTransformInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsX86.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include <optional>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "x86tti"

/// Return a constant boolean vector that has true elements in all positions
/// where the input constant data vector has an element with the sign bit set.
static Constant *getNegativeIsTrueBoolVec(Constant *V, const DataLayout &DL) {
  VectorType *IntTy = VectorType::getInteger(cast<VectorType>(V->getType()));
  V = ConstantExpr::getBitCast(V, IntTy);
  V = ConstantFoldCompareInstOperands(CmpInst::ICMP_SGT,
                                      Constant::getNullValue(IntTy), V, DL);
  assert(V && "Vector must be foldable");
  return V;
}

/// Convert the x86 XMM integer vector mask to a vector of bools based on
/// each element's most significant bit (the sign bit).
static Value *getBoolVecFromMask(Value *Mask, const DataLayout &DL) {
  // Fold Constant Mask.
  if (auto *ConstantMask = dyn_cast<ConstantDataVector>(Mask))
    return getNegativeIsTrueBoolVec(ConstantMask, DL);

  // Mask was extended from a boolean vector.
  Value *ExtMask;
  if (match(Mask, m_SExt(m_Value(ExtMask))) &&
      ExtMask->getType()->isIntOrIntVectorTy(1))
    return ExtMask;

  return nullptr;
}

// TODO: If the x86 backend knew how to convert a bool vector mask back to an
// XMM register mask efficiently, we could transform all x86 masked intrinsics
// to LLVM masked intrinsics and remove the x86 masked intrinsic defs.
static Instruction *simplifyX86MaskedLoad(IntrinsicInst &II, InstCombiner &IC) {
  Value *Ptr = II.getOperand(0);
  Value *Mask = II.getOperand(1);
  Constant *ZeroVec = Constant::getNullValue(II.getType());

  // Zero Mask - masked load instruction creates a zero vector.
  if (isa<ConstantAggregateZero>(Mask))
    return IC.replaceInstUsesWith(II, ZeroVec);

  // The mask is constant or extended from a bool vector. Convert this x86
  // intrinsic to the LLVM intrinsic to allow target-independent optimizations.
  if (Value *BoolMask = getBoolVecFromMask(Mask, IC.getDataLayout())) {
    // First, cast the x86 intrinsic scalar pointer to a vector pointer to match
    // the LLVM intrinsic definition for the pointer argument.
    unsigned AddrSpace = cast<PointerType>(Ptr->getType())->getAddressSpace();
    PointerType *VecPtrTy = PointerType::get(II.getType(), AddrSpace);
    Value *PtrCast = IC.Builder.CreateBitCast(Ptr, VecPtrTy, "castvec");

    // The pass-through vector for an x86 masked load is a zero vector.
    CallInst *NewMaskedLoad = IC.Builder.CreateMaskedLoad(
        II.getType(), PtrCast, Align(1), BoolMask, ZeroVec);
    return IC.replaceInstUsesWith(II, NewMaskedLoad);
  }

  return nullptr;
}

// TODO: If the x86 backend knew how to convert a bool vector mask back to an
// XMM register mask efficiently, we could transform all x86 masked intrinsics
// to LLVM masked intrinsics and remove the x86 masked intrinsic defs.
static bool simplifyX86MaskedStore(IntrinsicInst &II, InstCombiner &IC) {
  Value *Ptr = II.getOperand(0);
  Value *Mask = II.getOperand(1);
  Value *Vec = II.getOperand(2);

  // Zero Mask - this masked store instruction does nothing.
  if (isa<ConstantAggregateZero>(Mask)) {
    IC.eraseInstFromFunction(II);
    return true;
  }

  // The SSE2 version is too weird (eg, unaligned but non-temporal) to do
  // anything else at this level.
  if (II.getIntrinsicID() == Intrinsic::x86_sse2_maskmov_dqu)
    return false;

  // The mask is constant or extended from a bool vector. Convert this x86
  // intrinsic to the LLVM intrinsic to allow target-independent optimizations.
  if (Value *BoolMask = getBoolVecFromMask(Mask, IC.getDataLayout())) {
    unsigned AddrSpace = cast<PointerType>(Ptr->getType())->getAddressSpace();
    PointerType *VecPtrTy = PointerType::get(Vec->getType(), AddrSpace);
    Value *PtrCast = IC.Builder.CreateBitCast(Ptr, VecPtrTy, "castvec");

    IC.Builder.CreateMaskedStore(Vec, PtrCast, Align(1), BoolMask);

    // 'Replace uses' doesn't work for stores. Erase the original masked store.
    IC.eraseInstFromFunction(II);
    return true;
  }

  return false;
}

static Value *simplifyX86immShift(const IntrinsicInst &II,
                                  InstCombiner::BuilderTy &Builder) {
  bool LogicalShift = false;
  bool ShiftLeft = false;
  bool IsImm = false;

  switch (II.getIntrinsicID()) {
  default:
    llvm_unreachable("Unexpected intrinsic!");
  case Intrinsic::x86_sse2_psrai_d:
  case Intrinsic::x86_sse2_psrai_w:
  case Intrinsic::x86_avx2_psrai_d:
  case Intrinsic::x86_avx2_psrai_w:
  case Intrinsic::x86_avx512_psrai_q_128:
  case Intrinsic::x86_avx512_psrai_q_256:
  case Intrinsic::x86_avx512_psrai_d_512:
  case Intrinsic::x86_avx512_psrai_q_512:
  case Intrinsic::x86_avx512_psrai_w_512:
    IsImm = true;
    [[fallthrough]];
  case Intrinsic::x86_sse2_psra_d:
  case Intrinsic::x86_sse2_psra_w:
  case Intrinsic::x86_avx2_psra_d:
  case Intrinsic::x86_avx2_psra_w:
  case Intrinsic::x86_avx512_psra_q_128:
  case Intrinsic::x86_avx512_psra_q_256:
  case Intrinsic::x86_avx512_psra_d_512:
  case Intrinsic::x86_avx512_psra_q_512:
  case Intrinsic::x86_avx512_psra_w_512:
    LogicalShift = false;
    ShiftLeft = false;
    break;
  case Intrinsic::x86_sse2_psrli_d:
  case Intrinsic::x86_sse2_psrli_q:
  case Intrinsic::x86_sse2_psrli_w:
  case Intrinsic::x86_avx2_psrli_d:
  case Intrinsic::x86_avx2_psrli_q:
  case Intrinsic::x86_avx2_psrli_w:
  case Intrinsic::x86_avx512_psrli_d_512:
  case Intrinsic::x86_avx512_psrli_q_512:
  case Intrinsic::x86_avx512_psrli_w_512:
    IsImm = true;
    [[fallthrough]];
  case Intrinsic::x86_sse2_psrl_d:
  case Intrinsic::x86_sse2_psrl_q:
  case Intrinsic::x86_sse2_psrl_w:
  case Intrinsic::x86_avx2_psrl_d:
  case Intrinsic::x86_avx2_psrl_q:
  case Intrinsic::x86_avx2_psrl_w:
  case Intrinsic::x86_avx512_psrl_d_512:
  case Intrinsic::x86_avx512_psrl_q_512:
  case Intrinsic::x86_avx512_psrl_w_512:
    LogicalShift = true;
    ShiftLeft = false;
    break;
  case Intrinsic::x86_sse2_pslli_d:
  case Intrinsic::x86_sse2_pslli_q:
  case Intrinsic::x86_sse2_pslli_w:
  case Intrinsic::x86_avx2_pslli_d:
  case Intrinsic::x86_avx2_pslli_q:
  case Intrinsic::x86_avx2_pslli_w:
  case Intrinsic::x86_avx512_pslli_d_512:
  case Intrinsic::x86_avx512_pslli_q_512:
  case Intrinsic::x86_avx512_pslli_w_512:
    IsImm = true;
    [[fallthrough]];
  case Intrinsic::x86_sse2_psll_d:
  case Intrinsic::x86_sse2_psll_q:
  case Intrinsic::x86_sse2_psll_w:
  case Intrinsic::x86_avx2_psll_d:
  case Intrinsic::x86_avx2_psll_q:
  case Intrinsic::x86_avx2_psll_w:
  case Intrinsic::x86_avx512_psll_d_512:
  case Intrinsic::x86_avx512_psll_q_512:
  case Intrinsic::x86_avx512_psll_w_512:
    LogicalShift = true;
    ShiftLeft = true;
    break;
  }
  assert((LogicalShift || !ShiftLeft) && "Only logical shifts can shift left");

  Value *Vec = II.getArgOperand(0);
  Value *Amt = II.getArgOperand(1);
  auto *VT = cast<FixedVectorType>(Vec->getType());
  Type *SVT = VT->getElementType();
  Type *AmtVT = Amt->getType();
  unsigned VWidth = VT->getNumElements();
  unsigned BitWidth = SVT->getPrimitiveSizeInBits();

  // If the shift amount is guaranteed to be in-range we can replace it with a
  // generic shift. If its guaranteed to be out of range, logical shifts combine
  // to zero and arithmetic shifts are clamped to (BitWidth - 1).
  if (IsImm) {
    assert(AmtVT->isIntegerTy(32) && "Unexpected shift-by-immediate type");
    KnownBits KnownAmtBits =
        llvm::computeKnownBits(Amt, II.getDataLayout());
    if (KnownAmtBits.getMaxValue().ult(BitWidth)) {
      Amt = Builder.CreateZExtOrTrunc(Amt, SVT);
      Amt = Builder.CreateVectorSplat(VWidth, Amt);
      return (LogicalShift ? (ShiftLeft ? Builder.CreateShl(Vec, Amt)
                                        : Builder.CreateLShr(Vec, Amt))
                           : Builder.CreateAShr(Vec, Amt));
    }
    if (KnownAmtBits.getMinValue().uge(BitWidth)) {
      if (LogicalShift)
        return ConstantAggregateZero::get(VT);
      Amt = ConstantInt::get(SVT, BitWidth - 1);
      return Builder.CreateAShr(Vec, Builder.CreateVectorSplat(VWidth, Amt));
    }
  } else {
    // Ensure the first element has an in-range value and the rest of the
    // elements in the bottom 64 bits are zero.
    assert(AmtVT->isVectorTy() && AmtVT->getPrimitiveSizeInBits() == 128 &&
           cast<VectorType>(AmtVT)->getElementType() == SVT &&
           "Unexpected shift-by-scalar type");
    unsigned NumAmtElts = cast<FixedVectorType>(AmtVT)->getNumElements();
    APInt DemandedLower = APInt::getOneBitSet(NumAmtElts, 0);
    APInt DemandedUpper = APInt::getBitsSet(NumAmtElts, 1, NumAmtElts / 2);
    KnownBits KnownLowerBits = llvm::computeKnownBits(
        Amt, DemandedLower, II.getDataLayout());
    KnownBits KnownUpperBits = llvm::computeKnownBits(
        Amt, DemandedUpper, II.getDataLayout());
    if (KnownLowerBits.getMaxValue().ult(BitWidth) &&
        (DemandedUpper.isZero() || KnownUpperBits.isZero())) {
      SmallVector<int, 16> ZeroSplat(VWidth, 0);
      Amt = Builder.CreateShuffleVector(Amt, ZeroSplat);
      return (LogicalShift ? (ShiftLeft ? Builder.CreateShl(Vec, Amt)
                                        : Builder.CreateLShr(Vec, Amt))
                           : Builder.CreateAShr(Vec, Amt));
    }
  }

  // Simplify if count is constant vector.
  auto *CDV = dyn_cast<ConstantDataVector>(Amt);
  if (!CDV)
    return nullptr;

  // SSE2/AVX2 uses all the first 64-bits of the 128-bit vector
  // operand to compute the shift amount.
  assert(AmtVT->isVectorTy() && AmtVT->getPrimitiveSizeInBits() == 128 &&
         cast<VectorType>(AmtVT)->getElementType() == SVT &&
         "Unexpected shift-by-scalar type");

  // Concatenate the sub-elements to create the 64-bit value.
  APInt Count(64, 0);
  for (unsigned i = 0, NumSubElts = 64 / BitWidth; i != NumSubElts; ++i) {
    unsigned SubEltIdx = (NumSubElts - 1) - i;
    auto *SubElt = cast<ConstantInt>(CDV->getElementAsConstant(SubEltIdx));
    Count <<= BitWidth;
    Count |= SubElt->getValue().zextOrTrunc(64);
  }

  // If shift-by-zero then just return the original value.
  if (Count.isZero())
    return Vec;

  // Handle cases when Shift >= BitWidth.
  if (Count.uge(BitWidth)) {
    // If LogicalShift - just return zero.
    if (LogicalShift)
      return ConstantAggregateZero::get(VT);

    // If ArithmeticShift - clamp Shift to (BitWidth - 1).
    Count = APInt(64, BitWidth - 1);
  }

  // Get a constant vector of the same type as the first operand.
  auto ShiftAmt = ConstantInt::get(SVT, Count.zextOrTrunc(BitWidth));
  auto ShiftVec = Builder.CreateVectorSplat(VWidth, ShiftAmt);

  if (ShiftLeft)
    return Builder.CreateShl(Vec, ShiftVec);

  if (LogicalShift)
    return Builder.CreateLShr(Vec, ShiftVec);

  return Builder.CreateAShr(Vec, ShiftVec);
}

// Attempt to simplify AVX2 per-element shift intrinsics to a generic IR shift.
// Unlike the generic IR shifts, the intrinsics have defined behaviour for out
// of range shift amounts (logical - set to zero, arithmetic - splat sign bit).
static Value *simplifyX86varShift(const IntrinsicInst &II,
                                  InstCombiner::BuilderTy &Builder) {
  bool LogicalShift = false;
  bool ShiftLeft = false;

  switch (II.getIntrinsicID()) {
  default:
    llvm_unreachable("Unexpected intrinsic!");
  case Intrinsic::x86_avx2_psrav_d:
  case Intrinsic::x86_avx2_psrav_d_256:
  case Intrinsic::x86_avx512_psrav_q_128:
  case Intrinsic::x86_avx512_psrav_q_256:
  case Intrinsic::x86_avx512_psrav_d_512:
  case Intrinsic::x86_avx512_psrav_q_512:
  case Intrinsic::x86_avx512_psrav_w_128:
  case Intrinsic::x86_avx512_psrav_w_256:
  case Intrinsic::x86_avx512_psrav_w_512:
    LogicalShift = false;
    ShiftLeft = false;
    break;
  case Intrinsic::x86_avx2_psrlv_d:
  case Intrinsic::x86_avx2_psrlv_d_256:
  case Intrinsic::x86_avx2_psrlv_q:
  case Intrinsic::x86_avx2_psrlv_q_256:
  case Intrinsic::x86_avx512_psrlv_d_512:
  case Intrinsic::x86_avx512_psrlv_q_512:
  case Intrinsic::x86_avx512_psrlv_w_128:
  case Intrinsic::x86_avx512_psrlv_w_256:
  case Intrinsic::x86_avx512_psrlv_w_512:
    LogicalShift = true;
    ShiftLeft = false;
    break;
  case Intrinsic::x86_avx2_psllv_d:
  case Intrinsic::x86_avx2_psllv_d_256:
  case Intrinsic::x86_avx2_psllv_q:
  case Intrinsic::x86_avx2_psllv_q_256:
  case Intrinsic::x86_avx512_psllv_d_512:
  case Intrinsic::x86_avx512_psllv_q_512:
  case Intrinsic::x86_avx512_psllv_w_128:
  case Intrinsic::x86_avx512_psllv_w_256:
  case Intrinsic::x86_avx512_psllv_w_512:
    LogicalShift = true;
    ShiftLeft = true;
    break;
  }
  assert((LogicalShift || !ShiftLeft) && "Only logical shifts can shift left");

  Value *Vec = II.getArgOperand(0);
  Value *Amt = II.getArgOperand(1);
  auto *VT = cast<FixedVectorType>(II.getType());
  Type *SVT = VT->getElementType();
  int NumElts = VT->getNumElements();
  int BitWidth = SVT->getIntegerBitWidth();

  // If the shift amount is guaranteed to be in-range we can replace it with a
  // generic shift.
  KnownBits KnownAmt =
      llvm::computeKnownBits(Amt, II.getDataLayout());
  if (KnownAmt.getMaxValue().ult(BitWidth)) {
    return (LogicalShift ? (ShiftLeft ? Builder.CreateShl(Vec, Amt)
                                      : Builder.CreateLShr(Vec, Amt))
                         : Builder.CreateAShr(Vec, Amt));
  }

  // Simplify if all shift amounts are constant/undef.
  auto *CShift = dyn_cast<Constant>(Amt);
  if (!CShift)
    return nullptr;

  // Collect each element's shift amount.
  // We also collect special cases: UNDEF = -1, OUT-OF-RANGE = BitWidth.
  bool AnyOutOfRange = false;
  SmallVector<int, 8> ShiftAmts;
  for (int I = 0; I < NumElts; ++I) {
    auto *CElt = CShift->getAggregateElement(I);
    if (isa_and_nonnull<UndefValue>(CElt)) {
      ShiftAmts.push_back(-1);
      continue;
    }

    auto *COp = dyn_cast_or_null<ConstantInt>(CElt);
    if (!COp)
      return nullptr;

    // Handle out of range shifts.
    // If LogicalShift - set to BitWidth (special case).
    // If ArithmeticShift - set to (BitWidth - 1) (sign splat).
    APInt ShiftVal = COp->getValue();
    if (ShiftVal.uge(BitWidth)) {
      AnyOutOfRange = LogicalShift;
      ShiftAmts.push_back(LogicalShift ? BitWidth : BitWidth - 1);
      continue;
    }

    ShiftAmts.push_back((int)ShiftVal.getZExtValue());
  }

  // If all elements out of range or UNDEF, return vector of zeros/undefs.
  // ArithmeticShift should only hit this if they are all UNDEF.
  auto OutOfRange = [&](int Idx) { return (Idx < 0) || (BitWidth <= Idx); };
  if (llvm::all_of(ShiftAmts, OutOfRange)) {
    SmallVector<Constant *, 8> ConstantVec;
    for (int Idx : ShiftAmts) {
      if (Idx < 0) {
        ConstantVec.push_back(UndefValue::get(SVT));
      } else {
        assert(LogicalShift && "Logical shift expected");
        ConstantVec.push_back(ConstantInt::getNullValue(SVT));
      }
    }
    return ConstantVector::get(ConstantVec);
  }

  // We can't handle only some out of range values with generic logical shifts.
  if (AnyOutOfRange)
    return nullptr;

  // Build the shift amount constant vector.
  SmallVector<Constant *, 8> ShiftVecAmts;
  for (int Idx : ShiftAmts) {
    if (Idx < 0)
      ShiftVecAmts.push_back(UndefValue::get(SVT));
    else
      ShiftVecAmts.push_back(ConstantInt::get(SVT, Idx));
  }
  auto ShiftVec = ConstantVector::get(ShiftVecAmts);

  if (ShiftLeft)
    return Builder.CreateShl(Vec, ShiftVec);

  if (LogicalShift)
    return Builder.CreateLShr(Vec, ShiftVec);

  return Builder.CreateAShr(Vec, ShiftVec);
}

static Value *simplifyX86pack(IntrinsicInst &II,
                              InstCombiner::BuilderTy &Builder, bool IsSigned) {
  Value *Arg0 = II.getArgOperand(0);
  Value *Arg1 = II.getArgOperand(1);
  Type *ResTy = II.getType();

  // Fast all undef handling.
  if (isa<UndefValue>(Arg0) && isa<UndefValue>(Arg1))
    return UndefValue::get(ResTy);

  auto *ArgTy = cast<FixedVectorType>(Arg0->getType());
  unsigned NumLanes = ResTy->getPrimitiveSizeInBits() / 128;
  unsigned NumSrcElts = ArgTy->getNumElements();
  assert(cast<FixedVectorType>(ResTy)->getNumElements() == (2 * NumSrcElts) &&
         "Unexpected packing types");

  unsigned NumSrcEltsPerLane = NumSrcElts / NumLanes;
  unsigned DstScalarSizeInBits = ResTy->getScalarSizeInBits();
  unsigned SrcScalarSizeInBits = ArgTy->getScalarSizeInBits();
  assert(SrcScalarSizeInBits == (2 * DstScalarSizeInBits) &&
         "Unexpected packing types");

  // Constant folding.
  if (!isa<Constant>(Arg0) || !isa<Constant>(Arg1))
    return nullptr;

  // Clamp Values - signed/unsigned both use signed clamp values, but they
  // differ on the min/max values.
  APInt MinValue, MaxValue;
  if (IsSigned) {
    // PACKSS: Truncate signed value with signed saturation.
    // Source values less than dst minint are saturated to minint.
    // Source values greater than dst maxint are saturated to maxint.
    MinValue =
        APInt::getSignedMinValue(DstScalarSizeInBits).sext(SrcScalarSizeInBits);
    MaxValue =
        APInt::getSignedMaxValue(DstScalarSizeInBits).sext(SrcScalarSizeInBits);
  } else {
    // PACKUS: Truncate signed value with unsigned saturation.
    // Source values less than zero are saturated to zero.
    // Source values greater than dst maxuint are saturated to maxuint.
    MinValue = APInt::getZero(SrcScalarSizeInBits);
    MaxValue = APInt::getLowBitsSet(SrcScalarSizeInBits, DstScalarSizeInBits);
  }

  auto *MinC = Constant::getIntegerValue(ArgTy, MinValue);
  auto *MaxC = Constant::getIntegerValue(ArgTy, MaxValue);
  Arg0 = Builder.CreateSelect(Builder.CreateICmpSLT(Arg0, MinC), MinC, Arg0);
  Arg1 = Builder.CreateSelect(Builder.CreateICmpSLT(Arg1, MinC), MinC, Arg1);
  Arg0 = Builder.CreateSelect(Builder.CreateICmpSGT(Arg0, MaxC), MaxC, Arg0);
  Arg1 = Builder.CreateSelect(Builder.CreateICmpSGT(Arg1, MaxC), MaxC, Arg1);

  // Shuffle clamped args together at the lane level.
  SmallVector<int, 32> PackMask;
  for (unsigned Lane = 0; Lane != NumLanes; ++Lane) {
    for (unsigned Elt = 0; Elt != NumSrcEltsPerLane; ++Elt)
      PackMask.push_back(Elt + (Lane * NumSrcEltsPerLane));
    for (unsigned Elt = 0; Elt != NumSrcEltsPerLane; ++Elt)
      PackMask.push_back(Elt + (Lane * NumSrcEltsPerLane) + NumSrcElts);
  }
  auto *Shuffle = Builder.CreateShuffleVector(Arg0, Arg1, PackMask);

  // Truncate to dst size.
  return Builder.CreateTrunc(Shuffle, ResTy);
}

static Value *simplifyX86pmulh(IntrinsicInst &II,
                               InstCombiner::BuilderTy &Builder, bool IsSigned,
                               bool IsRounding) {
  Value *Arg0 = II.getArgOperand(0);
  Value *Arg1 = II.getArgOperand(1);
  auto *ResTy = cast<FixedVectorType>(II.getType());
  auto *ArgTy = cast<FixedVectorType>(Arg0->getType());
  assert(ArgTy == ResTy && ResTy->getScalarSizeInBits() == 16 &&
         "Unexpected PMULH types");
  assert((!IsRounding || IsSigned) && "PMULHRS instruction must be signed");

  // Multiply by undef -> zero (NOT undef!) as other arg could still be zero.
  if (isa<UndefValue>(Arg0) || isa<UndefValue>(Arg1))
    return ConstantAggregateZero::get(ResTy);

  // Multiply by zero.
  if (isa<ConstantAggregateZero>(Arg0) || isa<ConstantAggregateZero>(Arg1))
    return ConstantAggregateZero::get(ResTy);

  // Multiply by one.
  if (!IsRounding) {
    if (match(Arg0, m_One()))
      return IsSigned ? Builder.CreateAShr(Arg1, 15)
                      : ConstantAggregateZero::get(ResTy);
    if (match(Arg1, m_One()))
      return IsSigned ? Builder.CreateAShr(Arg0, 15)
                      : ConstantAggregateZero::get(ResTy);
  }

  // Constant folding.
  if (!isa<Constant>(Arg0) || !isa<Constant>(Arg1))
    return nullptr;

  // Extend to twice the width and multiply.
  auto Cast =
      IsSigned ? Instruction::CastOps::SExt : Instruction::CastOps::ZExt;
  auto *ExtTy = FixedVectorType::getExtendedElementVectorType(ArgTy);
  Value *LHS = Builder.CreateCast(Cast, Arg0, ExtTy);
  Value *RHS = Builder.CreateCast(Cast, Arg1, ExtTy);
  Value *Mul = Builder.CreateMul(LHS, RHS);

  if (IsRounding) {
    // PMULHRSW: truncate to vXi18 of the most significant bits, add one and
    // extract bits[16:1].
    auto *RndEltTy = IntegerType::get(ExtTy->getContext(), 18);
    auto *RndTy = FixedVectorType::get(RndEltTy, ExtTy);
    Mul = Builder.CreateLShr(Mul, 14);
    Mul = Builder.CreateTrunc(Mul, RndTy);
    Mul = Builder.CreateAdd(Mul, ConstantInt::get(RndTy, 1));
    Mul = Builder.CreateLShr(Mul, 1);
  } else {
    // PMULH/PMULHU: extract the vXi16 most significant bits.
    Mul = Builder.CreateLShr(Mul, 16);
  }

  return Builder.CreateTrunc(Mul, ResTy);
}

static Value *simplifyX86pmadd(IntrinsicInst &II,
                               InstCombiner::BuilderTy &Builder,
                               bool IsPMADDWD) {
  Value *Arg0 = II.getArgOperand(0);
  Value *Arg1 = II.getArgOperand(1);
  auto *ResTy = cast<FixedVectorType>(II.getType());
  [[maybe_unused]] auto *ArgTy = cast<FixedVectorType>(Arg0->getType());

  unsigned NumDstElts = ResTy->getNumElements();
  assert(ArgTy->getNumElements() == (2 * NumDstElts) &&
         ResTy->getScalarSizeInBits() == (2 * ArgTy->getScalarSizeInBits()) &&
         "Unexpected PMADD types");

  // Multiply by undef -> zero (NOT undef!) as other arg could still be zero.
  if (isa<UndefValue>(Arg0) || isa<UndefValue>(Arg1))
    return ConstantAggregateZero::get(ResTy);

  // Multiply by zero.
  if (isa<ConstantAggregateZero>(Arg0) || isa<ConstantAggregateZero>(Arg1))
    return ConstantAggregateZero::get(ResTy);

  // Constant folding.
  if (!isa<Constant>(Arg0) || !isa<Constant>(Arg1))
    return nullptr;

  // Split Lo/Hi elements pairs, extend and add together.
  // PMADDWD(X,Y) =
  // add(mul(sext(lhs[0]),sext(rhs[0])),mul(sext(lhs[1]),sext(rhs[1])))
  // PMADDUBSW(X,Y) =
  // sadd_sat(mul(zext(lhs[0]),sext(rhs[0])),mul(zext(lhs[1]),sext(rhs[1])))
  SmallVector<int> LoMask, HiMask;
  for (unsigned I = 0; I != NumDstElts; ++I) {
    LoMask.push_back(2 * I + 0);
    HiMask.push_back(2 * I + 1);
  }

  auto *LHSLo = Builder.CreateShuffleVector(Arg0, LoMask);
  auto *LHSHi = Builder.CreateShuffleVector(Arg0, HiMask);
  auto *RHSLo = Builder.CreateShuffleVector(Arg1, LoMask);
  auto *RHSHi = Builder.CreateShuffleVector(Arg1, HiMask);

  auto LHSCast =
      IsPMADDWD ? Instruction::CastOps::SExt : Instruction::CastOps::ZExt;
  LHSLo = Builder.CreateCast(LHSCast, LHSLo, ResTy);
  LHSHi = Builder.CreateCast(LHSCast, LHSHi, ResTy);
  RHSLo = Builder.CreateCast(Instruction::CastOps::SExt, RHSLo, ResTy);
  RHSHi = Builder.CreateCast(Instruction::CastOps::SExt, RHSHi, ResTy);
  Value *Lo = Builder.CreateMul(LHSLo, RHSLo);
  Value *Hi = Builder.CreateMul(LHSHi, RHSHi);
  return IsPMADDWD
             ? Builder.CreateAdd(Lo, Hi)
             : Builder.CreateIntrinsic(ResTy, Intrinsic::sadd_sat, {Lo, Hi});
}

static Value *simplifyX86movmsk(const IntrinsicInst &II,
                                InstCombiner::BuilderTy &Builder) {
  Value *Arg = II.getArgOperand(0);
  Type *ResTy = II.getType();

  // movmsk(undef) -> zero as we must ensure the upper bits are zero.
  if (isa<UndefValue>(Arg))
    return Constant::getNullValue(ResTy);

  auto *ArgTy = dyn_cast<FixedVectorType>(Arg->getType());
  // We can't easily peek through x86_mmx types.
  if (!ArgTy)
    return nullptr;

  // Expand MOVMSK to compare/bitcast/zext:
  // e.g. PMOVMSKB(v16i8 x):
  // %cmp = icmp slt <16 x i8> %x, zeroinitializer
  // %int = bitcast <16 x i1> %cmp to i16
  // %res = zext i16 %int to i32
  unsigned NumElts = ArgTy->getNumElements();
  Type *IntegerTy = Builder.getIntNTy(NumElts);

  Value *Res = Builder.CreateBitCast(Arg, VectorType::getInteger(ArgTy));
  Res = Builder.CreateIsNeg(Res);
  Res = Builder.CreateBitCast(Res, IntegerTy);
  Res = Builder.CreateZExtOrTrunc(Res, ResTy);
  return Res;
}

static Value *simplifyX86addcarry(const IntrinsicInst &II,
                                  InstCombiner::BuilderTy &Builder) {
  Value *CarryIn = II.getArgOperand(0);
  Value *Op1 = II.getArgOperand(1);
  Value *Op2 = II.getArgOperand(2);
  Type *RetTy = II.getType();
  Type *OpTy = Op1->getType();
  assert(RetTy->getStructElementType(0)->isIntegerTy(8) &&
         RetTy->getStructElementType(1) == OpTy && OpTy == Op2->getType() &&
         "Unexpected types for x86 addcarry");

  // If carry-in is zero, this is just an unsigned add with overflow.
  if (match(CarryIn, m_ZeroInt())) {
    Value *UAdd = Builder.CreateIntrinsic(Intrinsic::uadd_with_overflow, OpTy,
                                          {Op1, Op2});
    // The types have to be adjusted to match the x86 call types.
    Value *UAddResult = Builder.CreateExtractValue(UAdd, 0);
    Value *UAddOV = Builder.CreateZExt(Builder.CreateExtractValue(UAdd, 1),
                                       Builder.getInt8Ty());
    Value *Res = PoisonValue::get(RetTy);
    Res = Builder.CreateInsertValue(Res, UAddOV, 0);
    return Builder.CreateInsertValue(Res, UAddResult, 1);
  }

  return nullptr;
}

static Value *simplifyTernarylogic(const IntrinsicInst &II,
                                   InstCombiner::BuilderTy &Builder) {

  auto *ArgImm = dyn_cast<ConstantInt>(II.getArgOperand(3));
  if (!ArgImm || ArgImm->getValue().uge(256))
    return nullptr;

  Value *ArgA = II.getArgOperand(0);
  Value *ArgB = II.getArgOperand(1);
  Value *ArgC = II.getArgOperand(2);

  Type *Ty = II.getType();

  auto Or = [&](auto Lhs, auto Rhs) -> std::pair<Value *, uint8_t> {
    return {Builder.CreateOr(Lhs.first, Rhs.first), Lhs.second | Rhs.second};
  };
  auto Xor = [&](auto Lhs, auto Rhs) -> std::pair<Value *, uint8_t> {
    return {Builder.CreateXor(Lhs.first, Rhs.first), Lhs.second ^ Rhs.second};
  };
  auto And = [&](auto Lhs, auto Rhs) -> std::pair<Value *, uint8_t> {
    return {Builder.CreateAnd(Lhs.first, Rhs.first), Lhs.second & Rhs.second};
  };
  auto Not = [&](auto V) -> std::pair<Value *, uint8_t> {
    return {Builder.CreateNot(V.first), ~V.second};
  };
  auto Nor = [&](auto Lhs, auto Rhs) { return Not(Or(Lhs, Rhs)); };
  auto Xnor = [&](auto Lhs, auto Rhs) { return Not(Xor(Lhs, Rhs)); };
  auto Nand = [&](auto Lhs, auto Rhs) { return Not(And(Lhs, Rhs)); };

  bool AIsConst = match(ArgA, m_ImmConstant());
  bool BIsConst = match(ArgB, m_ImmConstant());
  bool CIsConst = match(ArgC, m_ImmConstant());

  bool ABIsConst = AIsConst && BIsConst;
  bool ACIsConst = AIsConst && CIsConst;
  bool BCIsConst = BIsConst && CIsConst;
  bool ABCIsConst = AIsConst && BIsConst && CIsConst;

  // Use for verification. Its a big table. Its difficult to go from Imm ->
  // logic ops, but easy to verify that a set of logic ops is correct. We track
  // the logic ops through the second value in the pair. At the end it should
  // equal Imm.
  std::pair<Value *, uint8_t> A = {ArgA, 0xf0};
  std::pair<Value *, uint8_t> B = {ArgB, 0xcc};
  std::pair<Value *, uint8_t> C = {ArgC, 0xaa};
  std::pair<Value *, uint8_t> Res = {nullptr, 0};

  // Currently we only handle cases that convert directly to another instruction
  // or cases where all the ops are constant.  This is because we don't properly
  // handle creating ternary ops in the backend, so splitting them here may
  // cause regressions. As the backend improves, uncomment more cases.

  uint8_t Imm = ArgImm->getValue().getZExtValue();
  switch (Imm) {
  case 0x0:
    Res = {Constant::getNullValue(Ty), 0};
    break;
  case 0x1:
    if (ABCIsConst)
      Res = Nor(Or(A, B), C);
    break;
  case 0x2:
    if (ABCIsConst)
      Res = And(Nor(A, B), C);
    break;
  case 0x3:
    if (ABIsConst)
      Res = Nor(A, B);
    break;
  case 0x4:
    if (ABCIsConst)
      Res = And(Nor(A, C), B);
    break;
  case 0x5:
    if (ACIsConst)
      Res = Nor(A, C);
    break;
  case 0x6:
    if (ABCIsConst)
      Res = Nor(A, Xnor(B, C));
    break;
  case 0x7:
    if (ABCIsConst)
      Res = Nor(A, And(B, C));
    break;
  case 0x8:
    if (ABCIsConst)
      Res = Nor(A, Nand(B, C));
    break;
  case 0x9:
    if (ABCIsConst)
      Res = Nor(A, Xor(B, C));
    break;
  case 0xa:
    if (ACIsConst)
      Res = Nor(A, Not(C));
    break;
  case 0xb:
    if (ABCIsConst)
      Res = Nor(A, Nor(C, Not(B)));
    break;
  case 0xc:
    if (ABIsConst)
      Res = Nor(A, Not(B));
    break;
  case 0xd:
    if (ABCIsConst)
      Res = Nor(A, Nor(B, Not(C)));
    break;
  case 0xe:
    if (ABCIsConst)
      Res = Nor(A, Nor(B, C));
    break;
  case 0xf:
    Res = Not(A);
    break;
  case 0x10:
    if (ABCIsConst)
      Res = And(A, Nor(B, C));
    break;
  case 0x11:
    if (BCIsConst)
      Res = Nor(B, C);
    break;
  case 0x12:
    if (ABCIsConst)
      Res = Nor(Xnor(A, C), B);
    break;
  case 0x13:
    if (ABCIsConst)
      Res = Nor(And(A, C), B);
    break;
  case 0x14:
    if (ABCIsConst)
      Res = Nor(Xnor(A, B), C);
    break;
  case 0x15:
    if (ABCIsConst)
      Res = Nor(And(A, B), C);
    break;
  case 0x16:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), And(Nand(A, B), C));
    break;
  case 0x17:
    if (ABCIsConst)
      Res = Xor(Or(A, B), Or(Xnor(A, B), C));
    break;
  case 0x18:
    if (ABCIsConst)
      Res = Nor(Xnor(A, B), Xnor(A, C));
    break;
  case 0x19:
    if (ABCIsConst)
      Res = And(Nand(A, B), Xnor(B, C));
    break;
  case 0x1a:
    if (ABCIsConst)
      Res = Xor(A, Or(And(A, B), C));
    break;
  case 0x1b:
    if (ABCIsConst)
      Res = Xor(A, Or(Xnor(A, B), C));
    break;
  case 0x1c:
    if (ABCIsConst)
      Res = Xor(A, Or(And(A, C), B));
    break;
  case 0x1d:
    if (ABCIsConst)
      Res = Xor(A, Or(Xnor(A, C), B));
    break;
  case 0x1e:
    if (ABCIsConst)
      Res = Xor(A, Or(B, C));
    break;
  case 0x1f:
    if (ABCIsConst)
      Res = Nand(A, Or(B, C));
    break;
  case 0x20:
    if (ABCIsConst)
      Res = Nor(Nand(A, C), B);
    break;
  case 0x21:
    if (ABCIsConst)
      Res = Nor(Xor(A, C), B);
    break;
  case 0x22:
    if (BCIsConst)
      Res = Nor(B, Not(C));
    break;
  case 0x23:
    if (ABCIsConst)
      Res = Nor(B, Nor(C, Not(A)));
    break;
  case 0x24:
    if (ABCIsConst)
      Res = Nor(Xnor(A, B), Xor(A, C));
    break;
  case 0x25:
    if (ABCIsConst)
      Res = Xor(A, Nand(Nand(A, B), C));
    break;
  case 0x26:
    if (ABCIsConst)
      Res = And(Nand(A, B), Xor(B, C));
    break;
  case 0x27:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), C), B);
    break;
  case 0x28:
    if (ABCIsConst)
      Res = And(Xor(A, B), C);
    break;
  case 0x29:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), Nor(And(A, B), C));
    break;
  case 0x2a:
    if (ABCIsConst)
      Res = And(Nand(A, B), C);
    break;
  case 0x2b:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), Xor(A, C)), A);
    break;
  case 0x2c:
    if (ABCIsConst)
      Res = Nor(Xnor(A, B), Nor(B, C));
    break;
  case 0x2d:
    if (ABCIsConst)
      Res = Xor(A, Or(B, Not(C)));
    break;
  case 0x2e:
    if (ABCIsConst)
      Res = Xor(A, Or(Xor(A, C), B));
    break;
  case 0x2f:
    if (ABCIsConst)
      Res = Nand(A, Or(B, Not(C)));
    break;
  case 0x30:
    if (ABIsConst)
      Res = Nor(B, Not(A));
    break;
  case 0x31:
    if (ABCIsConst)
      Res = Nor(Nor(A, Not(C)), B);
    break;
  case 0x32:
    if (ABCIsConst)
      Res = Nor(Nor(A, C), B);
    break;
  case 0x33:
    Res = Not(B);
    break;
  case 0x34:
    if (ABCIsConst)
      Res = And(Xor(A, B), Nand(B, C));
    break;
  case 0x35:
    if (ABCIsConst)
      Res = Xor(B, Or(A, Xnor(B, C)));
    break;
  case 0x36:
    if (ABCIsConst)
      Res = Xor(Or(A, C), B);
    break;
  case 0x37:
    if (ABCIsConst)
      Res = Nand(Or(A, C), B);
    break;
  case 0x38:
    if (ABCIsConst)
      Res = Nor(Xnor(A, B), Nor(A, C));
    break;
  case 0x39:
    if (ABCIsConst)
      Res = Xor(Or(A, Not(C)), B);
    break;
  case 0x3a:
    if (ABCIsConst)
      Res = Xor(B, Or(A, Xor(B, C)));
    break;
  case 0x3b:
    if (ABCIsConst)
      Res = Nand(Or(A, Not(C)), B);
    break;
  case 0x3c:
    Res = Xor(A, B);
    break;
  case 0x3d:
    if (ABCIsConst)
      Res = Xor(A, Or(Nor(A, C), B));
    break;
  case 0x3e:
    if (ABCIsConst)
      Res = Xor(A, Or(Nor(A, Not(C)), B));
    break;
  case 0x3f:
    if (ABIsConst)
      Res = Nand(A, B);
    break;
  case 0x40:
    if (ABCIsConst)
      Res = Nor(Nand(A, B), C);
    break;
  case 0x41:
    if (ABCIsConst)
      Res = Nor(Xor(A, B), C);
    break;
  case 0x42:
    if (ABCIsConst)
      Res = Nor(Xor(A, B), Xnor(A, C));
    break;
  case 0x43:
    if (ABCIsConst)
      Res = Xor(A, Nand(Nand(A, C), B));
    break;
  case 0x44:
    if (BCIsConst)
      Res = Nor(C, Not(B));
    break;
  case 0x45:
    if (ABCIsConst)
      Res = Nor(Nor(B, Not(A)), C);
    break;
  case 0x46:
    if (ABCIsConst)
      Res = Xor(Or(And(A, C), B), C);
    break;
  case 0x47:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, C), B), C);
    break;
  case 0x48:
    if (ABCIsConst)
      Res = And(Xor(A, C), B);
    break;
  case 0x49:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), And(A, C)), C);
    break;
  case 0x4a:
    if (ABCIsConst)
      Res = Nor(Xnor(A, C), Nor(B, C));
    break;
  case 0x4b:
    if (ABCIsConst)
      Res = Xor(A, Or(C, Not(B)));
    break;
  case 0x4c:
    if (ABCIsConst)
      Res = And(Nand(A, C), B);
    break;
  case 0x4d:
    if (ABCIsConst)
      Res = Xor(Or(Xor(A, B), Xnor(A, C)), A);
    break;
  case 0x4e:
    if (ABCIsConst)
      Res = Xor(A, Or(Xor(A, B), C));
    break;
  case 0x4f:
    if (ABCIsConst)
      Res = Nand(A, Nand(B, Not(C)));
    break;
  case 0x50:
    if (ACIsConst)
      Res = Nor(C, Not(A));
    break;
  case 0x51:
    if (ABCIsConst)
      Res = Nor(Nor(A, Not(B)), C);
    break;
  case 0x52:
    if (ABCIsConst)
      Res = And(Xor(A, C), Nand(B, C));
    break;
  case 0x53:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(B, C), A), C);
    break;
  case 0x54:
    if (ABCIsConst)
      Res = Nor(Nor(A, B), C);
    break;
  case 0x55:
    Res = Not(C);
    break;
  case 0x56:
    if (ABCIsConst)
      Res = Xor(Or(A, B), C);
    break;
  case 0x57:
    if (ABCIsConst)
      Res = Nand(Or(A, B), C);
    break;
  case 0x58:
    if (ABCIsConst)
      Res = Nor(Nor(A, B), Xnor(A, C));
    break;
  case 0x59:
    if (ABCIsConst)
      Res = Xor(Or(A, Not(B)), C);
    break;
  case 0x5a:
    Res = Xor(A, C);
    break;
  case 0x5b:
    if (ABCIsConst)
      Res = Xor(A, Or(Nor(A, B), C));
    break;
  case 0x5c:
    if (ABCIsConst)
      Res = Xor(Or(Xor(B, C), A), C);
    break;
  case 0x5d:
    if (ABCIsConst)
      Res = Nand(Or(A, Not(B)), C);
    break;
  case 0x5e:
    if (ABCIsConst)
      Res = Xor(A, Or(Nor(A, Not(B)), C));
    break;
  case 0x5f:
    if (ACIsConst)
      Res = Nand(A, C);
    break;
  case 0x60:
    if (ABCIsConst)
      Res = And(A, Xor(B, C));
    break;
  case 0x61:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), And(B, C)), C);
    break;
  case 0x62:
    if (ABCIsConst)
      Res = Nor(Nor(A, C), Xnor(B, C));
    break;
  case 0x63:
    if (ABCIsConst)
      Res = Xor(B, Or(C, Not(A)));
    break;
  case 0x64:
    if (ABCIsConst)
      Res = Nor(Nor(A, B), Xnor(B, C));
    break;
  case 0x65:
    if (ABCIsConst)
      Res = Xor(Or(B, Not(A)), C);
    break;
  case 0x66:
    Res = Xor(B, C);
    break;
  case 0x67:
    if (ABCIsConst)
      Res = Or(Nor(A, B), Xor(B, C));
    break;
  case 0x68:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), Nor(Nor(A, B), C));
    break;
  case 0x69:
    if (ABCIsConst)
      Res = Xor(Xnor(A, B), C);
    break;
  case 0x6a:
    if (ABCIsConst)
      Res = Xor(And(A, B), C);
    break;
  case 0x6b:
    if (ABCIsConst)
      Res = Or(Nor(A, B), Xor(Xnor(A, B), C));
    break;
  case 0x6c:
    if (ABCIsConst)
      Res = Xor(And(A, C), B);
    break;
  case 0x6d:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), Nor(A, C)), C);
    break;
  case 0x6e:
    if (ABCIsConst)
      Res = Or(Nor(A, Not(B)), Xor(B, C));
    break;
  case 0x6f:
    if (ABCIsConst)
      Res = Nand(A, Xnor(B, C));
    break;
  case 0x70:
    if (ABCIsConst)
      Res = And(A, Nand(B, C));
    break;
  case 0x71:
    if (ABCIsConst)
      Res = Xor(Nor(Xor(A, B), Xor(A, C)), A);
    break;
  case 0x72:
    if (ABCIsConst)
      Res = Xor(Or(Xor(A, B), C), B);
    break;
  case 0x73:
    if (ABCIsConst)
      Res = Nand(Nand(A, Not(C)), B);
    break;
  case 0x74:
    if (ABCIsConst)
      Res = Xor(Or(Xor(A, C), B), C);
    break;
  case 0x75:
    if (ABCIsConst)
      Res = Nand(Nand(A, Not(B)), C);
    break;
  case 0x76:
    if (ABCIsConst)
      Res = Xor(B, Or(Nor(B, Not(A)), C));
    break;
  case 0x77:
    if (BCIsConst)
      Res = Nand(B, C);
    break;
  case 0x78:
    if (ABCIsConst)
      Res = Xor(A, And(B, C));
    break;
  case 0x79:
    if (ABCIsConst)
      Res = Xor(Or(Xnor(A, B), Nor(B, C)), C);
    break;
  case 0x7a:
    if (ABCIsConst)
      Res = Or(Xor(A, C), Nor(B, Not(A)));
    break;
  case 0x7b:
    if (ABCIsConst)
      Res = Nand(Xnor(A, C), B);
    break;
  case 0x7c:
    if (ABCIsConst)
      Res = Or(Xor(A, B), Nor(C, Not(A)));
    break;
  case 0x7d:
    if (ABCIsConst)
      Res = Nand(Xnor(A, B), C);
    break;
  case 0x7e:
    if (ABCIsConst)
      Res = Or(Xor(A, B), Xor(A, C));
    break;
  case 0x7f:
    if (ABCIsConst)
      Res = Nand(And(A, B), C);
    break;
  case 0x80:
    if (ABCIsConst)
      Res = And(And(A, B), C);
    break;
  case 0x81:
    if (ABCIsConst)
      Res = Nor(Xor(A, B), Xor(A, C));
    break;
  case 0x82:
    if (ABCIsConst)
      Res = And(Xnor(A, B), C);
    break;
  case 0x83:
    if (ABCIsConst)
      Res = Nor(Xor(A, B), Nor(C, Not(A)));
    break;
  case 0x84:
    if (ABCIsConst)
      Res = And(Xnor(A, C), B);
    break;
  case 0x85:
    if (ABCIsConst)
      Res = Nor(Xor(A, C), Nor(B, Not(A)));
    break;
  case 0x86:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(A, B), Nor(B, C)), C);
    break;
  case 0x87:
    if (ABCIsConst)
      Res = Xor(A, Nand(B, C));
    break;
  case 0x88:
    Res = And(B, C);
    break;
  case 0x89:
    if (ABCIsConst)
      Res = Xor(B, Nor(Nor(B, Not(A)), C));
    break;
  case 0x8a:
    if (ABCIsConst)
      Res = And(Nand(A, Not(B)), C);
    break;
  case 0x8b:
    if (ABCIsConst)
      Res = Xor(Nor(Xor(A, C), B), C);
    break;
  case 0x8c:
    if (ABCIsConst)
      Res = And(Nand(A, Not(C)), B);
    break;
  case 0x8d:
    if (ABCIsConst)
      Res = Xor(Nor(Xor(A, B), C), B);
    break;
  case 0x8e:
    if (ABCIsConst)
      Res = Xor(Or(Xor(A, B), Xor(A, C)), A);
    break;
  case 0x8f:
    if (ABCIsConst)
      Res = Nand(A, Nand(B, C));
    break;
  case 0x90:
    if (ABCIsConst)
      Res = And(A, Xnor(B, C));
    break;
  case 0x91:
    if (ABCIsConst)
      Res = Nor(Nor(A, Not(B)), Xor(B, C));
    break;
  case 0x92:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(A, B), Nor(A, C)), C);
    break;
  case 0x93:
    if (ABCIsConst)
      Res = Xor(Nand(A, C), B);
    break;
  case 0x94:
    if (ABCIsConst)
      Res = Nor(Nor(A, B), Xor(Xnor(A, B), C));
    break;
  case 0x95:
    if (ABCIsConst)
      Res = Xor(Nand(A, B), C);
    break;
  case 0x96:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), C);
    break;
  case 0x97:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), Or(Nor(A, B), C));
    break;
  case 0x98:
    if (ABCIsConst)
      Res = Nor(Nor(A, B), Xor(B, C));
    break;
  case 0x99:
    if (BCIsConst)
      Res = Xnor(B, C);
    break;
  case 0x9a:
    if (ABCIsConst)
      Res = Xor(Nor(B, Not(A)), C);
    break;
  case 0x9b:
    if (ABCIsConst)
      Res = Or(Nor(A, B), Xnor(B, C));
    break;
  case 0x9c:
    if (ABCIsConst)
      Res = Xor(B, Nor(C, Not(A)));
    break;
  case 0x9d:
    if (ABCIsConst)
      Res = Or(Nor(A, C), Xnor(B, C));
    break;
  case 0x9e:
    if (ABCIsConst)
      Res = Xor(And(Xor(A, B), Nand(B, C)), C);
    break;
  case 0x9f:
    if (ABCIsConst)
      Res = Nand(A, Xor(B, C));
    break;
  case 0xa0:
    Res = And(A, C);
    break;
  case 0xa1:
    if (ABCIsConst)
      Res = Xor(A, Nor(Nor(A, Not(B)), C));
    break;
  case 0xa2:
    if (ABCIsConst)
      Res = And(Or(A, Not(B)), C);
    break;
  case 0xa3:
    if (ABCIsConst)
      Res = Xor(Nor(Xor(B, C), A), C);
    break;
  case 0xa4:
    if (ABCIsConst)
      Res = Xor(A, Nor(Nor(A, B), C));
    break;
  case 0xa5:
    if (ACIsConst)
      Res = Xnor(A, C);
    break;
  case 0xa6:
    if (ABCIsConst)
      Res = Xor(Nor(A, Not(B)), C);
    break;
  case 0xa7:
    if (ABCIsConst)
      Res = Or(Nor(A, B), Xnor(A, C));
    break;
  case 0xa8:
    if (ABCIsConst)
      Res = And(Or(A, B), C);
    break;
  case 0xa9:
    if (ABCIsConst)
      Res = Xor(Nor(A, B), C);
    break;
  case 0xaa:
    Res = C;
    break;
  case 0xab:
    if (ABCIsConst)
      Res = Or(Nor(A, B), C);
    break;
  case 0xac:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(B, C), A), C);
    break;
  case 0xad:
    if (ABCIsConst)
      Res = Or(Xnor(A, C), And(B, C));
    break;
  case 0xae:
    if (ABCIsConst)
      Res = Or(Nor(A, Not(B)), C);
    break;
  case 0xaf:
    if (ACIsConst)
      Res = Or(C, Not(A));
    break;
  case 0xb0:
    if (ABCIsConst)
      Res = And(A, Nand(B, Not(C)));
    break;
  case 0xb1:
    if (ABCIsConst)
      Res = Xor(A, Nor(Xor(A, B), C));
    break;
  case 0xb2:
    if (ABCIsConst)
      Res = Xor(Nor(Xor(A, B), Xnor(A, C)), A);
    break;
  case 0xb3:
    if (ABCIsConst)
      Res = Nand(Nand(A, C), B);
    break;
  case 0xb4:
    if (ABCIsConst)
      Res = Xor(A, Nor(C, Not(B)));
    break;
  case 0xb5:
    if (ABCIsConst)
      Res = Or(Xnor(A, C), Nor(B, C));
    break;
  case 0xb6:
    if (ABCIsConst)
      Res = Xor(And(Xor(A, B), Nand(A, C)), C);
    break;
  case 0xb7:
    if (ABCIsConst)
      Res = Nand(Xor(A, C), B);
    break;
  case 0xb8:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(A, C), B), C);
    break;
  case 0xb9:
    if (ABCIsConst)
      Res = Xor(Nor(And(A, C), B), C);
    break;
  case 0xba:
    if (ABCIsConst)
      Res = Or(Nor(B, Not(A)), C);
    break;
  case 0xbb:
    if (BCIsConst)
      Res = Or(C, Not(B));
    break;
  case 0xbc:
    if (ABCIsConst)
      Res = Xor(A, And(Nand(A, C), B));
    break;
  case 0xbd:
    if (ABCIsConst)
      Res = Or(Xor(A, B), Xnor(A, C));
    break;
  case 0xbe:
    if (ABCIsConst)
      Res = Or(Xor(A, B), C);
    break;
  case 0xbf:
    if (ABCIsConst)
      Res = Or(Nand(A, B), C);
    break;
  case 0xc0:
    Res = And(A, B);
    break;
  case 0xc1:
    if (ABCIsConst)
      Res = Xor(A, Nor(Nor(A, Not(C)), B));
    break;
  case 0xc2:
    if (ABCIsConst)
      Res = Xor(A, Nor(Nor(A, C), B));
    break;
  case 0xc3:
    if (ABIsConst)
      Res = Xnor(A, B);
    break;
  case 0xc4:
    if (ABCIsConst)
      Res = And(Or(A, Not(C)), B);
    break;
  case 0xc5:
    if (ABCIsConst)
      Res = Xor(B, Nor(A, Xor(B, C)));
    break;
  case 0xc6:
    if (ABCIsConst)
      Res = Xor(Nor(A, Not(C)), B);
    break;
  case 0xc7:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), Nor(A, C));
    break;
  case 0xc8:
    if (ABCIsConst)
      Res = And(Or(A, C), B);
    break;
  case 0xc9:
    if (ABCIsConst)
      Res = Xor(Nor(A, C), B);
    break;
  case 0xca:
    if (ABCIsConst)
      Res = Xor(B, Nor(A, Xnor(B, C)));
    break;
  case 0xcb:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), And(B, C));
    break;
  case 0xcc:
    Res = B;
    break;
  case 0xcd:
    if (ABCIsConst)
      Res = Or(Nor(A, C), B);
    break;
  case 0xce:
    if (ABCIsConst)
      Res = Or(Nor(A, Not(C)), B);
    break;
  case 0xcf:
    if (ABIsConst)
      Res = Or(B, Not(A));
    break;
  case 0xd0:
    if (ABCIsConst)
      Res = And(A, Or(B, Not(C)));
    break;
  case 0xd1:
    if (ABCIsConst)
      Res = Xor(A, Nor(Xor(A, C), B));
    break;
  case 0xd2:
    if (ABCIsConst)
      Res = Xor(A, Nor(B, Not(C)));
    break;
  case 0xd3:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), Nor(B, C));
    break;
  case 0xd4:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(A, B), Xor(A, C)), A);
    break;
  case 0xd5:
    if (ABCIsConst)
      Res = Nand(Nand(A, B), C);
    break;
  case 0xd6:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), Or(And(A, B), C));
    break;
  case 0xd7:
    if (ABCIsConst)
      Res = Nand(Xor(A, B), C);
    break;
  case 0xd8:
    if (ABCIsConst)
      Res = Xor(Nor(Xnor(A, B), C), B);
    break;
  case 0xd9:
    if (ABCIsConst)
      Res = Or(And(A, B), Xnor(B, C));
    break;
  case 0xda:
    if (ABCIsConst)
      Res = Xor(A, And(Nand(A, B), C));
    break;
  case 0xdb:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), Xor(A, C));
    break;
  case 0xdc:
    if (ABCIsConst)
      Res = Or(B, Nor(C, Not(A)));
    break;
  case 0xdd:
    if (BCIsConst)
      Res = Or(B, Not(C));
    break;
  case 0xde:
    if (ABCIsConst)
      Res = Or(Xor(A, C), B);
    break;
  case 0xdf:
    if (ABCIsConst)
      Res = Or(Nand(A, C), B);
    break;
  case 0xe0:
    if (ABCIsConst)
      Res = And(A, Or(B, C));
    break;
  case 0xe1:
    if (ABCIsConst)
      Res = Xor(A, Nor(B, C));
    break;
  case 0xe2:
    if (ABCIsConst)
      Res = Xor(A, Nor(Xnor(A, C), B));
    break;
  case 0xe3:
    if (ABCIsConst)
      Res = Xor(A, Nor(And(A, C), B));
    break;
  case 0xe4:
    if (ABCIsConst)
      Res = Xor(A, Nor(Xnor(A, B), C));
    break;
  case 0xe5:
    if (ABCIsConst)
      Res = Xor(A, Nor(And(A, B), C));
    break;
  case 0xe6:
    if (ABCIsConst)
      Res = Or(And(A, B), Xor(B, C));
    break;
  case 0xe7:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), Xnor(A, C));
    break;
  case 0xe8:
    if (ABCIsConst)
      Res = Xor(Or(A, B), Nor(Xnor(A, B), C));
    break;
  case 0xe9:
    if (ABCIsConst)
      Res = Xor(Xor(A, B), Nand(Nand(A, B), C));
    break;
  case 0xea:
    if (ABCIsConst)
      Res = Or(And(A, B), C);
    break;
  case 0xeb:
    if (ABCIsConst)
      Res = Or(Xnor(A, B), C);
    break;
  case 0xec:
    if (ABCIsConst)
      Res = Or(And(A, C), B);
    break;
  case 0xed:
    if (ABCIsConst)
      Res = Or(Xnor(A, C), B);
    break;
  case 0xee:
    Res = Or(B, C);
    break;
  case 0xef:
    if (ABCIsConst)
      Res = Nand(A, Nor(B, C));
    break;
  case 0xf0:
    Res = A;
    break;
  case 0xf1:
    if (ABCIsConst)
      Res = Or(A, Nor(B, C));
    break;
  case 0xf2:
    if (ABCIsConst)
      Res = Or(A, Nor(B, Not(C)));
    break;
  case 0xf3:
    if (ABIsConst)
      Res = Or(A, Not(B));
    break;
  case 0xf4:
    if (ABCIsConst)
      Res = Or(A, Nor(C, Not(B)));
    break;
  case 0xf5:
    if (ACIsConst)
      Res = Or(A, Not(C));
    break;
  case 0xf6:
    if (ABCIsConst)
      Res = Or(A, Xor(B, C));
    break;
  case 0xf7:
    if (ABCIsConst)
      Res = Or(A, Nand(B, C));
    break;
  case 0xf8:
    if (ABCIsConst)
      Res = Or(A, And(B, C));
    break;
  case 0xf9:
    if (ABCIsConst)
      Res = Or(A, Xnor(B, C));
    break;
  case 0xfa:
    Res = Or(A, C);
    break;
  case 0xfb:
    if (ABCIsConst)
      Res = Nand(Nor(A, C), B);
    break;
  case 0xfc:
    Res = Or(A, B);
    break;
  case 0xfd:
    if (ABCIsConst)
      Res = Nand(Nor(A, B), C);
    break;
  case 0xfe:
    if (ABCIsConst)
      Res = Or(Or(A, B), C);
    break;
  case 0xff:
    Res = {Constant::getAllOnesValue(Ty), 0xff};
    break;
  }

  assert((Res.first == nullptr || Res.second == Imm) &&
         "Simplification of ternary logic does not verify!");
  return Res.first;
}

static Value *simplifyX86insertps(const IntrinsicInst &II,
                                  InstCombiner::BuilderTy &Builder) {
  auto *CInt = dyn_cast<ConstantInt>(II.getArgOperand(2));
  if (!CInt)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  assert(VecTy->getNumElements() == 4 && "insertps with wrong vector type");

  // The immediate permute control byte looks like this:
  //    [3:0] - zero mask for each 32-bit lane
  //    [5:4] - select one 32-bit destination lane
  //    [7:6] - select one 32-bit source lane

  uint8_t Imm = CInt->getZExtValue();
  uint8_t ZMask = Imm & 0xf;
  uint8_t DestLane = (Imm >> 4) & 0x3;
  uint8_t SourceLane = (Imm >> 6) & 0x3;

  ConstantAggregateZero *ZeroVector = ConstantAggregateZero::get(VecTy);

  // If all zero mask bits are set, this was just a weird way to
  // generate a zero vector.
  if (ZMask == 0xf)
    return ZeroVector;

  // Initialize by passing all of the first source bits through.
  int ShuffleMask[4] = {0, 1, 2, 3};

  // We may replace the second operand with the zero vector.
  Value *V1 = II.getArgOperand(1);

  if (ZMask) {
    // If the zero mask is being used with a single input or the zero mask
    // overrides the destination lane, this is a shuffle with the zero vector.
    if ((II.getArgOperand(0) == II.getArgOperand(1)) ||
        (ZMask & (1 << DestLane))) {
      V1 = ZeroVector;
      // We may still move 32-bits of the first source vector from one lane
      // to another.
      ShuffleMask[DestLane] = SourceLane;
      // The zero mask may override the previous insert operation.
      for (unsigned i = 0; i < 4; ++i)
        if ((ZMask >> i) & 0x1)
          ShuffleMask[i] = i + 4;
    } else {
      // TODO: Model this case as 2 shuffles or a 'logical and' plus shuffle?
      return nullptr;
    }
  } else {
    // Replace the selected destination lane with the selected source lane.
    ShuffleMask[DestLane] = SourceLane + 4;
  }

  return Builder.CreateShuffleVector(II.getArgOperand(0), V1, ShuffleMask);
}

/// Attempt to simplify SSE4A EXTRQ/EXTRQI instructions using constant folding
/// or conversion to a shuffle vector.
static Value *simplifyX86extrq(IntrinsicInst &II, Value *Op0,
                               ConstantInt *CILength, ConstantInt *CIIndex,
                               InstCombiner::BuilderTy &Builder) {
  auto LowConstantHighUndef = [&](uint64_t Val) {
    Type *IntTy64 = Type::getInt64Ty(II.getContext());
    Constant *Args[] = {ConstantInt::get(IntTy64, Val),
                        UndefValue::get(IntTy64)};
    return ConstantVector::get(Args);
  };

  // See if we're dealing with constant values.
  auto *C0 = dyn_cast<Constant>(Op0);
  auto *CI0 =
      C0 ? dyn_cast_or_null<ConstantInt>(C0->getAggregateElement((unsigned)0))
         : nullptr;

  // Attempt to constant fold.
  if (CILength && CIIndex) {
    // From AMD documentation: "The bit index and field length are each six
    // bits in length other bits of the field are ignored."
    APInt APIndex = CIIndex->getValue().zextOrTrunc(6);
    APInt APLength = CILength->getValue().zextOrTrunc(6);

    unsigned Index = APIndex.getZExtValue();

    // From AMD documentation: "a value of zero in the field length is
    // defined as length of 64".
    unsigned Length = APLength == 0 ? 64 : APLength.getZExtValue();

    // From AMD documentation: "If the sum of the bit index + length field
    // is greater than 64, the results are undefined".
    unsigned End = Index + Length;

    // Note that both field index and field length are 8-bit quantities.
    // Since variables 'Index' and 'Length' are unsigned values
    // obtained from zero-extending field index and field length
    // respectively, their sum should never wrap around.
    if (End > 64)
      return UndefValue::get(II.getType());

    // If we are inserting whole bytes, we can convert this to a shuffle.
    // Lowering can recognize EXTRQI shuffle masks.
    if ((Length % 8) == 0 && (Index % 8) == 0) {
      // Convert bit indices to byte indices.
      Length /= 8;
      Index /= 8;

      Type *IntTy8 = Type::getInt8Ty(II.getContext());
      auto *ShufTy = FixedVectorType::get(IntTy8, 16);

      SmallVector<int, 16> ShuffleMask;
      for (int i = 0; i != (int)Length; ++i)
        ShuffleMask.push_back(i + Index);
      for (int i = Length; i != 8; ++i)
        ShuffleMask.push_back(i + 16);
      for (int i = 8; i != 16; ++i)
        ShuffleMask.push_back(-1);

      Value *SV = Builder.CreateShuffleVector(
          Builder.CreateBitCast(Op0, ShufTy),
          ConstantAggregateZero::get(ShufTy), ShuffleMask);
      return Builder.CreateBitCast(SV, II.getType());
    }

    // Constant Fold - shift Index'th bit to lowest position and mask off
    // Length bits.
    if (CI0) {
      APInt Elt = CI0->getValue();
      Elt.lshrInPlace(Index);
      Elt = Elt.zextOrTrunc(Length);
      return LowConstantHighUndef(Elt.getZExtValue());
    }

    // If we were an EXTRQ call, we'll save registers if we convert to EXTRQI.
    if (II.getIntrinsicID() == Intrinsic::x86_sse4a_extrq) {
      Value *Args[] = {Op0, CILength, CIIndex};
      Module *M = II.getModule();
      Function *F = Intrinsic::getDeclaration(M, Intrinsic::x86_sse4a_extrqi);
      return Builder.CreateCall(F, Args);
    }
  }

  // Constant Fold - extraction from zero is always {zero, undef}.
  if (CI0 && CI0->isZero())
    return LowConstantHighUndef(0);

  return nullptr;
}

/// Attempt to simplify SSE4A INSERTQ/INSERTQI instructions using constant
/// folding or conversion to a shuffle vector.
static Value *simplifyX86insertq(IntrinsicInst &II, Value *Op0, Value *Op1,
                                 APInt APLength, APInt APIndex,
                                 InstCombiner::BuilderTy &Builder) {
  // From AMD documentation: "The bit index and field length are each six bits
  // in length other bits of the field are ignored."
  APIndex = APIndex.zextOrTrunc(6);
  APLength = APLength.zextOrTrunc(6);

  // Attempt to constant fold.
  unsigned Index = APIndex.getZExtValue();

  // From AMD documentation: "a value of zero in the field length is
  // defined as length of 64".
  unsigned Length = APLength == 0 ? 64 : APLength.getZExtValue();

  // From AMD documentation: "If the sum of the bit index + length field
  // is greater than 64, the results are undefined".
  unsigned End = Index + Length;

  // Note that both field index and field length are 8-bit quantities.
  // Since variables 'Index' and 'Length' are unsigned values
  // obtained from zero-extending field index and field length
  // respectively, their sum should never wrap around.
  if (End > 64)
    return UndefValue::get(II.getType());

  // If we are inserting whole bytes, we can convert this to a shuffle.
  // Lowering can recognize INSERTQI shuffle masks.
  if ((Length % 8) == 0 && (Index % 8) == 0) {
    // Convert bit indices to byte indices.
    Length /= 8;
    Index /= 8;

    Type *IntTy8 = Type::getInt8Ty(II.getContext());
    auto *ShufTy = FixedVectorType::get(IntTy8, 16);

    SmallVector<int, 16> ShuffleMask;
    for (int i = 0; i != (int)Index; ++i)
      ShuffleMask.push_back(i);
    for (int i = 0; i != (int)Length; ++i)
      ShuffleMask.push_back(i + 16);
    for (int i = Index + Length; i != 8; ++i)
      ShuffleMask.push_back(i);
    for (int i = 8; i != 16; ++i)
      ShuffleMask.push_back(-1);

    Value *SV = Builder.CreateShuffleVector(Builder.CreateBitCast(Op0, ShufTy),
                                            Builder.CreateBitCast(Op1, ShufTy),
                                            ShuffleMask);
    return Builder.CreateBitCast(SV, II.getType());
  }

  // See if we're dealing with constant values.
  auto *C0 = dyn_cast<Constant>(Op0);
  auto *C1 = dyn_cast<Constant>(Op1);
  auto *CI00 =
      C0 ? dyn_cast_or_null<ConstantInt>(C0->getAggregateElement((unsigned)0))
         : nullptr;
  auto *CI10 =
      C1 ? dyn_cast_or_null<ConstantInt>(C1->getAggregateElement((unsigned)0))
         : nullptr;

  // Constant Fold - insert bottom Length bits starting at the Index'th bit.
  if (CI00 && CI10) {
    APInt V00 = CI00->getValue();
    APInt V10 = CI10->getValue();
    APInt Mask = APInt::getLowBitsSet(64, Length).shl(Index);
    V00 = V00 & ~Mask;
    V10 = V10.zextOrTrunc(Length).zextOrTrunc(64).shl(Index);
    APInt Val = V00 | V10;
    Type *IntTy64 = Type::getInt64Ty(II.getContext());
    Constant *Args[] = {ConstantInt::get(IntTy64, Val.getZExtValue()),
                        UndefValue::get(IntTy64)};
    return ConstantVector::get(Args);
  }

  // If we were an INSERTQ call, we'll save demanded elements if we convert to
  // INSERTQI.
  if (II.getIntrinsicID() == Intrinsic::x86_sse4a_insertq) {
    Type *IntTy8 = Type::getInt8Ty(II.getContext());
    Constant *CILength = ConstantInt::get(IntTy8, Length, false);
    Constant *CIIndex = ConstantInt::get(IntTy8, Index, false);

    Value *Args[] = {Op0, Op1, CILength, CIIndex};
    Module *M = II.getModule();
    Function *F = Intrinsic::getDeclaration(M, Intrinsic::x86_sse4a_insertqi);
    return Builder.CreateCall(F, Args);
  }

  return nullptr;
}

/// Attempt to convert pshufb* to shufflevector if the mask is constant.
static Value *simplifyX86pshufb(const IntrinsicInst &II,
                                InstCombiner::BuilderTy &Builder) {
  auto *V = dyn_cast<Constant>(II.getArgOperand(1));
  if (!V)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  unsigned NumElts = VecTy->getNumElements();
  assert((NumElts == 16 || NumElts == 32 || NumElts == 64) &&
         "Unexpected number of elements in shuffle mask!");

  // Construct a shuffle mask from constant integers or UNDEFs.
  int Indexes[64];

  // Each byte in the shuffle control mask forms an index to permute the
  // corresponding byte in the destination operand.
  for (unsigned I = 0; I < NumElts; ++I) {
    Constant *COp = V->getAggregateElement(I);
    if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
      return nullptr;

    if (isa<UndefValue>(COp)) {
      Indexes[I] = -1;
      continue;
    }

    int8_t Index = cast<ConstantInt>(COp)->getValue().getZExtValue();

    // If the most significant bit (bit[7]) of each byte of the shuffle
    // control mask is set, then zero is written in the result byte.
    // The zero vector is in the right-hand side of the resulting
    // shufflevector.

    // The value of each index for the high 128-bit lane is the least
    // significant 4 bits of the respective shuffle control byte.
    Index = ((Index < 0) ? NumElts : Index & 0x0F) + (I & 0xF0);
    Indexes[I] = Index;
  }

  auto V1 = II.getArgOperand(0);
  auto V2 = Constant::getNullValue(VecTy);
  return Builder.CreateShuffleVector(V1, V2, ArrayRef(Indexes, NumElts));
}

/// Attempt to convert vpermilvar* to shufflevector if the mask is constant.
static Value *simplifyX86vpermilvar(const IntrinsicInst &II,
                                    InstCombiner::BuilderTy &Builder) {
  auto *V = dyn_cast<Constant>(II.getArgOperand(1));
  if (!V)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  unsigned NumElts = VecTy->getNumElements();
  bool IsPD = VecTy->getScalarType()->isDoubleTy();
  unsigned NumLaneElts = IsPD ? 2 : 4;
  assert(NumElts == 16 || NumElts == 8 || NumElts == 4 || NumElts == 2);

  // Construct a shuffle mask from constant integers or UNDEFs.
  int Indexes[16];

  // The intrinsics only read one or two bits, clear the rest.
  for (unsigned I = 0; I < NumElts; ++I) {
    Constant *COp = V->getAggregateElement(I);
    if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
      return nullptr;

    if (isa<UndefValue>(COp)) {
      Indexes[I] = -1;
      continue;
    }

    APInt Index = cast<ConstantInt>(COp)->getValue();
    Index = Index.zextOrTrunc(32).getLoBits(2);

    // The PD variants uses bit 1 to select per-lane element index, so
    // shift down to convert to generic shuffle mask index.
    if (IsPD)
      Index.lshrInPlace(1);

    // The _256 variants are a bit trickier since the mask bits always index
    // into the corresponding 128 half. In order to convert to a generic
    // shuffle, we have to make that explicit.
    Index += APInt(32, (I / NumLaneElts) * NumLaneElts);

    Indexes[I] = Index.getZExtValue();
  }

  auto V1 = II.getArgOperand(0);
  return Builder.CreateShuffleVector(V1, ArrayRef(Indexes, NumElts));
}

/// Attempt to convert vpermd/vpermps to shufflevector if the mask is constant.
static Value *simplifyX86vpermv(const IntrinsicInst &II,
                                InstCombiner::BuilderTy &Builder) {
  auto *V = dyn_cast<Constant>(II.getArgOperand(1));
  if (!V)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  unsigned Size = VecTy->getNumElements();
  assert((Size == 4 || Size == 8 || Size == 16 || Size == 32 || Size == 64) &&
         "Unexpected shuffle mask size");

  // Construct a shuffle mask from constant integers or UNDEFs.
  int Indexes[64];

  for (unsigned I = 0; I < Size; ++I) {
    Constant *COp = V->getAggregateElement(I);
    if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
      return nullptr;

    if (isa<UndefValue>(COp)) {
      Indexes[I] = -1;
      continue;
    }

    uint32_t Index = cast<ConstantInt>(COp)->getZExtValue();
    Index &= Size - 1;
    Indexes[I] = Index;
  }

  auto V1 = II.getArgOperand(0);
  return Builder.CreateShuffleVector(V1, ArrayRef(Indexes, Size));
}

/// Attempt to convert vpermi2/vpermt2 to shufflevector if the mask is constant.
static Value *simplifyX86vpermv3(const IntrinsicInst &II,
                                 InstCombiner::BuilderTy &Builder) {
  auto *V = dyn_cast<Constant>(II.getArgOperand(1));
  if (!V)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  unsigned Size = VecTy->getNumElements();
  assert((Size == 2 || Size == 4 || Size == 8 || Size == 16 || Size == 32 ||
          Size == 64) &&
         "Unexpected shuffle mask size");

  // Construct a shuffle mask from constant integers or UNDEFs.
  int Indexes[64];

  for (unsigned I = 0; I < Size; ++I) {
    Constant *COp = V->getAggregateElement(I);
    if (!COp || (!isa<UndefValue>(COp) && !isa<ConstantInt>(COp)))
      return nullptr;

    if (isa<UndefValue>(COp)) {
      Indexes[I] = -1;
      continue;
    }

    uint32_t Index = cast<ConstantInt>(COp)->getZExtValue();
    Index &= (2 * Size) - 1;
    Indexes[I] = Index;
  }

  auto V1 = II.getArgOperand(0);
  auto V2 = II.getArgOperand(2);
  return Builder.CreateShuffleVector(V1, V2, ArrayRef(Indexes, Size));
}

std::optional<Instruction *>
X86TTIImpl::instCombineIntrinsic(InstCombiner &IC, IntrinsicInst &II) const {
  auto SimplifyDemandedVectorEltsLow = [&IC](Value *Op, unsigned Width,
                                             unsigned DemandedWidth) {
    APInt UndefElts(Width, 0);
    APInt DemandedElts = APInt::getLowBitsSet(Width, DemandedWidth);
    return IC.SimplifyDemandedVectorElts(Op, DemandedElts, UndefElts);
  };

  Intrinsic::ID IID = II.getIntrinsicID();
  switch (IID) {
  case Intrinsic::x86_bmi_bextr_32:
  case Intrinsic::x86_bmi_bextr_64:
  case Intrinsic::x86_tbm_bextri_u32:
  case Intrinsic::x86_tbm_bextri_u64:
    // If the RHS is a constant we can try some simplifications.
    if (auto *C = dyn_cast<ConstantInt>(II.getArgOperand(1))) {
      uint64_t Shift = C->getZExtValue();
      uint64_t Length = (Shift >> 8) & 0xff;
      Shift &= 0xff;
      unsigned BitWidth = II.getType()->getIntegerBitWidth();
      // If the length is 0 or the shift is out of range, replace with zero.
      if (Length == 0 || Shift >= BitWidth) {
        return IC.replaceInstUsesWith(II, ConstantInt::get(II.getType(), 0));
      }
      // If the LHS is also a constant, we can completely constant fold this.
      if (auto *InC = dyn_cast<ConstantInt>(II.getArgOperand(0))) {
        uint64_t Result = InC->getZExtValue() >> Shift;
        if (Length > BitWidth)
          Length = BitWidth;
        Result &= maskTrailingOnes<uint64_t>(Length);
        return IC.replaceInstUsesWith(II,
                                      ConstantInt::get(II.getType(), Result));
      }
      // TODO should we turn this into 'and' if shift is 0? Or 'shl' if we
      // are only masking bits that a shift already cleared?
    }
    break;

  case Intrinsic::x86_bmi_bzhi_32:
  case Intrinsic::x86_bmi_bzhi_64:
    // If the RHS is a constant we can try some simplifications.
    if (auto *C = dyn_cast<ConstantInt>(II.getArgOperand(1))) {
      uint64_t Index = C->getZExtValue() & 0xff;
      unsigned BitWidth = II.getType()->getIntegerBitWidth();
      if (Index >= BitWidth) {
        return IC.replaceInstUsesWith(II, II.getArgOperand(0));
      }
      if (Index == 0) {
        return IC.replaceInstUsesWith(II, ConstantInt::get(II.getType(), 0));
      }
      // If the LHS is also a constant, we can completely constant fold this.
      if (auto *InC = dyn_cast<ConstantInt>(II.getArgOperand(0))) {
        uint64_t Result = InC->getZExtValue();
        Result &= maskTrailingOnes<uint64_t>(Index);
        return IC.replaceInstUsesWith(II,
                                      ConstantInt::get(II.getType(), Result));
      }
      // TODO should we convert this to an AND if the RHS is constant?
    }
    break;
  case Intrinsic::x86_bmi_pext_32:
  case Intrinsic::x86_bmi_pext_64:
    if (auto *MaskC = dyn_cast<ConstantInt>(II.getArgOperand(1))) {
      if (MaskC->isNullValue()) {
        return IC.replaceInstUsesWith(II, ConstantInt::get(II.getType(), 0));
      }
      if (MaskC->isAllOnesValue()) {
        return IC.replaceInstUsesWith(II, II.getArgOperand(0));
      }

      unsigned MaskIdx, MaskLen;
      if (MaskC->getValue().isShiftedMask(MaskIdx, MaskLen)) {
        // any single contingous sequence of 1s anywhere in the mask simply
        // describes a subset of the input bits shifted to the appropriate
        // position.  Replace with the straight forward IR.
        Value *Input = II.getArgOperand(0);
        Value *Masked = IC.Builder.CreateAnd(Input, II.getArgOperand(1));
        Value *ShiftAmt = ConstantInt::get(II.getType(), MaskIdx);
        Value *Shifted = IC.Builder.CreateLShr(Masked, ShiftAmt);
        return IC.replaceInstUsesWith(II, Shifted);
      }

      if (auto *SrcC = dyn_cast<ConstantInt>(II.getArgOperand(0))) {
        uint64_t Src = SrcC->getZExtValue();
        uint64_t Mask = MaskC->getZExtValue();
        uint64_t Result = 0;
        uint64_t BitToSet = 1;

        while (Mask) {
          // Isolate lowest set bit.
          uint64_t BitToTest = Mask & -Mask;
          if (BitToTest & Src)
            Result |= BitToSet;

          BitToSet <<= 1;
          // Clear lowest set bit.
          Mask &= Mask - 1;
        }

        return IC.replaceInstUsesWith(II,
                                      ConstantInt::get(II.getType(), Result));
      }
    }
    break;
  case Intrinsic::x86_bmi_pdep_32:
  case Intrinsic::x86_bmi_pdep_64:
    if (auto *MaskC = dyn_cast<ConstantInt>(II.getArgOperand(1))) {
      if (MaskC->isNullValue()) {
        return IC.replaceInstUsesWith(II, ConstantInt::get(II.getType(), 0));
      }
      if (MaskC->isAllOnesValue()) {
        return IC.replaceInstUsesWith(II, II.getArgOperand(0));
      }

      unsigned MaskIdx, MaskLen;
      if (MaskC->getValue().isShiftedMask(MaskIdx, MaskLen)) {
        // any single contingous sequence of 1s anywhere in the mask simply
        // describes a subset of the input bits shifted to the appropriate
        // position.  Replace with the straight forward IR.
        Value *Input = II.getArgOperand(0);
        Value *ShiftAmt = ConstantInt::get(II.getType(), MaskIdx);
        Value *Shifted = IC.Builder.CreateShl(Input, ShiftAmt);
        Value *Masked = IC.Builder.CreateAnd(Shifted, II.getArgOperand(1));
        return IC.replaceInstUsesWith(II, Masked);
      }

      if (auto *SrcC = dyn_cast<ConstantInt>(II.getArgOperand(0))) {
        uint64_t Src = SrcC->getZExtValue();
        uint64_t Mask = MaskC->getZExtValue();
        uint64_t Result = 0;
        uint64_t BitToTest = 1;

        while (Mask) {
          // Isolate lowest set bit.
          uint64_t BitToSet = Mask & -Mask;
          if (BitToTest & Src)
            Result |= BitToSet;

          BitToTest <<= 1;
          // Clear lowest set bit;
          Mask &= Mask - 1;
        }

        return IC.replaceInstUsesWith(II,
                                      ConstantInt::get(II.getType(), Result));
      }
    }
    break;

  case Intrinsic::x86_sse_cvtss2si:
  case Intrinsic::x86_sse_cvtss2si64:
  case Intrinsic::x86_sse_cvttss2si:
  case Intrinsic::x86_sse_cvttss2si64:
  case Intrinsic::x86_sse2_cvtsd2si:
  case Intrinsic::x86_sse2_cvtsd2si64:
  case Intrinsic::x86_sse2_cvttsd2si:
  case Intrinsic::x86_sse2_cvttsd2si64:
  case Intrinsic::x86_avx512_vcvtss2si32:
  case Intrinsic::x86_avx512_vcvtss2si64:
  case Intrinsic::x86_avx512_vcvtss2usi32:
  case Intrinsic::x86_avx512_vcvtss2usi64:
  case Intrinsic::x86_avx512_vcvtsd2si32:
  case Intrinsic::x86_avx512_vcvtsd2si64:
  case Intrinsic::x86_avx512_vcvtsd2usi32:
  case Intrinsic::x86_avx512_vcvtsd2usi64:
  case Intrinsic::x86_avx512_cvttss2si:
  case Intrinsic::x86_avx512_cvttss2si64:
  case Intrinsic::x86_avx512_cvttss2usi:
  case Intrinsic::x86_avx512_cvttss2usi64:
  case Intrinsic::x86_avx512_cvttsd2si:
  case Intrinsic::x86_avx512_cvttsd2si64:
  case Intrinsic::x86_avx512_cvttsd2usi:
  case Intrinsic::x86_avx512_cvttsd2usi64: {
    // These intrinsics only demand the 0th element of their input vectors. If
    // we can simplify the input based on that, do so now.
    Value *Arg = II.getArgOperand(0);
    unsigned VWidth = cast<FixedVectorType>(Arg->getType())->getNumElements();
    if (Value *V = SimplifyDemandedVectorEltsLow(Arg, VWidth, 1)) {
      return IC.replaceOperand(II, 0, V);
    }
    break;
  }

  case Intrinsic::x86_mmx_pmovmskb:
  case Intrinsic::x86_sse_movmsk_ps:
  case Intrinsic::x86_sse2_movmsk_pd:
  case Intrinsic::x86_sse2_pmovmskb_128:
  case Intrinsic::x86_avx_movmsk_pd_256:
  case Intrinsic::x86_avx_movmsk_ps_256:
  case Intrinsic::x86_avx2_pmovmskb:
    if (Value *V = simplifyX86movmsk(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse_comieq_ss:
  case Intrinsic::x86_sse_comige_ss:
  case Intrinsic::x86_sse_comigt_ss:
  case Intrinsic::x86_sse_comile_ss:
  case Intrinsic::x86_sse_comilt_ss:
  case Intrinsic::x86_sse_comineq_ss:
  case Intrinsic::x86_sse_ucomieq_ss:
  case Intrinsic::x86_sse_ucomige_ss:
  case Intrinsic::x86_sse_ucomigt_ss:
  case Intrinsic::x86_sse_ucomile_ss:
  case Intrinsic::x86_sse_ucomilt_ss:
  case Intrinsic::x86_sse_ucomineq_ss:
  case Intrinsic::x86_sse2_comieq_sd:
  case Intrinsic::x86_sse2_comige_sd:
  case Intrinsic::x86_sse2_comigt_sd:
  case Intrinsic::x86_sse2_comile_sd:
  case Intrinsic::x86_sse2_comilt_sd:
  case Intrinsic::x86_sse2_comineq_sd:
  case Intrinsic::x86_sse2_ucomieq_sd:
  case Intrinsic::x86_sse2_ucomige_sd:
  case Intrinsic::x86_sse2_ucomigt_sd:
  case Intrinsic::x86_sse2_ucomile_sd:
  case Intrinsic::x86_sse2_ucomilt_sd:
  case Intrinsic::x86_sse2_ucomineq_sd:
  case Intrinsic::x86_avx512_vcomi_ss:
  case Intrinsic::x86_avx512_vcomi_sd:
  case Intrinsic::x86_avx512_mask_cmp_ss:
  case Intrinsic::x86_avx512_mask_cmp_sd: {
    // These intrinsics only demand the 0th element of their input vectors. If
    // we can simplify the input based on that, do so now.
    bool MadeChange = false;
    Value *Arg0 = II.getArgOperand(0);
    Value *Arg1 = II.getArgOperand(1);
    unsigned VWidth = cast<FixedVectorType>(Arg0->getType())->getNumElements();
    if (Value *V = SimplifyDemandedVectorEltsLow(Arg0, VWidth, 1)) {
      IC.replaceOperand(II, 0, V);
      MadeChange = true;
    }
    if (Value *V = SimplifyDemandedVectorEltsLow(Arg1, VWidth, 1)) {
      IC.replaceOperand(II, 1, V);
      MadeChange = true;
    }
    if (MadeChange) {
      return &II;
    }
    break;
  }

  case Intrinsic::x86_avx512_add_ps_512:
  case Intrinsic::x86_avx512_div_ps_512:
  case Intrinsic::x86_avx512_mul_ps_512:
  case Intrinsic::x86_avx512_sub_ps_512:
  case Intrinsic::x86_avx512_add_pd_512:
  case Intrinsic::x86_avx512_div_pd_512:
  case Intrinsic::x86_avx512_mul_pd_512:
  case Intrinsic::x86_avx512_sub_pd_512:
    // If the rounding mode is CUR_DIRECTION(4) we can turn these into regular
    // IR operations.
    if (auto *R = dyn_cast<ConstantInt>(II.getArgOperand(2))) {
      if (R->getValue() == 4) {
        Value *Arg0 = II.getArgOperand(0);
        Value *Arg1 = II.getArgOperand(1);

        Value *V;
        switch (IID) {
        default:
          llvm_unreachable("Case stmts out of sync!");
        case Intrinsic::x86_avx512_add_ps_512:
        case Intrinsic::x86_avx512_add_pd_512:
          V = IC.Builder.CreateFAdd(Arg0, Arg1);
          break;
        case Intrinsic::x86_avx512_sub_ps_512:
        case Intrinsic::x86_avx512_sub_pd_512:
          V = IC.Builder.CreateFSub(Arg0, Arg1);
          break;
        case Intrinsic::x86_avx512_mul_ps_512:
        case Intrinsic::x86_avx512_mul_pd_512:
          V = IC.Builder.CreateFMul(Arg0, Arg1);
          break;
        case Intrinsic::x86_avx512_div_ps_512:
        case Intrinsic::x86_avx512_div_pd_512:
          V = IC.Builder.CreateFDiv(Arg0, Arg1);
          break;
        }

        return IC.replaceInstUsesWith(II, V);
      }
    }
    break;

  case Intrinsic::x86_avx512_mask_add_ss_round:
  case Intrinsic::x86_avx512_mask_div_ss_round:
  case Intrinsic::x86_avx512_mask_mul_ss_round:
  case Intrinsic::x86_avx512_mask_sub_ss_round:
  case Intrinsic::x86_avx512_mask_add_sd_round:
  case Intrinsic::x86_avx512_mask_div_sd_round:
  case Intrinsic::x86_avx512_mask_mul_sd_round:
  case Intrinsic::x86_avx512_mask_sub_sd_round:
    // If the rounding mode is CUR_DIRECTION(4) we can turn these into regular
    // IR operations.
    if (auto *R = dyn_cast<ConstantInt>(II.getArgOperand(4))) {
      if (R->getValue() == 4) {
        // Extract the element as scalars.
        Value *Arg0 = II.getArgOperand(0);
        Value *Arg1 = II.getArgOperand(1);
        Value *LHS = IC.Builder.CreateExtractElement(Arg0, (uint64_t)0);
        Value *RHS = IC.Builder.CreateExtractElement(Arg1, (uint64_t)0);

        Value *V;
        switch (IID) {
        default:
          llvm_unreachable("Case stmts out of sync!");
        case Intrinsic::x86_avx512_mask_add_ss_round:
        case Intrinsic::x86_avx512_mask_add_sd_round:
          V = IC.Builder.CreateFAdd(LHS, RHS);
          break;
        case Intrinsic::x86_avx512_mask_sub_ss_round:
        case Intrinsic::x86_avx512_mask_sub_sd_round:
          V = IC.Builder.CreateFSub(LHS, RHS);
          break;
        case Intrinsic::x86_avx512_mask_mul_ss_round:
        case Intrinsic::x86_avx512_mask_mul_sd_round:
          V = IC.Builder.CreateFMul(LHS, RHS);
          break;
        case Intrinsic::x86_avx512_mask_div_ss_round:
        case Intrinsic::x86_avx512_mask_div_sd_round:
          V = IC.Builder.CreateFDiv(LHS, RHS);
          break;
        }

        // Handle the masking aspect of the intrinsic.
        Value *Mask = II.getArgOperand(3);
        auto *C = dyn_cast<ConstantInt>(Mask);
        // We don't need a select if we know the mask bit is a 1.
        if (!C || !C->getValue()[0]) {
          // Cast the mask to an i1 vector and then extract the lowest element.
          auto *MaskTy = FixedVectorType::get(
              IC.Builder.getInt1Ty(),
              cast<IntegerType>(Mask->getType())->getBitWidth());
          Mask = IC.Builder.CreateBitCast(Mask, MaskTy);
          Mask = IC.Builder.CreateExtractElement(Mask, (uint64_t)0);
          // Extract the lowest element from the passthru operand.
          Value *Passthru =
              IC.Builder.CreateExtractElement(II.getArgOperand(2), (uint64_t)0);
          V = IC.Builder.CreateSelect(Mask, V, Passthru);
        }

        // Insert the result back into the original argument 0.
        V = IC.Builder.CreateInsertElement(Arg0, V, (uint64_t)0);

        return IC.replaceInstUsesWith(II, V);
      }
    }
    break;

  // Constant fold ashr( <A x Bi>, Ci ).
  // Constant fold lshr( <A x Bi>, Ci ).
  // Constant fold shl( <A x Bi>, Ci ).
  case Intrinsic::x86_sse2_psrai_d:
  case Intrinsic::x86_sse2_psrai_w:
  case Intrinsic::x86_avx2_psrai_d:
  case Intrinsic::x86_avx2_psrai_w:
  case Intrinsic::x86_avx512_psrai_q_128:
  case Intrinsic::x86_avx512_psrai_q_256:
  case Intrinsic::x86_avx512_psrai_d_512:
  case Intrinsic::x86_avx512_psrai_q_512:
  case Intrinsic::x86_avx512_psrai_w_512:
  case Intrinsic::x86_sse2_psrli_d:
  case Intrinsic::x86_sse2_psrli_q:
  case Intrinsic::x86_sse2_psrli_w:
  case Intrinsic::x86_avx2_psrli_d:
  case Intrinsic::x86_avx2_psrli_q:
  case Intrinsic::x86_avx2_psrli_w:
  case Intrinsic::x86_avx512_psrli_d_512:
  case Intrinsic::x86_avx512_psrli_q_512:
  case Intrinsic::x86_avx512_psrli_w_512:
  case Intrinsic::x86_sse2_pslli_d:
  case Intrinsic::x86_sse2_pslli_q:
  case Intrinsic::x86_sse2_pslli_w:
  case Intrinsic::x86_avx2_pslli_d:
  case Intrinsic::x86_avx2_pslli_q:
  case Intrinsic::x86_avx2_pslli_w:
  case Intrinsic::x86_avx512_pslli_d_512:
  case Intrinsic::x86_avx512_pslli_q_512:
  case Intrinsic::x86_avx512_pslli_w_512:
    if (Value *V = simplifyX86immShift(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_psra_d:
  case Intrinsic::x86_sse2_psra_w:
  case Intrinsic::x86_avx2_psra_d:
  case Intrinsic::x86_avx2_psra_w:
  case Intrinsic::x86_avx512_psra_q_128:
  case Intrinsic::x86_avx512_psra_q_256:
  case Intrinsic::x86_avx512_psra_d_512:
  case Intrinsic::x86_avx512_psra_q_512:
  case Intrinsic::x86_avx512_psra_w_512:
  case Intrinsic::x86_sse2_psrl_d:
  case Intrinsic::x86_sse2_psrl_q:
  case Intrinsic::x86_sse2_psrl_w:
  case Intrinsic::x86_avx2_psrl_d:
  case Intrinsic::x86_avx2_psrl_q:
  case Intrinsic::x86_avx2_psrl_w:
  case Intrinsic::x86_avx512_psrl_d_512:
  case Intrinsic::x86_avx512_psrl_q_512:
  case Intrinsic::x86_avx512_psrl_w_512:
  case Intrinsic::x86_sse2_psll_d:
  case Intrinsic::x86_sse2_psll_q:
  case Intrinsic::x86_sse2_psll_w:
  case Intrinsic::x86_avx2_psll_d:
  case Intrinsic::x86_avx2_psll_q:
  case Intrinsic::x86_avx2_psll_w:
  case Intrinsic::x86_avx512_psll_d_512:
  case Intrinsic::x86_avx512_psll_q_512:
  case Intrinsic::x86_avx512_psll_w_512: {
    if (Value *V = simplifyX86immShift(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }

    // SSE2/AVX2 uses only the first 64-bits of the 128-bit vector
    // operand to compute the shift amount.
    Value *Arg1 = II.getArgOperand(1);
    assert(Arg1->getType()->getPrimitiveSizeInBits() == 128 &&
           "Unexpected packed shift size");
    unsigned VWidth = cast<FixedVectorType>(Arg1->getType())->getNumElements();

    if (Value *V = SimplifyDemandedVectorEltsLow(Arg1, VWidth, VWidth / 2)) {
      return IC.replaceOperand(II, 1, V);
    }
    break;
  }

  case Intrinsic::x86_avx2_psllv_d:
  case Intrinsic::x86_avx2_psllv_d_256:
  case Intrinsic::x86_avx2_psllv_q:
  case Intrinsic::x86_avx2_psllv_q_256:
  case Intrinsic::x86_avx512_psllv_d_512:
  case Intrinsic::x86_avx512_psllv_q_512:
  case Intrinsic::x86_avx512_psllv_w_128:
  case Intrinsic::x86_avx512_psllv_w_256:
  case Intrinsic::x86_avx512_psllv_w_512:
  case Intrinsic::x86_avx2_psrav_d:
  case Intrinsic::x86_avx2_psrav_d_256:
  case Intrinsic::x86_avx512_psrav_q_128:
  case Intrinsic::x86_avx512_psrav_q_256:
  case Intrinsic::x86_avx512_psrav_d_512:
  case Intrinsic::x86_avx512_psrav_q_512:
  case Intrinsic::x86_avx512_psrav_w_128:
  case Intrinsic::x86_avx512_psrav_w_256:
  case Intrinsic::x86_avx512_psrav_w_512:
  case Intrinsic::x86_avx2_psrlv_d:
  case Intrinsic::x86_avx2_psrlv_d_256:
  case Intrinsic::x86_avx2_psrlv_q:
  case Intrinsic::x86_avx2_psrlv_q_256:
  case Intrinsic::x86_avx512_psrlv_d_512:
  case Intrinsic::x86_avx512_psrlv_q_512:
  case Intrinsic::x86_avx512_psrlv_w_128:
  case Intrinsic::x86_avx512_psrlv_w_256:
  case Intrinsic::x86_avx512_psrlv_w_512:
    if (Value *V = simplifyX86varShift(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_packssdw_128:
  case Intrinsic::x86_sse2_packsswb_128:
  case Intrinsic::x86_avx2_packssdw:
  case Intrinsic::x86_avx2_packsswb:
  case Intrinsic::x86_avx512_packssdw_512:
  case Intrinsic::x86_avx512_packsswb_512:
    if (Value *V = simplifyX86pack(II, IC.Builder, true)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_packuswb_128:
  case Intrinsic::x86_sse41_packusdw:
  case Intrinsic::x86_avx2_packusdw:
  case Intrinsic::x86_avx2_packuswb:
  case Intrinsic::x86_avx512_packusdw_512:
  case Intrinsic::x86_avx512_packuswb_512:
    if (Value *V = simplifyX86pack(II, IC.Builder, false)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_pmulh_w:
  case Intrinsic::x86_avx2_pmulh_w:
  case Intrinsic::x86_avx512_pmulh_w_512:
    if (Value *V = simplifyX86pmulh(II, IC.Builder, true, false)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_pmulhu_w:
  case Intrinsic::x86_avx2_pmulhu_w:
  case Intrinsic::x86_avx512_pmulhu_w_512:
    if (Value *V = simplifyX86pmulh(II, IC.Builder, false, false)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_ssse3_pmul_hr_sw_128:
  case Intrinsic::x86_avx2_pmul_hr_sw:
  case Intrinsic::x86_avx512_pmul_hr_sw_512:
    if (Value *V = simplifyX86pmulh(II, IC.Builder, true, true)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse2_pmadd_wd:
  case Intrinsic::x86_avx2_pmadd_wd:
  case Intrinsic::x86_avx512_pmaddw_d_512:
    if (Value *V = simplifyX86pmadd(II, IC.Builder, true)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_ssse3_pmadd_ub_sw_128:
  case Intrinsic::x86_avx2_pmadd_ub_sw:
  case Intrinsic::x86_avx512_pmaddubs_w_512:
    if (Value *V = simplifyX86pmadd(II, IC.Builder, false)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_pclmulqdq:
  case Intrinsic::x86_pclmulqdq_256:
  case Intrinsic::x86_pclmulqdq_512: {
    if (auto *C = dyn_cast<ConstantInt>(II.getArgOperand(2))) {
      unsigned Imm = C->getZExtValue();

      bool MadeChange = false;
      Value *Arg0 = II.getArgOperand(0);
      Value *Arg1 = II.getArgOperand(1);
      unsigned VWidth =
          cast<FixedVectorType>(Arg0->getType())->getNumElements();

      APInt UndefElts1(VWidth, 0);
      APInt DemandedElts1 =
          APInt::getSplat(VWidth, APInt(2, (Imm & 0x01) ? 2 : 1));
      if (Value *V =
              IC.SimplifyDemandedVectorElts(Arg0, DemandedElts1, UndefElts1)) {
        IC.replaceOperand(II, 0, V);
        MadeChange = true;
      }

      APInt UndefElts2(VWidth, 0);
      APInt DemandedElts2 =
          APInt::getSplat(VWidth, APInt(2, (Imm & 0x10) ? 2 : 1));
      if (Value *V =
              IC.SimplifyDemandedVectorElts(Arg1, DemandedElts2, UndefElts2)) {
        IC.replaceOperand(II, 1, V);
        MadeChange = true;
      }

      // If either input elements are undef, the result is zero.
      if (DemandedElts1.isSubsetOf(UndefElts1) ||
          DemandedElts2.isSubsetOf(UndefElts2)) {
        return IC.replaceInstUsesWith(II,
                                      ConstantAggregateZero::get(II.getType()));
      }

      if (MadeChange) {
        return &II;
      }
    }
    break;
  }

  case Intrinsic::x86_sse41_insertps:
    if (Value *V = simplifyX86insertps(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_sse4a_extrq: {
    Value *Op0 = II.getArgOperand(0);
    Value *Op1 = II.getArgOperand(1);
    unsigned VWidth0 = cast<FixedVectorType>(Op0->getType())->getNumElements();
    unsigned VWidth1 = cast<FixedVectorType>(Op1->getType())->getNumElements();
    assert(Op0->getType()->getPrimitiveSizeInBits() == 128 &&
           Op1->getType()->getPrimitiveSizeInBits() == 128 && VWidth0 == 2 &&
           VWidth1 == 16 && "Unexpected operand sizes");

    // See if we're dealing with constant values.
    auto *C1 = dyn_cast<Constant>(Op1);
    auto *CILength =
        C1 ? dyn_cast_or_null<ConstantInt>(C1->getAggregateElement((unsigned)0))
           : nullptr;
    auto *CIIndex =
        C1 ? dyn_cast_or_null<ConstantInt>(C1->getAggregateElement((unsigned)1))
           : nullptr;

    // Attempt to simplify to a constant, shuffle vector or EXTRQI call.
    if (Value *V = simplifyX86extrq(II, Op0, CILength, CIIndex, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }

    // EXTRQ only uses the lowest 64-bits of the first 128-bit vector
    // operands and the lowest 16-bits of the second.
    bool MadeChange = false;
    if (Value *V = SimplifyDemandedVectorEltsLow(Op0, VWidth0, 1)) {
      IC.replaceOperand(II, 0, V);
      MadeChange = true;
    }
    if (Value *V = SimplifyDemandedVectorEltsLow(Op1, VWidth1, 2)) {
      IC.replaceOperand(II, 1, V);
      MadeChange = true;
    }
    if (MadeChange) {
      return &II;
    }
    break;
  }

  case Intrinsic::x86_sse4a_extrqi: {
    // EXTRQI: Extract Length bits starting from Index. Zero pad the remaining
    // bits of the lower 64-bits. The upper 64-bits are undefined.
    Value *Op0 = II.getArgOperand(0);
    unsigned VWidth = cast<FixedVectorType>(Op0->getType())->getNumElements();
    assert(Op0->getType()->getPrimitiveSizeInBits() == 128 && VWidth == 2 &&
           "Unexpected operand size");

    // See if we're dealing with constant values.
    auto *CILength = dyn_cast<ConstantInt>(II.getArgOperand(1));
    auto *CIIndex = dyn_cast<ConstantInt>(II.getArgOperand(2));

    // Attempt to simplify to a constant or shuffle vector.
    if (Value *V = simplifyX86extrq(II, Op0, CILength, CIIndex, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }

    // EXTRQI only uses the lowest 64-bits of the first 128-bit vector
    // operand.
    if (Value *V = SimplifyDemandedVectorEltsLow(Op0, VWidth, 1)) {
      return IC.replaceOperand(II, 0, V);
    }
    break;
  }

  case Intrinsic::x86_sse4a_insertq: {
    Value *Op0 = II.getArgOperand(0);
    Value *Op1 = II.getArgOperand(1);
    unsigned VWidth = cast<FixedVectorType>(Op0->getType())->getNumElements();
    assert(Op0->getType()->getPrimitiveSizeInBits() == 128 &&
           Op1->getType()->getPrimitiveSizeInBits() == 128 && VWidth == 2 &&
           cast<FixedVectorType>(Op1->getType())->getNumElements() == 2 &&
           "Unexpected operand size");

    // See if we're dealing with constant values.
    auto *C1 = dyn_cast<Constant>(Op1);
    auto *CI11 =
        C1 ? dyn_cast_or_null<ConstantInt>(C1->getAggregateElement((unsigned)1))
           : nullptr;

    // Attempt to simplify to a constant, shuffle vector or INSERTQI call.
    if (CI11) {
      const APInt &V11 = CI11->getValue();
      APInt Len = V11.zextOrTrunc(6);
      APInt Idx = V11.lshr(8).zextOrTrunc(6);
      if (Value *V = simplifyX86insertq(II, Op0, Op1, Len, Idx, IC.Builder)) {
        return IC.replaceInstUsesWith(II, V);
      }
    }

    // INSERTQ only uses the lowest 64-bits of the first 128-bit vector
    // operand.
    if (Value *V = SimplifyDemandedVectorEltsLow(Op0, VWidth, 1)) {
      return IC.replaceOperand(II, 0, V);
    }
    break;
  }

  case Intrinsic::x86_sse4a_insertqi: {
    // INSERTQI: Extract lowest Length bits from lower half of second source and
    // insert over first source starting at Index bit. The upper 64-bits are
    // undefined.
    Value *Op0 = II.getArgOperand(0);
    Value *Op1 = II.getArgOperand(1);
    unsigned VWidth0 = cast<FixedVectorType>(Op0->getType())->getNumElements();
    unsigned VWidth1 = cast<FixedVectorType>(Op1->getType())->getNumElements();
    assert(Op0->getType()->getPrimitiveSizeInBits() == 128 &&
           Op1->getType()->getPrimitiveSizeInBits() == 128 && VWidth0 == 2 &&
           VWidth1 == 2 && "Unexpected operand sizes");

    // See if we're dealing with constant values.
    auto *CILength = dyn_cast<ConstantInt>(II.getArgOperand(2));
    auto *CIIndex = dyn_cast<ConstantInt>(II.getArgOperand(3));

    // Attempt to simplify to a constant or shuffle vector.
    if (CILength && CIIndex) {
      APInt Len = CILength->getValue().zextOrTrunc(6);
      APInt Idx = CIIndex->getValue().zextOrTrunc(6);
      if (Value *V = simplifyX86insertq(II, Op0, Op1, Len, Idx, IC.Builder)) {
        return IC.replaceInstUsesWith(II, V);
      }
    }

    // INSERTQI only uses the lowest 64-bits of the first two 128-bit vector
    // operands.
    bool MadeChange = false;
    if (Value *V = SimplifyDemandedVectorEltsLow(Op0, VWidth0, 1)) {
      IC.replaceOperand(II, 0, V);
      MadeChange = true;
    }
    if (Value *V = SimplifyDemandedVectorEltsLow(Op1, VWidth1, 1)) {
      IC.replaceOperand(II, 1, V);
      MadeChange = true;
    }
    if (MadeChange) {
      return &II;
    }
    break;
  }

  case Intrinsic::x86_sse41_pblendvb:
  case Intrinsic::x86_sse41_blendvps:
  case Intrinsic::x86_sse41_blendvpd:
  case Intrinsic::x86_avx_blendv_ps_256:
  case Intrinsic::x86_avx_blendv_pd_256:
  case Intrinsic::x86_avx2_pblendvb: {
    // fold (blend A, A, Mask) -> A
    Value *Op0 = II.getArgOperand(0);
    Value *Op1 = II.getArgOperand(1);
    Value *Mask = II.getArgOperand(2);
    if (Op0 == Op1) {
      return IC.replaceInstUsesWith(II, Op0);
    }

    // Zero Mask - select 1st argument.
    if (isa<ConstantAggregateZero>(Mask)) {
      return IC.replaceInstUsesWith(II, Op0);
    }

    // Constant Mask - select 1st/2nd argument lane based on top bit of mask.
    if (auto *ConstantMask = dyn_cast<ConstantDataVector>(Mask)) {
      Constant *NewSelector =
          getNegativeIsTrueBoolVec(ConstantMask, IC.getDataLayout());
      return SelectInst::Create(NewSelector, Op1, Op0, "blendv");
    }

    Mask = InstCombiner::peekThroughBitcast(Mask);

    // Peek through a one-use shuffle - VectorCombine should have simplified
    // this for cases where we're splitting wider vectors to use blendv
    // intrinsics.
    Value *MaskSrc = nullptr;
    ArrayRef<int> ShuffleMask;
    if (match(Mask, m_OneUse(m_Shuffle(m_Value(MaskSrc), m_Undef(),
                                       m_Mask(ShuffleMask))))) {
      // Bail if the shuffle was irregular or contains undefs.
      int NumElts = cast<FixedVectorType>(MaskSrc->getType())->getNumElements();
      if (NumElts < (int)ShuffleMask.size() || !isPowerOf2_32(NumElts) ||
          any_of(ShuffleMask,
                 [NumElts](int M) { return M < 0 || M >= NumElts; }))
        break;
      Mask = InstCombiner::peekThroughBitcast(MaskSrc);
    }

    // Convert to a vector select if we can bypass casts and find a boolean
    // vector condition value.
    Value *BoolVec;
    if (match(Mask, m_SExt(m_Value(BoolVec))) &&
        BoolVec->getType()->isVectorTy() &&
        BoolVec->getType()->getScalarSizeInBits() == 1) {
      auto *MaskTy = cast<FixedVectorType>(Mask->getType());
      auto *OpTy = cast<FixedVectorType>(II.getType());
      unsigned NumMaskElts = MaskTy->getNumElements();
      unsigned NumOperandElts = OpTy->getNumElements();

      // If we peeked through a shuffle, reapply the shuffle to the bool vector.
      if (MaskSrc) {
        unsigned NumMaskSrcElts =
            cast<FixedVectorType>(MaskSrc->getType())->getNumElements();
        NumMaskElts = (ShuffleMask.size() * NumMaskElts) / NumMaskSrcElts;
        // Multiple mask bits maps to the same operand element - bail out.
        if (NumMaskElts > NumOperandElts)
          break;
        SmallVector<int> ScaledMask;
        if (!llvm::scaleShuffleMaskElts(NumMaskElts, ShuffleMask, ScaledMask))
          break;
        BoolVec = IC.Builder.CreateShuffleVector(BoolVec, ScaledMask);
        MaskTy = FixedVectorType::get(MaskTy->getElementType(), NumMaskElts);
      }
      assert(MaskTy->getPrimitiveSizeInBits() ==
                 OpTy->getPrimitiveSizeInBits() &&
             "Not expecting mask and operands with different sizes");

      if (NumMaskElts == NumOperandElts) {
        return SelectInst::Create(BoolVec, Op1, Op0);
      }

      // If the mask has less elements than the operands, each mask bit maps to
      // multiple elements of the operands. Bitcast back and forth.
      if (NumMaskElts < NumOperandElts) {
        Value *CastOp0 = IC.Builder.CreateBitCast(Op0, MaskTy);
        Value *CastOp1 = IC.Builder.CreateBitCast(Op1, MaskTy);
        Value *Sel = IC.Builder.CreateSelect(BoolVec, CastOp1, CastOp0);
        return new BitCastInst(Sel, II.getType());
      }
    }

    break;
  }

  case Intrinsic::x86_ssse3_pshuf_b_128:
  case Intrinsic::x86_avx2_pshuf_b:
  case Intrinsic::x86_avx512_pshuf_b_512:
    if (Value *V = simplifyX86pshufb(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_avx_vpermilvar_ps:
  case Intrinsic::x86_avx_vpermilvar_ps_256:
  case Intrinsic::x86_avx512_vpermilvar_ps_512:
  case Intrinsic::x86_avx_vpermilvar_pd:
  case Intrinsic::x86_avx_vpermilvar_pd_256:
  case Intrinsic::x86_avx512_vpermilvar_pd_512:
    if (Value *V = simplifyX86vpermilvar(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_avx2_permd:
  case Intrinsic::x86_avx2_permps:
  case Intrinsic::x86_avx512_permvar_df_256:
  case Intrinsic::x86_avx512_permvar_df_512:
  case Intrinsic::x86_avx512_permvar_di_256:
  case Intrinsic::x86_avx512_permvar_di_512:
  case Intrinsic::x86_avx512_permvar_hi_128:
  case Intrinsic::x86_avx512_permvar_hi_256:
  case Intrinsic::x86_avx512_permvar_hi_512:
  case Intrinsic::x86_avx512_permvar_qi_128:
  case Intrinsic::x86_avx512_permvar_qi_256:
  case Intrinsic::x86_avx512_permvar_qi_512:
  case Intrinsic::x86_avx512_permvar_sf_512:
  case Intrinsic::x86_avx512_permvar_si_512:
    if (Value *V = simplifyX86vpermv(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_avx512_vpermi2var_d_128:
  case Intrinsic::x86_avx512_vpermi2var_d_256:
  case Intrinsic::x86_avx512_vpermi2var_d_512:
  case Intrinsic::x86_avx512_vpermi2var_hi_128: 
  case Intrinsic::x86_avx512_vpermi2var_hi_256: 
  case Intrinsic::x86_avx512_vpermi2var_hi_512: 
  case Intrinsic::x86_avx512_vpermi2var_pd_128: 
  case Intrinsic::x86_avx512_vpermi2var_pd_256: 
  case Intrinsic::x86_avx512_vpermi2var_pd_512: 
  case Intrinsic::x86_avx512_vpermi2var_ps_128: 
  case Intrinsic::x86_avx512_vpermi2var_ps_256: 
  case Intrinsic::x86_avx512_vpermi2var_ps_512: 
  case Intrinsic::x86_avx512_vpermi2var_q_128:
  case Intrinsic::x86_avx512_vpermi2var_q_256:
  case Intrinsic::x86_avx512_vpermi2var_q_512:
  case Intrinsic::x86_avx512_vpermi2var_qi_128:
  case Intrinsic::x86_avx512_vpermi2var_qi_256:
  case Intrinsic::x86_avx512_vpermi2var_qi_512:
    if (Value *V = simplifyX86vpermv3(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_avx_maskload_ps:
  case Intrinsic::x86_avx_maskload_pd:
  case Intrinsic::x86_avx_maskload_ps_256:
  case Intrinsic::x86_avx_maskload_pd_256:
  case Intrinsic::x86_avx2_maskload_d:
  case Intrinsic::x86_avx2_maskload_q:
  case Intrinsic::x86_avx2_maskload_d_256:
  case Intrinsic::x86_avx2_maskload_q_256:
    if (Instruction *I = simplifyX86MaskedLoad(II, IC)) {
      return I;
    }
    break;

  case Intrinsic::x86_sse2_maskmov_dqu:
  case Intrinsic::x86_avx_maskstore_ps:
  case Intrinsic::x86_avx_maskstore_pd:
  case Intrinsic::x86_avx_maskstore_ps_256:
  case Intrinsic::x86_avx_maskstore_pd_256:
  case Intrinsic::x86_avx2_maskstore_d:
  case Intrinsic::x86_avx2_maskstore_q:
  case Intrinsic::x86_avx2_maskstore_d_256:
  case Intrinsic::x86_avx2_maskstore_q_256:
    if (simplifyX86MaskedStore(II, IC)) {
      return nullptr;
    }
    break;

  case Intrinsic::x86_addcarry_32:
  case Intrinsic::x86_addcarry_64:
    if (Value *V = simplifyX86addcarry(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;

  case Intrinsic::x86_avx512_pternlog_d_128:
  case Intrinsic::x86_avx512_pternlog_d_256:
  case Intrinsic::x86_avx512_pternlog_d_512:
  case Intrinsic::x86_avx512_pternlog_q_128:
  case Intrinsic::x86_avx512_pternlog_q_256:
  case Intrinsic::x86_avx512_pternlog_q_512:
    if (Value *V = simplifyTernarylogic(II, IC.Builder)) {
      return IC.replaceInstUsesWith(II, V);
    }
    break;
  default:
    break;
  }
  return std::nullopt;
}

std::optional<Value *> X86TTIImpl::simplifyDemandedUseBitsIntrinsic(
    InstCombiner &IC, IntrinsicInst &II, APInt DemandedMask, KnownBits &Known,
    bool &KnownBitsComputed) const {
  switch (II.getIntrinsicID()) {
  default:
    break;
  case Intrinsic::x86_mmx_pmovmskb:
  case Intrinsic::x86_sse_movmsk_ps:
  case Intrinsic::x86_sse2_movmsk_pd:
  case Intrinsic::x86_sse2_pmovmskb_128:
  case Intrinsic::x86_avx_movmsk_ps_256:
  case Intrinsic::x86_avx_movmsk_pd_256:
  case Intrinsic::x86_avx2_pmovmskb: {
    // MOVMSK copies the vector elements' sign bits to the low bits
    // and zeros the high bits.
    unsigned ArgWidth;
    if (II.getIntrinsicID() == Intrinsic::x86_mmx_pmovmskb) {
      ArgWidth = 8; // Arg is x86_mmx, but treated as <8 x i8>.
    } else {
      auto *ArgType = cast<FixedVectorType>(II.getArgOperand(0)->getType());
      ArgWidth = ArgType->getNumElements();
    }

    // If we don't need any of low bits then return zero,
    // we know that DemandedMask is non-zero already.
    APInt DemandedElts = DemandedMask.zextOrTrunc(ArgWidth);
    Type *VTy = II.getType();
    if (DemandedElts.isZero()) {
      return ConstantInt::getNullValue(VTy);
    }

    // We know that the upper bits are set to zero.
    Known.Zero.setBitsFrom(ArgWidth);
    KnownBitsComputed = true;
    break;
  }
  }
  return std::nullopt;
}

std::optional<Value *> X86TTIImpl::simplifyDemandedVectorEltsIntrinsic(
    InstCombiner &IC, IntrinsicInst &II, APInt DemandedElts, APInt &UndefElts,
    APInt &UndefElts2, APInt &UndefElts3,
    std::function<void(Instruction *, unsigned, APInt, APInt &)>
        simplifyAndSetOp) const {
  unsigned VWidth = cast<FixedVectorType>(II.getType())->getNumElements();
  switch (II.getIntrinsicID()) {
  default:
    break;
  case Intrinsic::x86_xop_vfrcz_ss:
  case Intrinsic::x86_xop_vfrcz_sd:
    // The instructions for these intrinsics are speced to zero upper bits not
    // pass them through like other scalar intrinsics. So we shouldn't just
    // use Arg0 if DemandedElts[0] is clear like we do for other intrinsics.
    // Instead we should return a zero vector.
    if (!DemandedElts[0]) {
      IC.addToWorklist(&II);
      return ConstantAggregateZero::get(II.getType());
    }

    // Only the lower element is used.
    DemandedElts = 1;
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);

    // Only the lower element is undefined. The high elements are zero.
    UndefElts = UndefElts[0];
    break;

  // Unary scalar-as-vector operations that work column-wise.
  case Intrinsic::x86_sse_rcp_ss:
  case Intrinsic::x86_sse_rsqrt_ss:
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);

    // If lowest element of a scalar op isn't used then use Arg0.
    if (!DemandedElts[0]) {
      IC.addToWorklist(&II);
      return II.getArgOperand(0);
    }
    // TODO: If only low elt lower SQRT to FSQRT (with rounding/exceptions
    // checks).
    break;

  // Binary scalar-as-vector operations that work column-wise. The high
  // elements come from operand 0. The low element is a function of both
  // operands.
  case Intrinsic::x86_sse_min_ss:
  case Intrinsic::x86_sse_max_ss:
  case Intrinsic::x86_sse_cmp_ss:
  case Intrinsic::x86_sse2_min_sd:
  case Intrinsic::x86_sse2_max_sd:
  case Intrinsic::x86_sse2_cmp_sd: {
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);

    // If lowest element of a scalar op isn't used then use Arg0.
    if (!DemandedElts[0]) {
      IC.addToWorklist(&II);
      return II.getArgOperand(0);
    }

    // Only lower element is used for operand 1.
    DemandedElts = 1;
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);

    // Lower element is undefined if both lower elements are undefined.
    // Consider things like undef&0.  The result is known zero, not undef.
    if (!UndefElts2[0])
      UndefElts.clearBit(0);

    break;
  }

  // Binary scalar-as-vector operations that work column-wise. The high
  // elements come from operand 0 and the low element comes from operand 1.
  case Intrinsic::x86_sse41_round_ss:
  case Intrinsic::x86_sse41_round_sd: {
    // Don't use the low element of operand 0.
    APInt DemandedElts2 = DemandedElts;
    DemandedElts2.clearBit(0);
    simplifyAndSetOp(&II, 0, DemandedElts2, UndefElts);

    // If lowest element of a scalar op isn't used then use Arg0.
    if (!DemandedElts[0]) {
      IC.addToWorklist(&II);
      return II.getArgOperand(0);
    }

    // Only lower element is used for operand 1.
    DemandedElts = 1;
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);

    // Take the high undef elements from operand 0 and take the lower element
    // from operand 1.
    UndefElts.clearBit(0);
    UndefElts |= UndefElts2[0];
    break;
  }

  // Three input scalar-as-vector operations that work column-wise. The high
  // elements come from operand 0 and the low element is a function of all
  // three inputs.
  case Intrinsic::x86_avx512_mask_add_ss_round:
  case Intrinsic::x86_avx512_mask_div_ss_round:
  case Intrinsic::x86_avx512_mask_mul_ss_round:
  case Intrinsic::x86_avx512_mask_sub_ss_round:
  case Intrinsic::x86_avx512_mask_max_ss_round:
  case Intrinsic::x86_avx512_mask_min_ss_round:
  case Intrinsic::x86_avx512_mask_add_sd_round:
  case Intrinsic::x86_avx512_mask_div_sd_round:
  case Intrinsic::x86_avx512_mask_mul_sd_round:
  case Intrinsic::x86_avx512_mask_sub_sd_round:
  case Intrinsic::x86_avx512_mask_max_sd_round:
  case Intrinsic::x86_avx512_mask_min_sd_round:
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);

    // If lowest element of a scalar op isn't used then use Arg0.
    if (!DemandedElts[0]) {
      IC.addToWorklist(&II);
      return II.getArgOperand(0);
    }

    // Only lower element is used for operand 1 and 2.
    DemandedElts = 1;
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);
    simplifyAndSetOp(&II, 2, DemandedElts, UndefElts3);

    // Lower element is undefined if all three lower elements are undefined.
    // Consider things like undef&0.  The result is known zero, not undef.
    if (!UndefElts2[0] || !UndefElts3[0])
      UndefElts.clearBit(0);
    break;

  // TODO: Add fmaddsub support?
  case Intrinsic::x86_sse3_addsub_pd:
  case Intrinsic::x86_sse3_addsub_ps:
  case Intrinsic::x86_avx_addsub_pd_256:
  case Intrinsic::x86_avx_addsub_ps_256: {
    // If none of the even or none of the odd lanes are required, turn this
    // into a generic FP math instruction.
    APInt SubMask = APInt::getSplat(VWidth, APInt(2, 0x1));
    APInt AddMask = APInt::getSplat(VWidth, APInt(2, 0x2));
    bool IsSubOnly = DemandedElts.isSubsetOf(SubMask);
    bool IsAddOnly = DemandedElts.isSubsetOf(AddMask);
    if (IsSubOnly || IsAddOnly) {
      assert((IsSubOnly ^ IsAddOnly) && "Can't be both add-only and sub-only");
      IRBuilderBase::InsertPointGuard Guard(IC.Builder);
      IC.Builder.SetInsertPoint(&II);
      Value *Arg0 = II.getArgOperand(0), *Arg1 = II.getArgOperand(1);
      return IC.Builder.CreateBinOp(
          IsSubOnly ? Instruction::FSub : Instruction::FAdd, Arg0, Arg1);
    }

    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);
    UndefElts &= UndefElts2;
    break;
  }

  // General per-element vector operations.
  case Intrinsic::x86_avx2_psllv_d:
  case Intrinsic::x86_avx2_psllv_d_256:
  case Intrinsic::x86_avx2_psllv_q:
  case Intrinsic::x86_avx2_psllv_q_256:
  case Intrinsic::x86_avx2_psrlv_d:
  case Intrinsic::x86_avx2_psrlv_d_256:
  case Intrinsic::x86_avx2_psrlv_q:
  case Intrinsic::x86_avx2_psrlv_q_256:
  case Intrinsic::x86_avx2_psrav_d:
  case Intrinsic::x86_avx2_psrav_d_256: {
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);
    UndefElts &= UndefElts2;
    break;
  }

  case Intrinsic::x86_sse2_pmulh_w:
  case Intrinsic::x86_avx2_pmulh_w:
  case Intrinsic::x86_avx512_pmulh_w_512:
  case Intrinsic::x86_sse2_pmulhu_w:
  case Intrinsic::x86_avx2_pmulhu_w:
  case Intrinsic::x86_avx512_pmulhu_w_512:
  case Intrinsic::x86_ssse3_pmul_hr_sw_128:
  case Intrinsic::x86_avx2_pmul_hr_sw:
  case Intrinsic::x86_avx512_pmul_hr_sw_512: {
    simplifyAndSetOp(&II, 0, DemandedElts, UndefElts);
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts2);
    // NOTE: mulh(undef,undef) != undef.
    break;
  }

  case Intrinsic::x86_sse2_packssdw_128:
  case Intrinsic::x86_sse2_packsswb_128:
  case Intrinsic::x86_sse2_packuswb_128:
  case Intrinsic::x86_sse41_packusdw:
  case Intrinsic::x86_avx2_packssdw:
  case Intrinsic::x86_avx2_packsswb:
  case Intrinsic::x86_avx2_packusdw:
  case Intrinsic::x86_avx2_packuswb:
  case Intrinsic::x86_avx512_packssdw_512:
  case Intrinsic::x86_avx512_packsswb_512:
  case Intrinsic::x86_avx512_packusdw_512:
  case Intrinsic::x86_avx512_packuswb_512: {
    auto *Ty0 = II.getArgOperand(0)->getType();
    unsigned InnerVWidth = cast<FixedVectorType>(Ty0)->getNumElements();
    assert(VWidth == (InnerVWidth * 2) && "Unexpected input size");

    unsigned NumLanes = Ty0->getPrimitiveSizeInBits() / 128;
    unsigned VWidthPerLane = VWidth / NumLanes;
    unsigned InnerVWidthPerLane = InnerVWidth / NumLanes;

    // Per lane, pack the elements of the first input and then the second.
    // e.g.
    // v8i16 PACK(v4i32 X, v4i32 Y) - (X[0..3],Y[0..3])
    // v32i8 PACK(v16i16 X, v16i16 Y) - (X[0..7],Y[0..7]),(X[8..15],Y[8..15])
    for (int OpNum = 0; OpNum != 2; ++OpNum) {
      APInt OpDemandedElts(InnerVWidth, 0);
      for (unsigned Lane = 0; Lane != NumLanes; ++Lane) {
        unsigned LaneIdx = Lane * VWidthPerLane;
        for (unsigned Elt = 0; Elt != InnerVWidthPerLane; ++Elt) {
          unsigned Idx = LaneIdx + Elt + InnerVWidthPerLane * OpNum;
          if (DemandedElts[Idx])
            OpDemandedElts.setBit((Lane * InnerVWidthPerLane) + Elt);
        }
      }

      // Demand elements from the operand.
      APInt OpUndefElts(InnerVWidth, 0);
      simplifyAndSetOp(&II, OpNum, OpDemandedElts, OpUndefElts);

      // Pack the operand's UNDEF elements, one lane at a time.
      OpUndefElts = OpUndefElts.zext(VWidth);
      for (unsigned Lane = 0; Lane != NumLanes; ++Lane) {
        APInt LaneElts = OpUndefElts.lshr(InnerVWidthPerLane * Lane);
        LaneElts = LaneElts.getLoBits(InnerVWidthPerLane);
        LaneElts <<= InnerVWidthPerLane * (2 * Lane + OpNum);
        UndefElts |= LaneElts;
      }
    }
    break;
  }

  case Intrinsic::x86_sse2_pmadd_wd:
  case Intrinsic::x86_avx2_pmadd_wd:
  case Intrinsic::x86_avx512_pmaddw_d_512:
  case Intrinsic::x86_ssse3_pmadd_ub_sw_128:
  case Intrinsic::x86_avx2_pmadd_ub_sw:
  case Intrinsic::x86_avx512_pmaddubs_w_512: {
    // PMADD - demand both src elements that map to each dst element.
    auto *ArgTy = II.getArgOperand(0)->getType();
    unsigned InnerVWidth = cast<FixedVectorType>(ArgTy)->getNumElements();
    assert((VWidth * 2) == InnerVWidth && "Unexpected input size");
    APInt OpDemandedElts = APIntOps::ScaleBitMask(DemandedElts, InnerVWidth);
    APInt Op0UndefElts(InnerVWidth, 0);
    APInt Op1UndefElts(InnerVWidth, 0);
    simplifyAndSetOp(&II, 0, OpDemandedElts, Op0UndefElts);
    simplifyAndSetOp(&II, 1, OpDemandedElts, Op1UndefElts);
    // NOTE: madd(undef,undef) != undef.
    break;
  }

  // PSHUFB
  case Intrinsic::x86_ssse3_pshuf_b_128:
  case Intrinsic::x86_avx2_pshuf_b:
  case Intrinsic::x86_avx512_pshuf_b_512:
  // PERMILVAR
  case Intrinsic::x86_avx_vpermilvar_ps:
  case Intrinsic::x86_avx_vpermilvar_ps_256:
  case Intrinsic::x86_avx512_vpermilvar_ps_512:
  case Intrinsic::x86_avx_vpermilvar_pd:
  case Intrinsic::x86_avx_vpermilvar_pd_256:
  case Intrinsic::x86_avx512_vpermilvar_pd_512:
  // PERMV
  case Intrinsic::x86_avx2_permd:
  case Intrinsic::x86_avx2_permps: {
    simplifyAndSetOp(&II, 1, DemandedElts, UndefElts);
    break;
  }

  // SSE4A instructions leave the upper 64-bits of the 128-bit result
  // in an undefined state.
  case Intrinsic::x86_sse4a_extrq:
  case Intrinsic::x86_sse4a_extrqi:
  case Intrinsic::x86_sse4a_insertq:
  case Intrinsic::x86_sse4a_insertqi:
    UndefElts.setHighBits(VWidth / 2);
    break;
  }
  return std::nullopt;
}
