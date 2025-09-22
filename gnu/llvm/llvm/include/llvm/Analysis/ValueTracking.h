//===- llvm/Analysis/ValueTracking.h - Walk computations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains routines that help analyze properties that chains of
// computations have.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUETRACKING_H
#define LLVM_ANALYSIS_VALUETRACKING_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Analysis/SimplifyQuery.h"
#include "llvm/Analysis/WithCache.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/FMF.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Intrinsics.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class Operator;
class AddOperator;
class AllocaInst;
class APInt;
class AssumptionCache;
class DominatorTree;
class GEPOperator;
class LoadInst;
class WithOverflowInst;
struct KnownBits;
class Loop;
class LoopInfo;
class MDNode;
class StringRef;
class TargetLibraryInfo;
class Value;

constexpr unsigned MaxAnalysisRecursionDepth = 6;

/// Determine which bits of V are known to be either zero or one and return
/// them in the KnownZero/KnownOne bit sets.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, the known zero and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
void computeKnownBits(const Value *V, KnownBits &Known, const DataLayout &DL,
                      unsigned Depth = 0, AssumptionCache *AC = nullptr,
                      const Instruction *CxtI = nullptr,
                      const DominatorTree *DT = nullptr,
                      bool UseInstrInfo = true);

/// Returns the known bits rather than passing by reference.
KnownBits computeKnownBits(const Value *V, const DataLayout &DL,
                           unsigned Depth = 0, AssumptionCache *AC = nullptr,
                           const Instruction *CxtI = nullptr,
                           const DominatorTree *DT = nullptr,
                           bool UseInstrInfo = true);

/// Returns the known bits rather than passing by reference.
KnownBits computeKnownBits(const Value *V, const APInt &DemandedElts,
                           const DataLayout &DL, unsigned Depth = 0,
                           AssumptionCache *AC = nullptr,
                           const Instruction *CxtI = nullptr,
                           const DominatorTree *DT = nullptr,
                           bool UseInstrInfo = true);

KnownBits computeKnownBits(const Value *V, const APInt &DemandedElts,
                           unsigned Depth, const SimplifyQuery &Q);

KnownBits computeKnownBits(const Value *V, unsigned Depth,
                           const SimplifyQuery &Q);

void computeKnownBits(const Value *V, KnownBits &Known, unsigned Depth,
                      const SimplifyQuery &Q);

/// Compute known bits from the range metadata.
/// \p KnownZero the set of bits that are known to be zero
/// \p KnownOne the set of bits that are known to be one
void computeKnownBitsFromRangeMetadata(const MDNode &Ranges, KnownBits &Known);

/// Merge bits known from context-dependent facts into Known.
void computeKnownBitsFromContext(const Value *V, KnownBits &Known,
                                 unsigned Depth, const SimplifyQuery &Q);

/// Using KnownBits LHS/RHS produce the known bits for logic op (and/xor/or).
KnownBits analyzeKnownBitsFromAndXorOr(const Operator *I,
                                       const KnownBits &KnownLHS,
                                       const KnownBits &KnownRHS,
                                       unsigned Depth, const SimplifyQuery &SQ);

/// Adjust \p Known for the given select \p Arm to include information from the
/// select \p Cond.
void adjustKnownBitsForSelectArm(KnownBits &Known, Value *Cond, Value *Arm,
                                 bool Invert, unsigned Depth,
                                 const SimplifyQuery &Q);

/// Return true if LHS and RHS have no common bits set.
bool haveNoCommonBitsSet(const WithCache<const Value *> &LHSCache,
                         const WithCache<const Value *> &RHSCache,
                         const SimplifyQuery &SQ);

/// Return true if the given value is known to have exactly one bit set when
/// defined. For vectors return true if every element is known to be a power
/// of two when defined. Supports values with integer or pointer type and
/// vectors of integers. If 'OrZero' is set, then return true if the given
/// value is either a power of two or zero.
bool isKnownToBeAPowerOfTwo(const Value *V, const DataLayout &DL,
                            bool OrZero = false, unsigned Depth = 0,
                            AssumptionCache *AC = nullptr,
                            const Instruction *CxtI = nullptr,
                            const DominatorTree *DT = nullptr,
                            bool UseInstrInfo = true);

bool isOnlyUsedInZeroComparison(const Instruction *CxtI);

bool isOnlyUsedInZeroEqualityComparison(const Instruction *CxtI);

/// Return true if the given value is known to be non-zero when defined. For
/// vectors, return true if every element is known to be non-zero when
/// defined. For pointers, if the context instruction and dominator tree are
/// specified, perform context-sensitive analysis and return true if the
/// pointer couldn't possibly be null at the specified instruction.
/// Supports values with integer or pointer type and vectors of integers.
bool isKnownNonZero(const Value *V, const SimplifyQuery &Q, unsigned Depth = 0);

/// Return true if the two given values are negation.
/// Currently can recoginze Value pair:
/// 1: <X, Y> if X = sub (0, Y) or Y = sub (0, X)
/// 2: <X, Y> if X = sub (A, B) and Y = sub (B, A)
bool isKnownNegation(const Value *X, const Value *Y, bool NeedNSW = false,
                     bool AllowPoison = true);

/// Return true iff:
/// 1. X is poison implies Y is poison.
/// 2. X is true implies Y is false.
/// 3. X is false implies Y is true.
/// Otherwise, return false.
bool isKnownInversion(const Value *X, const Value *Y);

/// Returns true if the give value is known to be non-negative.
bool isKnownNonNegative(const Value *V, const SimplifyQuery &SQ,
                        unsigned Depth = 0);

/// Returns true if the given value is known be positive (i.e. non-negative
/// and non-zero).
bool isKnownPositive(const Value *V, const SimplifyQuery &SQ,
                     unsigned Depth = 0);

/// Returns true if the given value is known be negative (i.e. non-positive
/// and non-zero).
bool isKnownNegative(const Value *V, const SimplifyQuery &DL,
                     unsigned Depth = 0);

/// Return true if the given values are known to be non-equal when defined.
/// Supports scalar integer types only.
bool isKnownNonEqual(const Value *V1, const Value *V2, const DataLayout &DL,
                     AssumptionCache *AC = nullptr,
                     const Instruction *CxtI = nullptr,
                     const DominatorTree *DT = nullptr,
                     bool UseInstrInfo = true);

/// Return true if 'V & Mask' is known to be zero. We use this predicate to
/// simplify operations downstream. Mask is known to be zero for bits that V
/// cannot have.
///
/// This function is defined on values with integer type, values with pointer
/// type, and vectors of integers.  In the case
/// where V is a vector, the mask, known zero, and known one values are the
/// same width as the vector element, and the bit is set only if it is true
/// for all of the elements in the vector.
bool MaskedValueIsZero(const Value *V, const APInt &Mask,
                       const SimplifyQuery &DL, unsigned Depth = 0);

/// Return the number of times the sign bit of the register is replicated into
/// the other bits. We know that at least 1 bit is always equal to the sign
/// bit (itself), but other cases can give us information. For example,
/// immediately after an "ashr X, 2", we know that the top 3 bits are all
/// equal to each other, so we return 3. For vectors, return the number of
/// sign bits for the vector element with the mininum number of known sign
/// bits.
unsigned ComputeNumSignBits(const Value *Op, const DataLayout &DL,
                            unsigned Depth = 0, AssumptionCache *AC = nullptr,
                            const Instruction *CxtI = nullptr,
                            const DominatorTree *DT = nullptr,
                            bool UseInstrInfo = true);

/// Get the upper bound on bit size for this Value \p Op as a signed integer.
/// i.e.  x == sext(trunc(x to MaxSignificantBits) to bitwidth(x)).
/// Similar to the APInt::getSignificantBits function.
unsigned ComputeMaxSignificantBits(const Value *Op, const DataLayout &DL,
                                   unsigned Depth = 0,
                                   AssumptionCache *AC = nullptr,
                                   const Instruction *CxtI = nullptr,
                                   const DominatorTree *DT = nullptr);

/// Map a call instruction to an intrinsic ID.  Libcalls which have equivalent
/// intrinsics are treated as-if they were intrinsics.
Intrinsic::ID getIntrinsicForCallSite(const CallBase &CB,
                                      const TargetLibraryInfo *TLI);

/// Given an exploded icmp instruction, return true if the comparison only
/// checks the sign bit. If it only checks the sign bit, set TrueIfSigned if
/// the result of the comparison is true when the input value is signed.
bool isSignBitCheck(ICmpInst::Predicate Pred, const APInt &RHS,
                    bool &TrueIfSigned);

/// Returns a pair of values, which if passed to llvm.is.fpclass, returns the
/// same result as an fcmp with the given operands.
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask.
///
/// If \p LookThroughSrc is false, ignore the source value (i.e. the first pair
/// element will always be LHS.
std::pair<Value *, FPClassTest> fcmpToClassTest(CmpInst::Predicate Pred,
                                                const Function &F, Value *LHS,
                                                Value *RHS,
                                                bool LookThroughSrc = true);
std::pair<Value *, FPClassTest> fcmpToClassTest(CmpInst::Predicate Pred,
                                                const Function &F, Value *LHS,
                                                const APFloat *ConstRHS,
                                                bool LookThroughSrc = true);

/// Compute the possible floating-point classes that \p LHS could be based on
/// fcmp \Pred \p LHS, \p RHS.
///
/// \returns { TestedValue, ClassesIfTrue, ClassesIfFalse }
///
/// If the compare returns an exact class test, ClassesIfTrue == ~ClassesIfFalse
///
/// This is a less exact version of fcmpToClassTest (e.g. fcmpToClassTest will
/// only succeed for a test of x > 0 implies positive, but not x > 1).
///
/// If \p LookThroughSrc is true, consider the input value when computing the
/// mask. This may look through sign bit operations.
///
/// If \p LookThroughSrc is false, ignore the source value (i.e. the first pair
/// element will always be LHS.
///
std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 Value *RHS, bool LookThroughSrc = true);
std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 FPClassTest RHS, bool LookThroughSrc = true);
std::tuple<Value *, FPClassTest, FPClassTest>
fcmpImpliesClass(CmpInst::Predicate Pred, const Function &F, Value *LHS,
                 const APFloat &RHS, bool LookThroughSrc = true);

struct KnownFPClass {
  /// Floating-point classes the value could be one of.
  FPClassTest KnownFPClasses = fcAllFlags;

  /// std::nullopt if the sign bit is unknown, true if the sign bit is
  /// definitely set or false if the sign bit is definitely unset.
  std::optional<bool> SignBit;

  bool operator==(KnownFPClass Other) const {
    return KnownFPClasses == Other.KnownFPClasses && SignBit == Other.SignBit;
  }

  /// Return true if it's known this can never be one of the mask entries.
  bool isKnownNever(FPClassTest Mask) const {
    return (KnownFPClasses & Mask) == fcNone;
  }

  bool isKnownAlways(FPClassTest Mask) const { return isKnownNever(~Mask); }

  bool isUnknown() const {
    return KnownFPClasses == fcAllFlags && !SignBit;
  }

  /// Return true if it's known this can never be a nan.
  bool isKnownNeverNaN() const {
    return isKnownNever(fcNan);
  }

  /// Return true if it's known this must always be a nan.
  bool isKnownAlwaysNaN() const { return isKnownAlways(fcNan); }

  /// Return true if it's known this can never be an infinity.
  bool isKnownNeverInfinity() const {
    return isKnownNever(fcInf);
  }

  /// Return true if it's known this can never be +infinity.
  bool isKnownNeverPosInfinity() const {
    return isKnownNever(fcPosInf);
  }

  /// Return true if it's known this can never be -infinity.
  bool isKnownNeverNegInfinity() const {
    return isKnownNever(fcNegInf);
  }

  /// Return true if it's known this can never be a subnormal
  bool isKnownNeverSubnormal() const {
    return isKnownNever(fcSubnormal);
  }

  /// Return true if it's known this can never be a positive subnormal
  bool isKnownNeverPosSubnormal() const {
    return isKnownNever(fcPosSubnormal);
  }

  /// Return true if it's known this can never be a negative subnormal
  bool isKnownNeverNegSubnormal() const {
    return isKnownNever(fcNegSubnormal);
  }

  /// Return true if it's known this can never be a zero. This means a literal
  /// [+-]0, and does not include denormal inputs implicitly treated as [+-]0.
  bool isKnownNeverZero() const {
    return isKnownNever(fcZero);
  }

  /// Return true if it's known this can never be a literal positive zero.
  bool isKnownNeverPosZero() const {
    return isKnownNever(fcPosZero);
  }

  /// Return true if it's known this can never be a negative zero. This means a
  /// literal -0 and does not include denormal inputs implicitly treated as -0.
  bool isKnownNeverNegZero() const {
    return isKnownNever(fcNegZero);
  }

  /// Return true if it's know this can never be interpreted as a zero. This
  /// extends isKnownNeverZero to cover the case where the assumed
  /// floating-point mode for the function interprets denormals as zero.
  bool isKnownNeverLogicalZero(const Function &F, Type *Ty) const;

  /// Return true if it's know this can never be interpreted as a negative zero.
  bool isKnownNeverLogicalNegZero(const Function &F, Type *Ty) const;

  /// Return true if it's know this can never be interpreted as a positive zero.
  bool isKnownNeverLogicalPosZero(const Function &F, Type *Ty) const;

  static constexpr FPClassTest OrderedLessThanZeroMask =
      fcNegSubnormal | fcNegNormal | fcNegInf;
  static constexpr FPClassTest OrderedGreaterThanZeroMask =
      fcPosSubnormal | fcPosNormal | fcPosInf;

  /// Return true if we can prove that the analyzed floating-point value is
  /// either NaN or never less than -0.0.
  ///
  ///      NaN --> true
  ///       +0 --> true
  ///       -0 --> true
  ///   x > +0 --> true
  ///   x < -0 --> false
  bool cannotBeOrderedLessThanZero() const {
    return isKnownNever(OrderedLessThanZeroMask);
  }

  /// Return true if we can prove that the analyzed floating-point value is
  /// either NaN or never greater than -0.0.
  ///      NaN --> true
  ///       +0 --> true
  ///       -0 --> true
  ///   x > +0 --> false
  ///   x < -0 --> true
  bool cannotBeOrderedGreaterThanZero() const {
    return isKnownNever(OrderedGreaterThanZeroMask);
  }

  KnownFPClass &operator|=(const KnownFPClass &RHS) {
    KnownFPClasses = KnownFPClasses | RHS.KnownFPClasses;

    if (SignBit != RHS.SignBit)
      SignBit = std::nullopt;
    return *this;
  }

  void knownNot(FPClassTest RuleOut) {
    KnownFPClasses = KnownFPClasses & ~RuleOut;
    if (isKnownNever(fcNan) && !SignBit) {
      if (isKnownNever(fcNegative))
        SignBit = false;
      else if (isKnownNever(fcPositive))
        SignBit = true;
    }
  }

  void fneg() {
    KnownFPClasses = llvm::fneg(KnownFPClasses);
    if (SignBit)
      SignBit = !*SignBit;
  }

  void fabs() {
    if (KnownFPClasses & fcNegZero)
      KnownFPClasses |= fcPosZero;

    if (KnownFPClasses & fcNegInf)
      KnownFPClasses |= fcPosInf;

    if (KnownFPClasses & fcNegSubnormal)
      KnownFPClasses |= fcPosSubnormal;

    if (KnownFPClasses & fcNegNormal)
      KnownFPClasses |= fcPosNormal;

    signBitMustBeZero();
  }

  /// Return true if the sign bit must be 0, ignoring the sign of nans.
  bool signBitIsZeroOrNaN() const {
    return isKnownNever(fcNegative);
  }

  /// Assume the sign bit is zero.
  void signBitMustBeZero() {
    KnownFPClasses &= (fcPositive | fcNan);
    SignBit = false;
  }

  /// Assume the sign bit is one.
  void signBitMustBeOne() {
    KnownFPClasses &= (fcNegative | fcNan);
    SignBit = true;
  }

  void copysign(const KnownFPClass &Sign) {
    // Don't know anything about the sign of the source. Expand the possible set
    // to its opposite sign pair.
    if (KnownFPClasses & fcZero)
      KnownFPClasses |= fcZero;
    if (KnownFPClasses & fcSubnormal)
      KnownFPClasses |= fcSubnormal;
    if (KnownFPClasses & fcNormal)
      KnownFPClasses |= fcNormal;
    if (KnownFPClasses & fcInf)
      KnownFPClasses |= fcInf;

    // Sign bit is exactly preserved even for nans.
    SignBit = Sign.SignBit;

    // Clear sign bits based on the input sign mask.
    if (Sign.isKnownNever(fcPositive | fcNan) || (SignBit && *SignBit))
      KnownFPClasses &= (fcNegative | fcNan);
    if (Sign.isKnownNever(fcNegative | fcNan) || (SignBit && !*SignBit))
      KnownFPClasses &= (fcPositive | fcNan);
  }

  // Propagate knowledge that a non-NaN source implies the result can also not
  // be a NaN. For unconstrained operations, signaling nans are not guaranteed
  // to be quieted but cannot be introduced.
  void propagateNaN(const KnownFPClass &Src, bool PreserveSign = false) {
    if (Src.isKnownNever(fcNan)) {
      knownNot(fcNan);
      if (PreserveSign)
        SignBit = Src.SignBit;
    } else if (Src.isKnownNever(fcSNan))
      knownNot(fcSNan);
  }

  /// Propagate knowledge from a source value that could be a denormal or
  /// zero. We have to be conservative since output flushing is not guaranteed,
  /// so known-never-zero may not hold.
  ///
  /// This assumes a copy-like operation and will replace any currently known
  /// information.
  void propagateDenormal(const KnownFPClass &Src, const Function &F, Type *Ty);

  /// Report known classes if \p Src is evaluated through a potentially
  /// canonicalizing operation. We can assume signaling nans will not be
  /// introduced, but cannot assume a denormal will be flushed under FTZ/DAZ.
  ///
  /// This assumes a copy-like operation and will replace any currently known
  /// information.
  void propagateCanonicalizingSrc(const KnownFPClass &Src, const Function &F,
                                  Type *Ty);

  void resetAll() { *this = KnownFPClass(); }
};

inline KnownFPClass operator|(KnownFPClass LHS, const KnownFPClass &RHS) {
  LHS |= RHS;
  return LHS;
}

inline KnownFPClass operator|(const KnownFPClass &LHS, KnownFPClass &&RHS) {
  RHS |= LHS;
  return std::move(RHS);
}

/// Determine which floating-point classes are valid for \p V, and return them
/// in KnownFPClass bit sets.
///
/// This function is defined on values with floating-point type, values vectors
/// of floating-point type, and arrays of floating-point type.

/// \p InterestedClasses is a compile time optimization hint for which floating
/// point classes should be queried. Queries not specified in \p
/// InterestedClasses should be reliable if they are determined during the
/// query.
KnownFPClass computeKnownFPClass(const Value *V, const APInt &DemandedElts,
                                 FPClassTest InterestedClasses, unsigned Depth,
                                 const SimplifyQuery &SQ);

KnownFPClass computeKnownFPClass(const Value *V, FPClassTest InterestedClasses,
                                 unsigned Depth, const SimplifyQuery &SQ);

inline KnownFPClass computeKnownFPClass(
    const Value *V, const DataLayout &DL,
    FPClassTest InterestedClasses = fcAllFlags, unsigned Depth = 0,
    const TargetLibraryInfo *TLI = nullptr, AssumptionCache *AC = nullptr,
    const Instruction *CxtI = nullptr, const DominatorTree *DT = nullptr,
    bool UseInstrInfo = true) {
  return computeKnownFPClass(
      V, InterestedClasses, Depth,
      SimplifyQuery(DL, TLI, DT, AC, CxtI, UseInstrInfo));
}

/// Wrapper to account for known fast math flags at the use instruction.
inline KnownFPClass
computeKnownFPClass(const Value *V, const APInt &DemandedElts,
                    FastMathFlags FMF, FPClassTest InterestedClasses,
                    unsigned Depth, const SimplifyQuery &SQ) {
  if (FMF.noNaNs())
    InterestedClasses &= ~fcNan;
  if (FMF.noInfs())
    InterestedClasses &= ~fcInf;

  KnownFPClass Result =
      computeKnownFPClass(V, DemandedElts, InterestedClasses, Depth, SQ);

  if (FMF.noNaNs())
    Result.KnownFPClasses &= ~fcNan;
  if (FMF.noInfs())
    Result.KnownFPClasses &= ~fcInf;
  return Result;
}

inline KnownFPClass computeKnownFPClass(const Value *V, FastMathFlags FMF,
                                        FPClassTest InterestedClasses,
                                        unsigned Depth,
                                        const SimplifyQuery &SQ) {
  auto *FVTy = dyn_cast<FixedVectorType>(V->getType());
  APInt DemandedElts =
      FVTy ? APInt::getAllOnes(FVTy->getNumElements()) : APInt(1, 1);
  return computeKnownFPClass(V, DemandedElts, FMF, InterestedClasses, Depth,
                             SQ);
}

/// Return true if we can prove that the specified FP value is never equal to
/// -0.0. Users should use caution when considering PreserveSign
/// denormal-fp-math.
inline bool cannotBeNegativeZero(const Value *V, unsigned Depth,
                                 const SimplifyQuery &SQ) {
  KnownFPClass Known = computeKnownFPClass(V, fcNegZero, Depth, SQ);
  return Known.isKnownNeverNegZero();
}

/// Return true if we can prove that the specified FP value is either NaN or
/// never less than -0.0.
///
///      NaN --> true
///       +0 --> true
///       -0 --> true
///   x > +0 --> true
///   x < -0 --> false
inline bool cannotBeOrderedLessThanZero(const Value *V, unsigned Depth,
                                        const SimplifyQuery &SQ) {
  KnownFPClass Known =
      computeKnownFPClass(V, KnownFPClass::OrderedLessThanZeroMask, Depth, SQ);
  return Known.cannotBeOrderedLessThanZero();
}

/// Return true if the floating-point scalar value is not an infinity or if
/// the floating-point vector value has no infinities. Return false if a value
/// could ever be infinity.
inline bool isKnownNeverInfinity(const Value *V, unsigned Depth,
                                 const SimplifyQuery &SQ) {
  KnownFPClass Known = computeKnownFPClass(V, fcInf, Depth, SQ);
  return Known.isKnownNeverInfinity();
}

/// Return true if the floating-point value can never contain a NaN or infinity.
inline bool isKnownNeverInfOrNaN(const Value *V, unsigned Depth,
                                 const SimplifyQuery &SQ) {
  KnownFPClass Known = computeKnownFPClass(V, fcInf | fcNan, Depth, SQ);
  return Known.isKnownNeverNaN() && Known.isKnownNeverInfinity();
}

/// Return true if the floating-point scalar value is not a NaN or if the
/// floating-point vector value has no NaN elements. Return false if a value
/// could ever be NaN.
inline bool isKnownNeverNaN(const Value *V, unsigned Depth,
                            const SimplifyQuery &SQ) {
  KnownFPClass Known = computeKnownFPClass(V, fcNan, Depth, SQ);
  return Known.isKnownNeverNaN();
}

/// Return false if we can prove that the specified FP value's sign bit is 0.
/// Return true if we can prove that the specified FP value's sign bit is 1.
/// Otherwise return std::nullopt.
inline std::optional<bool> computeKnownFPSignBit(const Value *V, unsigned Depth,
                                                 const SimplifyQuery &SQ) {
  KnownFPClass Known = computeKnownFPClass(V, fcAllFlags, Depth, SQ);
  return Known.SignBit;
}

/// If the specified value can be set by repeating the same byte in memory,
/// return the i8 value that it is represented with. This is true for all i8
/// values obviously, but is also true for i32 0, i32 -1, i16 0xF0F0, double
/// 0.0 etc. If the value can't be handled with a repeated byte store (e.g.
/// i16 0x1234), return null. If the value is entirely undef and padding,
/// return undef.
Value *isBytewiseValue(Value *V, const DataLayout &DL);

/// Given an aggregate and an sequence of indices, see if the scalar value
/// indexed is already around as a register, for example if it were inserted
/// directly into the aggregate.
///
/// If InsertBefore is not empty, this function will duplicate (modified)
/// insertvalues when a part of a nested struct is extracted.
Value *FindInsertedValue(
    Value *V, ArrayRef<unsigned> idx_range,
    std::optional<BasicBlock::iterator> InsertBefore = std::nullopt);

/// Analyze the specified pointer to see if it can be expressed as a base
/// pointer plus a constant offset. Return the base and offset to the caller.
///
/// This is a wrapper around Value::stripAndAccumulateConstantOffsets that
/// creates and later unpacks the required APInt.
inline Value *GetPointerBaseWithConstantOffset(Value *Ptr, int64_t &Offset,
                                               const DataLayout &DL,
                                               bool AllowNonInbounds = true) {
  APInt OffsetAPInt(DL.getIndexTypeSizeInBits(Ptr->getType()), 0);
  Value *Base =
      Ptr->stripAndAccumulateConstantOffsets(DL, OffsetAPInt, AllowNonInbounds);

  Offset = OffsetAPInt.getSExtValue();
  return Base;
}
inline const Value *
GetPointerBaseWithConstantOffset(const Value *Ptr, int64_t &Offset,
                                 const DataLayout &DL,
                                 bool AllowNonInbounds = true) {
  return GetPointerBaseWithConstantOffset(const_cast<Value *>(Ptr), Offset, DL,
                                          AllowNonInbounds);
}

/// Returns true if the GEP is based on a pointer to a string (array of
// \p CharSize integers) and is indexing into this string.
bool isGEPBasedOnPointerToString(const GEPOperator *GEP, unsigned CharSize = 8);

/// Represents offset+length into a ConstantDataArray.
struct ConstantDataArraySlice {
  /// ConstantDataArray pointer. nullptr indicates a zeroinitializer (a valid
  /// initializer, it just doesn't fit the ConstantDataArray interface).
  const ConstantDataArray *Array;

  /// Slice starts at this Offset.
  uint64_t Offset;

  /// Length of the slice.
  uint64_t Length;

  /// Moves the Offset and adjusts Length accordingly.
  void move(uint64_t Delta) {
    assert(Delta < Length);
    Offset += Delta;
    Length -= Delta;
  }

  /// Convenience accessor for elements in the slice.
  uint64_t operator[](unsigned I) const {
    return Array == nullptr ? 0 : Array->getElementAsInteger(I + Offset);
  }
};

/// Returns true if the value \p V is a pointer into a ConstantDataArray.
/// If successful \p Slice will point to a ConstantDataArray info object
/// with an appropriate offset.
bool getConstantDataArrayInfo(const Value *V, ConstantDataArraySlice &Slice,
                              unsigned ElementSize, uint64_t Offset = 0);

/// This function computes the length of a null-terminated C string pointed to
/// by V. If successful, it returns true and returns the string in Str. If
/// unsuccessful, it returns false. This does not include the trailing null
/// character by default. If TrimAtNul is set to false, then this returns any
/// trailing null characters as well as any other characters that come after
/// it.
bool getConstantStringInfo(const Value *V, StringRef &Str,
                           bool TrimAtNul = true);

/// If we can compute the length of the string pointed to by the specified
/// pointer, return 'len+1'.  If we can't, return 0.
uint64_t GetStringLength(const Value *V, unsigned CharSize = 8);

/// This function returns call pointer argument that is considered the same by
/// aliasing rules. You CAN'T use it to replace one value with another. If
/// \p MustPreserveNullness is true, the call must preserve the nullness of
/// the pointer.
const Value *getArgumentAliasingToReturnedPointer(const CallBase *Call,
                                                  bool MustPreserveNullness);
inline Value *getArgumentAliasingToReturnedPointer(CallBase *Call,
                                                   bool MustPreserveNullness) {
  return const_cast<Value *>(getArgumentAliasingToReturnedPointer(
      const_cast<const CallBase *>(Call), MustPreserveNullness));
}

/// {launder,strip}.invariant.group returns pointer that aliases its argument,
/// and it only captures pointer by returning it.
/// These intrinsics are not marked as nocapture, because returning is
/// considered as capture. The arguments are not marked as returned neither,
/// because it would make it useless. If \p MustPreserveNullness is true,
/// the intrinsic must preserve the nullness of the pointer.
bool isIntrinsicReturningPointerAliasingArgumentWithoutCapturing(
    const CallBase *Call, bool MustPreserveNullness);

/// This method strips off any GEP address adjustments, pointer casts
/// or `llvm.threadlocal.address` from the specified value \p V, returning the
/// original object being addressed. Note that the returned value has pointer
/// type if the specified value does. If the \p MaxLookup value is non-zero, it
/// limits the number of instructions to be stripped off.
const Value *getUnderlyingObject(const Value *V, unsigned MaxLookup = 6);
inline Value *getUnderlyingObject(Value *V, unsigned MaxLookup = 6) {
  // Force const to avoid infinite recursion.
  const Value *VConst = V;
  return const_cast<Value *>(getUnderlyingObject(VConst, MaxLookup));
}

/// Like getUnderlyingObject(), but will try harder to find a single underlying
/// object. In particular, this function also looks through selects and phis.
const Value *getUnderlyingObjectAggressive(const Value *V);

/// This method is similar to getUnderlyingObject except that it can
/// look through phi and select instructions and return multiple objects.
///
/// If LoopInfo is passed, loop phis are further analyzed.  If a pointer
/// accesses different objects in each iteration, we don't look through the
/// phi node. E.g. consider this loop nest:
///
///   int **A;
///   for (i)
///     for (j) {
///        A[i][j] = A[i-1][j] * B[j]
///     }
///
/// This is transformed by Load-PRE to stash away A[i] for the next iteration
/// of the outer loop:
///
///   Curr = A[0];          // Prev_0
///   for (i: 1..N) {
///     Prev = Curr;        // Prev = PHI (Prev_0, Curr)
///     Curr = A[i];
///     for (j: 0..N) {
///        Curr[j] = Prev[j] * B[j]
///     }
///   }
///
/// Since A[i] and A[i-1] are independent pointers, getUnderlyingObjects
/// should not assume that Curr and Prev share the same underlying object thus
/// it shouldn't look through the phi above.
void getUnderlyingObjects(const Value *V,
                          SmallVectorImpl<const Value *> &Objects,
                          LoopInfo *LI = nullptr, unsigned MaxLookup = 6);

/// This is a wrapper around getUnderlyingObjects and adds support for basic
/// ptrtoint+arithmetic+inttoptr sequences.
bool getUnderlyingObjectsForCodeGen(const Value *V,
                                    SmallVectorImpl<Value *> &Objects);

/// Returns unique alloca where the value comes from, or nullptr.
/// If OffsetZero is true check that V points to the begining of the alloca.
AllocaInst *findAllocaForValue(Value *V, bool OffsetZero = false);
inline const AllocaInst *findAllocaForValue(const Value *V,
                                            bool OffsetZero = false) {
  return findAllocaForValue(const_cast<Value *>(V), OffsetZero);
}

/// Return true if the only users of this pointer are lifetime markers.
bool onlyUsedByLifetimeMarkers(const Value *V);

/// Return true if the only users of this pointer are lifetime markers or
/// droppable instructions.
bool onlyUsedByLifetimeMarkersOrDroppableInsts(const Value *V);

/// Return true if speculation of the given load must be suppressed to avoid
/// ordering or interfering with an active sanitizer.  If not suppressed,
/// dereferenceability and alignment must be proven separately.  Note: This
/// is only needed for raw reasoning; if you use the interface below
/// (isSafeToSpeculativelyExecute), this is handled internally.
bool mustSuppressSpeculation(const LoadInst &LI);

/// Return true if the instruction does not have any effects besides
/// calculating the result and does not have undefined behavior.
///
/// This method never returns true for an instruction that returns true for
/// mayHaveSideEffects; however, this method also does some other checks in
/// addition. It checks for undefined behavior, like dividing by zero or
/// loading from an invalid pointer (but not for undefined results, like a
/// shift with a shift amount larger than the width of the result). It checks
/// for malloc and alloca because speculatively executing them might cause a
/// memory leak. It also returns false for instructions related to control
/// flow, specifically terminators and PHI nodes.
///
/// If the CtxI is specified this method performs context-sensitive analysis
/// and returns true if it is safe to execute the instruction immediately
/// before the CtxI.
///
/// If the CtxI is NOT specified this method only looks at the instruction
/// itself and its operands, so if this method returns true, it is safe to
/// move the instruction as long as the correct dominance relationships for
/// the operands and users hold.
///
/// This method can return true for instructions that read memory;
/// for such instructions, moving them may change the resulting value.
bool isSafeToSpeculativelyExecute(const Instruction *I,
                                  const Instruction *CtxI = nullptr,
                                  AssumptionCache *AC = nullptr,
                                  const DominatorTree *DT = nullptr,
                                  const TargetLibraryInfo *TLI = nullptr,
                                  bool UseVariableInfo = true);

inline bool isSafeToSpeculativelyExecute(const Instruction *I,
                                         BasicBlock::iterator CtxI,
                                         AssumptionCache *AC = nullptr,
                                         const DominatorTree *DT = nullptr,
                                         const TargetLibraryInfo *TLI = nullptr,
                                         bool UseVariableInfo = true) {
  // Take an iterator, and unwrap it into an Instruction *.
  return isSafeToSpeculativelyExecute(I, &*CtxI, AC, DT, TLI, UseVariableInfo);
}

/// Don't use information from its non-constant operands. This helper is used
/// when its operands are going to be replaced.
inline bool
isSafeToSpeculativelyExecuteWithVariableReplaced(const Instruction *I) {
  return isSafeToSpeculativelyExecute(I, nullptr, nullptr, nullptr, nullptr,
                                      /*UseVariableInfo=*/false);
}

/// This returns the same result as isSafeToSpeculativelyExecute if Opcode is
/// the actual opcode of Inst. If the provided and actual opcode differ, the
/// function (virtually) overrides the opcode of Inst with the provided
/// Opcode. There are come constraints in this case:
/// * If Opcode has a fixed number of operands (eg, as binary operators do),
///   then Inst has to have at least as many leading operands. The function
///   will ignore all trailing operands beyond that number.
/// * If Opcode allows for an arbitrary number of operands (eg, as CallInsts
///   do), then all operands are considered.
/// * The virtual instruction has to satisfy all typing rules of the provided
///   Opcode.
/// * This function is pessimistic in the following sense: If one actually
///   materialized the virtual instruction, then isSafeToSpeculativelyExecute
///   may say that the materialized instruction is speculatable whereas this
///   function may have said that the instruction wouldn't be speculatable.
///   This behavior is a shortcoming in the current implementation and not
///   intentional.
bool isSafeToSpeculativelyExecuteWithOpcode(
    unsigned Opcode, const Instruction *Inst, const Instruction *CtxI = nullptr,
    AssumptionCache *AC = nullptr, const DominatorTree *DT = nullptr,
    const TargetLibraryInfo *TLI = nullptr, bool UseVariableInfo = true);

/// Returns true if the result or effects of the given instructions \p I
/// depend values not reachable through the def use graph.
/// * Memory dependence arises for example if the instruction reads from
///   memory or may produce effects or undefined behaviour. Memory dependent
///   instructions generally cannot be reorderd with respect to other memory
///   dependent instructions.
/// * Control dependence arises for example if the instruction may fault
///   if lifted above a throwing call or infinite loop.
bool mayHaveNonDefUseDependency(const Instruction &I);

/// Return true if it is an intrinsic that cannot be speculated but also
/// cannot trap.
bool isAssumeLikeIntrinsic(const Instruction *I);

/// Return true if it is valid to use the assumptions provided by an
/// assume intrinsic, I, at the point in the control-flow identified by the
/// context instruction, CxtI. By default, ephemeral values of the assumption
/// are treated as an invalid context, to prevent the assumption from being used
/// to optimize away its argument. If the caller can ensure that this won't
/// happen, it can call with AllowEphemerals set to true to get more valid
/// assumptions.
bool isValidAssumeForContext(const Instruction *I, const Instruction *CxtI,
                             const DominatorTree *DT = nullptr,
                             bool AllowEphemerals = false);

enum class OverflowResult {
  /// Always overflows in the direction of signed/unsigned min value.
  AlwaysOverflowsLow,
  /// Always overflows in the direction of signed/unsigned max value.
  AlwaysOverflowsHigh,
  /// May or may not overflow.
  MayOverflow,
  /// Never overflows.
  NeverOverflows,
};

OverflowResult computeOverflowForUnsignedMul(const Value *LHS, const Value *RHS,
                                             const SimplifyQuery &SQ,
                                             bool IsNSW = false);
OverflowResult computeOverflowForSignedMul(const Value *LHS, const Value *RHS,
                                           const SimplifyQuery &SQ);
OverflowResult
computeOverflowForUnsignedAdd(const WithCache<const Value *> &LHS,
                              const WithCache<const Value *> &RHS,
                              const SimplifyQuery &SQ);
OverflowResult computeOverflowForSignedAdd(const WithCache<const Value *> &LHS,
                                           const WithCache<const Value *> &RHS,
                                           const SimplifyQuery &SQ);
/// This version also leverages the sign bit of Add if known.
OverflowResult computeOverflowForSignedAdd(const AddOperator *Add,
                                           const SimplifyQuery &SQ);
OverflowResult computeOverflowForUnsignedSub(const Value *LHS, const Value *RHS,
                                             const SimplifyQuery &SQ);
OverflowResult computeOverflowForSignedSub(const Value *LHS, const Value *RHS,
                                           const SimplifyQuery &SQ);

/// Returns true if the arithmetic part of the \p WO 's result is
/// used only along the paths control dependent on the computation
/// not overflowing, \p WO being an <op>.with.overflow intrinsic.
bool isOverflowIntrinsicNoWrap(const WithOverflowInst *WO,
                               const DominatorTree &DT);

/// Determine the possible constant range of vscale with the given bit width,
/// based on the vscale_range function attribute.
ConstantRange getVScaleRange(const Function *F, unsigned BitWidth);

/// Determine the possible constant range of an integer or vector of integer
/// value. This is intended as a cheap, non-recursive check.
ConstantRange computeConstantRange(const Value *V, bool ForSigned,
                                   bool UseInstrInfo = true,
                                   AssumptionCache *AC = nullptr,
                                   const Instruction *CtxI = nullptr,
                                   const DominatorTree *DT = nullptr,
                                   unsigned Depth = 0);

/// Combine constant ranges from computeConstantRange() and computeKnownBits().
ConstantRange
computeConstantRangeIncludingKnownBits(const WithCache<const Value *> &V,
                                       bool ForSigned, const SimplifyQuery &SQ);

/// Return true if this function can prove that the instruction I will
/// always transfer execution to one of its successors (including the next
/// instruction that follows within a basic block). E.g. this is not
/// guaranteed for function calls that could loop infinitely.
///
/// In other words, this function returns false for instructions that may
/// transfer execution or fail to transfer execution in a way that is not
/// captured in the CFG nor in the sequence of instructions within a basic
/// block.
///
/// Undefined behavior is assumed not to happen, so e.g. division is
/// guaranteed to transfer execution to the following instruction even
/// though division by zero might cause undefined behavior.
bool isGuaranteedToTransferExecutionToSuccessor(const Instruction *I);

/// Returns true if this block does not contain a potential implicit exit.
/// This is equivelent to saying that all instructions within the basic block
/// are guaranteed to transfer execution to their successor within the basic
/// block. This has the same assumptions w.r.t. undefined behavior as the
/// instruction variant of this function.
bool isGuaranteedToTransferExecutionToSuccessor(const BasicBlock *BB);

/// Return true if every instruction in the range (Begin, End) is
/// guaranteed to transfer execution to its static successor. \p ScanLimit
/// bounds the search to avoid scanning huge blocks.
bool isGuaranteedToTransferExecutionToSuccessor(
    BasicBlock::const_iterator Begin, BasicBlock::const_iterator End,
    unsigned ScanLimit = 32);

/// Same as previous, but with range expressed via iterator_range.
bool isGuaranteedToTransferExecutionToSuccessor(
    iterator_range<BasicBlock::const_iterator> Range, unsigned ScanLimit = 32);

/// Return true if this function can prove that the instruction I
/// is executed for every iteration of the loop L.
///
/// Note that this currently only considers the loop header.
bool isGuaranteedToExecuteForEveryIteration(const Instruction *I,
                                            const Loop *L);

/// Return true if \p PoisonOp's user yields poison or raises UB if its
/// operand \p PoisonOp is poison.
///
/// If \p PoisonOp is a vector or an aggregate and the operation's result is a
/// single value, any poison element in /p PoisonOp should make the result
/// poison or raise UB.
///
/// To filter out operands that raise UB on poison, you can use
/// getGuaranteedNonPoisonOp.
bool propagatesPoison(const Use &PoisonOp);

/// Insert operands of I into Ops such that I will trigger undefined behavior
/// if I is executed and that operand has a poison value.
void getGuaranteedNonPoisonOps(const Instruction *I,
                               SmallVectorImpl<const Value *> &Ops);

/// Insert operands of I into Ops such that I will trigger undefined behavior
/// if I is executed and that operand is not a well-defined value
/// (i.e. has undef bits or poison).
void getGuaranteedWellDefinedOps(const Instruction *I,
                                 SmallVectorImpl<const Value *> &Ops);

/// Return true if the given instruction must trigger undefined behavior
/// when I is executed with any operands which appear in KnownPoison holding
/// a poison value at the point of execution.
bool mustTriggerUB(const Instruction *I,
                   const SmallPtrSetImpl<const Value *> &KnownPoison);

/// Return true if this function can prove that if Inst is executed
/// and yields a poison value or undef bits, then that will trigger
/// undefined behavior.
///
/// Note that this currently only considers the basic block that is
/// the parent of Inst.
bool programUndefinedIfUndefOrPoison(const Instruction *Inst);
bool programUndefinedIfPoison(const Instruction *Inst);

/// canCreateUndefOrPoison returns true if Op can create undef or poison from
/// non-undef & non-poison operands.
/// For vectors, canCreateUndefOrPoison returns true if there is potential
/// poison or undef in any element of the result when vectors without
/// undef/poison poison are given as operands.
/// For example, given `Op = shl <2 x i32> %x, <0, 32>`, this function returns
/// true. If Op raises immediate UB but never creates poison or undef
/// (e.g. sdiv I, 0), canCreatePoison returns false.
///
/// \p ConsiderFlagsAndMetadata controls whether poison producing flags and
/// metadata on the instruction are considered.  This can be used to see if the
/// instruction could still introduce undef or poison even without poison
/// generating flags and metadata which might be on the instruction.
/// (i.e. could the result of Op->dropPoisonGeneratingFlags() still create
/// poison or undef)
///
/// canCreatePoison returns true if Op can create poison from non-poison
/// operands.
bool canCreateUndefOrPoison(const Operator *Op,
                            bool ConsiderFlagsAndMetadata = true);
bool canCreatePoison(const Operator *Op, bool ConsiderFlagsAndMetadata = true);

/// Return true if V is poison given that ValAssumedPoison is already poison.
/// For example, if ValAssumedPoison is `icmp X, 10` and V is `icmp X, 5`,
/// impliesPoison returns true.
bool impliesPoison(const Value *ValAssumedPoison, const Value *V);

/// Return true if this function can prove that V does not have undef bits
/// and is never poison. If V is an aggregate value or vector, check whether
/// all elements (except padding) are not undef or poison.
/// Note that this is different from canCreateUndefOrPoison because the
/// function assumes Op's operands are not poison/undef.
///
/// If CtxI and DT are specified this method performs flow-sensitive analysis
/// and returns true if it is guaranteed to be never undef or poison
/// immediately before the CtxI.
bool isGuaranteedNotToBeUndefOrPoison(const Value *V,
                                      AssumptionCache *AC = nullptr,
                                      const Instruction *CtxI = nullptr,
                                      const DominatorTree *DT = nullptr,
                                      unsigned Depth = 0);

/// Returns true if V cannot be poison, but may be undef.
bool isGuaranteedNotToBePoison(const Value *V, AssumptionCache *AC = nullptr,
                               const Instruction *CtxI = nullptr,
                               const DominatorTree *DT = nullptr,
                               unsigned Depth = 0);

inline bool isGuaranteedNotToBePoison(const Value *V, AssumptionCache *AC,
                                      BasicBlock::iterator CtxI,
                                      const DominatorTree *DT = nullptr,
                                      unsigned Depth = 0) {
  // Takes an iterator as a position, passes down to Instruction *
  // implementation.
  return isGuaranteedNotToBePoison(V, AC, &*CtxI, DT, Depth);
}

/// Returns true if V cannot be undef, but may be poison.
bool isGuaranteedNotToBeUndef(const Value *V, AssumptionCache *AC = nullptr,
                              const Instruction *CtxI = nullptr,
                              const DominatorTree *DT = nullptr,
                              unsigned Depth = 0);

/// Return true if undefined behavior would provable be executed on the path to
/// OnPathTo if Root produced a posion result.  Note that this doesn't say
/// anything about whether OnPathTo is actually executed or whether Root is
/// actually poison.  This can be used to assess whether a new use of Root can
/// be added at a location which is control equivalent with OnPathTo (such as
/// immediately before it) without introducing UB which didn't previously
/// exist.  Note that a false result conveys no information.
bool mustExecuteUBIfPoisonOnPathTo(Instruction *Root,
                                   Instruction *OnPathTo,
                                   DominatorTree *DT);

/// Specific patterns of select instructions we can match.
enum SelectPatternFlavor {
  SPF_UNKNOWN = 0,
  SPF_SMIN,    /// Signed minimum
  SPF_UMIN,    /// Unsigned minimum
  SPF_SMAX,    /// Signed maximum
  SPF_UMAX,    /// Unsigned maximum
  SPF_FMINNUM, /// Floating point minnum
  SPF_FMAXNUM, /// Floating point maxnum
  SPF_ABS,     /// Absolute value
  SPF_NABS     /// Negated absolute value
};

/// Behavior when a floating point min/max is given one NaN and one
/// non-NaN as input.
enum SelectPatternNaNBehavior {
  SPNB_NA = 0,        /// NaN behavior not applicable.
  SPNB_RETURNS_NAN,   /// Given one NaN input, returns the NaN.
  SPNB_RETURNS_OTHER, /// Given one NaN input, returns the non-NaN.
  SPNB_RETURNS_ANY    /// Given one NaN input, can return either (or
                      /// it has been determined that no operands can
                      /// be NaN).
};

struct SelectPatternResult {
  SelectPatternFlavor Flavor;
  SelectPatternNaNBehavior NaNBehavior; /// Only applicable if Flavor is
                                        /// SPF_FMINNUM or SPF_FMAXNUM.
  bool Ordered; /// When implementing this min/max pattern as
                /// fcmp; select, does the fcmp have to be
                /// ordered?

  /// Return true if \p SPF is a min or a max pattern.
  static bool isMinOrMax(SelectPatternFlavor SPF) {
    return SPF != SPF_UNKNOWN && SPF != SPF_ABS && SPF != SPF_NABS;
  }
};

/// Pattern match integer [SU]MIN, [SU]MAX and ABS idioms, returning the kind
/// and providing the out parameter results if we successfully match.
///
/// For ABS/NABS, LHS will be set to the input to the abs idiom. RHS will be
/// the negation instruction from the idiom.
///
/// If CastOp is not nullptr, also match MIN/MAX idioms where the type does
/// not match that of the original select. If this is the case, the cast
/// operation (one of Trunc,SExt,Zext) that must be done to transform the
/// type of LHS and RHS into the type of V is returned in CastOp.
///
/// For example:
///   %1 = icmp slt i32 %a, i32 4
///   %2 = sext i32 %a to i64
///   %3 = select i1 %1, i64 %2, i64 4
///
/// -> LHS = %a, RHS = i32 4, *CastOp = Instruction::SExt
///
SelectPatternResult matchSelectPattern(Value *V, Value *&LHS, Value *&RHS,
                                       Instruction::CastOps *CastOp = nullptr,
                                       unsigned Depth = 0);

inline SelectPatternResult matchSelectPattern(const Value *V, const Value *&LHS,
                                              const Value *&RHS) {
  Value *L = const_cast<Value *>(LHS);
  Value *R = const_cast<Value *>(RHS);
  auto Result = matchSelectPattern(const_cast<Value *>(V), L, R);
  LHS = L;
  RHS = R;
  return Result;
}

/// Determine the pattern that a select with the given compare as its
/// predicate and given values as its true/false operands would match.
SelectPatternResult matchDecomposedSelectPattern(
    CmpInst *CmpI, Value *TrueVal, Value *FalseVal, Value *&LHS, Value *&RHS,
    Instruction::CastOps *CastOp = nullptr, unsigned Depth = 0);

/// Return the canonical comparison predicate for the specified
/// minimum/maximum flavor.
CmpInst::Predicate getMinMaxPred(SelectPatternFlavor SPF, bool Ordered = false);

/// Return the inverse minimum/maximum flavor of the specified flavor.
/// For example, signed minimum is the inverse of signed maximum.
SelectPatternFlavor getInverseMinMaxFlavor(SelectPatternFlavor SPF);

Intrinsic::ID getInverseMinMaxIntrinsic(Intrinsic::ID MinMaxID);

/// Return the minimum or maximum constant value for the specified integer
/// min/max flavor and type.
APInt getMinMaxLimit(SelectPatternFlavor SPF, unsigned BitWidth);

/// Check if the values in \p VL are select instructions that can be converted
/// to a min or max (vector) intrinsic. Returns the intrinsic ID, if such a
/// conversion is possible, together with a bool indicating whether all select
/// conditions are only used by the selects. Otherwise return
/// Intrinsic::not_intrinsic.
std::pair<Intrinsic::ID, bool>
canConvertToMinOrMaxIntrinsic(ArrayRef<Value *> VL);

/// Attempt to match a simple first order recurrence cycle of the form:
///   %iv = phi Ty [%Start, %Entry], [%Inc, %backedge]
///   %inc = binop %iv, %step
/// OR
///   %iv = phi Ty [%Start, %Entry], [%Inc, %backedge]
///   %inc = binop %step, %iv
///
/// A first order recurrence is a formula with the form: X_n = f(X_(n-1))
///
/// A couple of notes on subtleties in that definition:
/// * The Step does not have to be loop invariant.  In math terms, it can
///   be a free variable.  We allow recurrences with both constant and
///   variable coefficients. Callers may wish to filter cases where Step
///   does not dominate P.
/// * For non-commutative operators, we will match both forms.  This
///   results in some odd recurrence structures.  Callers may wish to filter
///   out recurrences where the phi is not the LHS of the returned operator.
/// * Because of the structure matched, the caller can assume as a post
///   condition of the match the presence of a Loop with P's parent as it's
///   header *except* in unreachable code.  (Dominance decays in unreachable
///   code.)
///
/// NOTE: This is intentional simple.  If you want the ability to analyze
/// non-trivial loop conditons, see ScalarEvolution instead.
bool matchSimpleRecurrence(const PHINode *P, BinaryOperator *&BO, Value *&Start,
                           Value *&Step);

/// Analogous to the above, but starting from the binary operator
bool matchSimpleRecurrence(const BinaryOperator *I, PHINode *&P, Value *&Start,
                           Value *&Step);

/// Return true if RHS is known to be implied true by LHS.  Return false if
/// RHS is known to be implied false by LHS.  Otherwise, return std::nullopt if
/// no implication can be made. A & B must be i1 (boolean) values or a vector of
/// such values. Note that the truth table for implication is the same as <=u on
/// i1 values (but not
/// <=s!).  The truth table for both is:
///    | T | F (B)
///  T | T | F
///  F | T | T
/// (A)
std::optional<bool> isImpliedCondition(const Value *LHS, const Value *RHS,
                                       const DataLayout &DL,
                                       bool LHSIsTrue = true,
                                       unsigned Depth = 0);
std::optional<bool> isImpliedCondition(const Value *LHS,
                                       CmpInst::Predicate RHSPred,
                                       const Value *RHSOp0, const Value *RHSOp1,
                                       const DataLayout &DL,
                                       bool LHSIsTrue = true,
                                       unsigned Depth = 0);

/// Return the boolean condition value in the context of the given instruction
/// if it is known based on dominating conditions.
std::optional<bool> isImpliedByDomCondition(const Value *Cond,
                                            const Instruction *ContextI,
                                            const DataLayout &DL);
std::optional<bool> isImpliedByDomCondition(CmpInst::Predicate Pred,
                                            const Value *LHS, const Value *RHS,
                                            const Instruction *ContextI,
                                            const DataLayout &DL);

/// Call \p InsertAffected on all Values whose known bits / value may be
/// affected by the condition \p Cond. Used by AssumptionCache and
/// DomConditionCache.
void findValuesAffectedByCondition(Value *Cond, bool IsAssume,
                                   function_ref<void(Value *)> InsertAffected);

} // end namespace llvm

#endif // LLVM_ANALYSIS_VALUETRACKING_H
