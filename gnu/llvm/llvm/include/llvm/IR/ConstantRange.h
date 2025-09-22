//===- ConstantRange.h - Represent a range ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Represent a range of possible values that may occur when the program is run
// for an integral value.  This keeps track of a lower and upper bound for the
// constant, which MAY wrap around the end of the numeric range.  To do this, it
// keeps track of a [lower, upper) bound, which specifies an interval just like
// STL iterators.  When used with boolean values, the following are important
// ranges: :
//
//  [F, F) = {}     = Empty set
//  [T, F) = {T}
//  [F, T) = {F}
//  [T, T) = {F, T} = Full set
//
// The other integral ranges use min/max values for special range values. For
// example, for 8-bit types, it uses:
// [0, 0)     = {}       = Empty set
// [255, 255) = {0..255} = Full Set
//
// Note that ConstantRange can be used to represent either signed or
// unsigned ranges.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_CONSTANTRANGE_H
#define LLVM_IR_CONSTANTRANGE_H

#include "llvm/ADT/APInt.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {

class MDNode;
class raw_ostream;
struct KnownBits;

/// This class represents a range of values.
class [[nodiscard]] ConstantRange {
  APInt Lower, Upper;

  /// Create empty constant range with same bitwidth.
  ConstantRange getEmpty() const {
    return ConstantRange(getBitWidth(), false);
  }

  /// Create full constant range with same bitwidth.
  ConstantRange getFull() const {
    return ConstantRange(getBitWidth(), true);
  }

public:
  /// Initialize a full or empty set for the specified bit width.
  explicit ConstantRange(uint32_t BitWidth, bool isFullSet);

  /// Initialize a range to hold the single specified value.
  ConstantRange(APInt Value);

  /// Initialize a range of values explicitly. This will assert out if
  /// Lower==Upper and Lower != Min or Max value for its type. It will also
  /// assert out if the two APInt's are not the same bit width.
  ConstantRange(APInt Lower, APInt Upper);

  /// Create empty constant range with the given bit width.
  static ConstantRange getEmpty(uint32_t BitWidth) {
    return ConstantRange(BitWidth, false);
  }

  /// Create full constant range with the given bit width.
  static ConstantRange getFull(uint32_t BitWidth) {
    return ConstantRange(BitWidth, true);
  }

  /// Create non-empty constant range with the given bounds. If Lower and
  /// Upper are the same, a full range is returned.
  static ConstantRange getNonEmpty(APInt Lower, APInt Upper) {
    if (Lower == Upper)
      return getFull(Lower.getBitWidth());
    return ConstantRange(std::move(Lower), std::move(Upper));
  }

  /// Initialize a range based on a known bits constraint. The IsSigned flag
  /// indicates whether the constant range should not wrap in the signed or
  /// unsigned domain.
  static ConstantRange fromKnownBits(const KnownBits &Known, bool IsSigned);

  /// Produce the smallest range such that all values that may satisfy the given
  /// predicate with any value contained within Other is contained in the
  /// returned range.  Formally, this returns a superset of
  /// 'union over all y in Other . { x : icmp op x y is true }'.  If the exact
  /// answer is not representable as a ConstantRange, the return value will be a
  /// proper superset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns Result = [0, 4)
  static ConstantRange makeAllowedICmpRegion(CmpInst::Predicate Pred,
                                             const ConstantRange &Other);

  /// Produce the largest range such that all values in the returned range
  /// satisfy the given predicate with all values contained within Other.
  /// Formally, this returns a subset of
  /// 'intersection over all y in Other . { x : icmp op x y is true }'.  If the
  /// exact answer is not representable as a ConstantRange, the return value
  /// will be a proper subset of the above.
  ///
  /// Example: Pred = ult and Other = i8 [2, 5) returns [0, 2)
  static ConstantRange makeSatisfyingICmpRegion(CmpInst::Predicate Pred,
                                                const ConstantRange &Other);

  /// Produce the exact range such that all values in the returned range satisfy
  /// the given predicate with any value contained within Other. Formally, this
  /// returns the exact answer when the superset of 'union over all y in Other
  /// is exactly same as the subset of intersection over all y in Other.
  /// { x : icmp op x y is true}'.
  ///
  /// Example: Pred = ult and Other = i8 3 returns [0, 3)
  static ConstantRange makeExactICmpRegion(CmpInst::Predicate Pred,
                                           const APInt &Other);

  /// Does the predicate \p Pred hold between ranges this and \p Other?
  /// NOTE: false does not mean that inverse predicate holds!
  bool icmp(CmpInst::Predicate Pred, const ConstantRange &Other) const;

  /// Return true iff CR1 ult CR2 is equivalent to CR1 slt CR2.
  /// Does not depend on strictness/direction of the predicate.
  static bool
  areInsensitiveToSignednessOfICmpPredicate(const ConstantRange &CR1,
                                            const ConstantRange &CR2);

  /// Return true iff CR1 ult CR2 is equivalent to CR1 sge CR2.
  /// Does not depend on strictness/direction of the predicate.
  static bool
  areInsensitiveToSignednessOfInvertedICmpPredicate(const ConstantRange &CR1,
                                                    const ConstantRange &CR2);

  /// If the comparison between constant ranges this and Other
  /// is insensitive to the signedness of the comparison predicate,
  /// return a predicate equivalent to \p Pred, with flipped signedness
  /// (i.e. unsigned instead of signed or vice versa), and maybe inverted,
  /// otherwise returns CmpInst::Predicate::BAD_ICMP_PREDICATE.
  static CmpInst::Predicate
  getEquivalentPredWithFlippedSignedness(CmpInst::Predicate Pred,
                                         const ConstantRange &CR1,
                                         const ConstantRange &CR2);

  /// Produce the largest range containing all X such that "X BinOp Y" is
  /// guaranteed not to wrap (overflow) for *all* Y in Other. However, there may
  /// be *some* Y in Other for which additional X not contained in the result
  /// also do not overflow.
  ///
  /// NoWrapKind must be one of OBO::NoUnsignedWrap or OBO::NoSignedWrap.
  ///
  /// Examples:
  ///  typedef OverflowingBinaryOperator OBO;
  ///  #define MGNR makeGuaranteedNoWrapRegion
  ///  MGNR(Add, [i8 1, 2), OBO::NoSignedWrap) == [-128, 127)
  ///  MGNR(Add, [i8 1, 2), OBO::NoUnsignedWrap) == [0, -1)
  ///  MGNR(Add, [i8 0, 1), OBO::NoUnsignedWrap) == Full Set
  ///  MGNR(Add, [i8 -1, 6), OBO::NoSignedWrap) == [INT_MIN+1, INT_MAX-4)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoSignedWrap) == [-127, 128)
  ///  MGNR(Sub, [i8 1, 2), OBO::NoUnsignedWrap) == [1, 0)
  static ConstantRange makeGuaranteedNoWrapRegion(Instruction::BinaryOps BinOp,
                                                  const ConstantRange &Other,
                                                  unsigned NoWrapKind);

  /// Produce the range that contains X if and only if "X BinOp Other" does
  /// not wrap.
  static ConstantRange makeExactNoWrapRegion(Instruction::BinaryOps BinOp,
                                             const APInt &Other,
                                             unsigned NoWrapKind);

  /// Initialize a range containing all values X that satisfy `(X & Mask)
  /// != C`. Note that the range returned may contain values where `(X & Mask)
  /// == C` holds, making it less precise, but still conservative.
  static ConstantRange makeMaskNotEqualRange(const APInt &Mask, const APInt &C);

  /// Returns true if ConstantRange calculations are supported for intrinsic
  /// with \p IntrinsicID.
  static bool isIntrinsicSupported(Intrinsic::ID IntrinsicID);

  /// Compute range of intrinsic result for the given operand ranges.
  static ConstantRange intrinsic(Intrinsic::ID IntrinsicID,
                                 ArrayRef<ConstantRange> Ops);

  /// Set up \p Pred and \p RHS such that
  /// ConstantRange::makeExactICmpRegion(Pred, RHS) == *this.  Return true if
  /// successful.
  bool getEquivalentICmp(CmpInst::Predicate &Pred, APInt &RHS) const;

  /// Set up \p Pred, \p RHS and \p Offset such that (V + Offset) Pred RHS
  /// is true iff V is in the range. Prefers using Offset == 0 if possible.
  void
  getEquivalentICmp(CmpInst::Predicate &Pred, APInt &RHS, APInt &Offset) const;

  /// Return the lower value for this range.
  const APInt &getLower() const { return Lower; }

  /// Return the upper value for this range.
  const APInt &getUpper() const { return Upper; }

  /// Get the bit width of this ConstantRange.
  uint32_t getBitWidth() const { return Lower.getBitWidth(); }

  /// Return true if this set contains all of the elements possible
  /// for this data-type.
  bool isFullSet() const;

  /// Return true if this set contains no members.
  bool isEmptySet() const;

  /// Return true if this set wraps around the unsigned domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, 0) == [X, Max]: Not wrapped.
  bool isWrappedSet() const;

  /// Return true if the exclusive upper bound wraps around the unsigned
  /// domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, 0): Wrapped.
  bool isUpperWrapped() const;

  /// Return true if this set wraps around the signed domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, SignedMin) == [X, SignedMax]: Not wrapped.
  bool isSignWrappedSet() const;

  /// Return true if the (exclusive) upper bound wraps around the signed
  /// domain. Special cases:
  ///  * Empty set: Not wrapped.
  ///  * Full set: Not wrapped.
  ///  * [X, SignedMin): Wrapped.
  bool isUpperSignWrapped() const;

  /// Return true if the specified value is in the set.
  bool contains(const APInt &Val) const;

  /// Return true if the other range is a subset of this one.
  bool contains(const ConstantRange &CR) const;

  /// If this set contains a single element, return it, otherwise return null.
  const APInt *getSingleElement() const {
    if (Upper == Lower + 1)
      return &Lower;
    return nullptr;
  }

  /// If this set contains all but a single element, return it, otherwise return
  /// null.
  const APInt *getSingleMissingElement() const {
    if (Lower == Upper + 1)
      return &Upper;
    return nullptr;
  }

  /// Return true if this set contains exactly one member.
  bool isSingleElement() const { return getSingleElement() != nullptr; }

  /// Compare set size of this range with the range CR.
  bool isSizeStrictlySmallerThan(const ConstantRange &CR) const;

  /// Compare set size of this range with Value.
  bool isSizeLargerThan(uint64_t MaxSize) const;

  /// Return true if all values in this range are negative.
  bool isAllNegative() const;

  /// Return true if all values in this range are non-negative.
  bool isAllNonNegative() const;

  /// Return true if all values in this range are positive.
  bool isAllPositive() const;

  /// Return the largest unsigned value contained in the ConstantRange.
  APInt getUnsignedMax() const;

  /// Return the smallest unsigned value contained in the ConstantRange.
  APInt getUnsignedMin() const;

  /// Return the largest signed value contained in the ConstantRange.
  APInt getSignedMax() const;

  /// Return the smallest signed value contained in the ConstantRange.
  APInt getSignedMin() const;

  /// Return true if this range is equal to another range.
  bool operator==(const ConstantRange &CR) const {
    return Lower == CR.Lower && Upper == CR.Upper;
  }
  bool operator!=(const ConstantRange &CR) const {
    return !operator==(CR);
  }

  /// Compute the maximal number of active bits needed to represent every value
  /// in this range.
  unsigned getActiveBits() const;

  /// Compute the maximal number of bits needed to represent every value
  /// in this signed range.
  unsigned getMinSignedBits() const;

  /// Subtract the specified constant from the endpoints of this constant range.
  ConstantRange subtract(const APInt &CI) const;

  /// Subtract the specified range from this range (aka relative complement of
  /// the sets).
  ConstantRange difference(const ConstantRange &CR) const;

  /// If represented precisely, the result of some range operations may consist
  /// of multiple disjoint ranges. As only a single range may be returned, any
  /// range covering these disjoint ranges constitutes a valid result, but some
  /// may be more useful than others depending on context. The preferred range
  /// type specifies whether a range that is non-wrapping in the unsigned or
  /// signed domain, or has the smallest size, is preferred. If a signedness is
  /// preferred but all ranges are non-wrapping or all wrapping, then the
  /// smallest set size is preferred. If there are multiple smallest sets, any
  /// one of them may be returned.
  enum PreferredRangeType { Smallest, Unsigned, Signed };

  /// Return the range that results from the intersection of this range with
  /// another range. If the intersection is disjoint, such that two results
  /// are possible, the preferred range is determined by the PreferredRangeType.
  ConstantRange intersectWith(const ConstantRange &CR,
                              PreferredRangeType Type = Smallest) const;

  /// Return the range that results from the union of this range
  /// with another range.  The resultant range is guaranteed to include the
  /// elements of both sets, but may contain more.  For example, [3, 9) union
  /// [12,15) is [3, 15), which includes 9, 10, and 11, which were not included
  /// in either set before.
  ConstantRange unionWith(const ConstantRange &CR,
                          PreferredRangeType Type = Smallest) const;

  /// Intersect the two ranges and return the result if it can be represented
  /// exactly, otherwise return std::nullopt.
  std::optional<ConstantRange>
  exactIntersectWith(const ConstantRange &CR) const;

  /// Union the two ranges and return the result if it can be represented
  /// exactly, otherwise return std::nullopt.
  std::optional<ConstantRange> exactUnionWith(const ConstantRange &CR) const;

  /// Return a new range representing the possible values resulting
  /// from an application of the specified cast operator to this range. \p
  /// BitWidth is the target bitwidth of the cast.  For casts which don't
  /// change bitwidth, it must be the same as the source bitwidth.  For casts
  /// which do change bitwidth, the bitwidth must be consistent with the
  /// requested cast and source bitwidth.
  ConstantRange castOp(Instruction::CastOps CastOp,
                       uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must
  /// be strictly larger than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// zero extended to BitWidth.
  ConstantRange zeroExtend(uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must
  /// be strictly larger than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// sign extended to BitWidth.
  ConstantRange signExtend(uint32_t BitWidth) const;

  /// Return a new range in the specified integer type, which must be
  /// strictly smaller than the current type.  The returned range will
  /// correspond to the possible range of values if the source range had been
  /// truncated to the specified type.
  ConstantRange truncate(uint32_t BitWidth) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is zero extended, truncated, or left alone to make it that width.
  ConstantRange zextOrTrunc(uint32_t BitWidth) const;

  /// Make this range have the bit width given by \p BitWidth. The
  /// value is sign extended, truncated, or left alone to make it that width.
  ConstantRange sextOrTrunc(uint32_t BitWidth) const;

  /// Return a new range representing the possible values resulting
  /// from an application of the specified binary operator to an left hand side
  /// of this range and a right hand side of \p Other.
  ConstantRange binaryOp(Instruction::BinaryOps BinOp,
                         const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an application of the specified overflowing binary operator to a
  /// left hand side of this range and a right hand side of \p Other given
  /// the provided knowledge about lack of wrapping \p NoWrapKind.
  ConstantRange overflowingBinaryOp(Instruction::BinaryOps BinOp,
                                    const ConstantRange &Other,
                                    unsigned NoWrapKind) const;

  /// Return a new range representing the possible values resulting
  /// from an addition of a value in this range and a value in \p Other.
  ConstantRange add(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an addition with wrap type \p NoWrapKind of a value in this
  /// range and a value in \p Other.
  /// If the result range is disjoint, the preferred range is determined by the
  /// \p PreferredRangeType.
  ConstantRange addWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                              PreferredRangeType RangeType = Smallest) const;

  /// Return a new range representing the possible values resulting
  /// from a subtraction of a value in this range and a value in \p Other.
  ConstantRange sub(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an subtraction with wrap type \p NoWrapKind of a value in this
  /// range and a value in \p Other.
  /// If the result range is disjoint, the preferred range is determined by the
  /// \p PreferredRangeType.
  ConstantRange subWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                              PreferredRangeType RangeType = Smallest) const;

  /// Return a new range representing the possible values resulting
  /// from a multiplication of a value in this range and a value in \p Other,
  /// treating both this and \p Other as unsigned ranges.
  ConstantRange multiply(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a multiplication with wrap type \p NoWrapKind of a value in this
  /// range and a value in \p Other.
  /// If the result range is disjoint, the preferred range is determined by the
  /// \p PreferredRangeType.
  ConstantRange
  multiplyWithNoWrap(const ConstantRange &Other, unsigned NoWrapKind,
                     PreferredRangeType RangeType = Smallest) const;

  /// Return range of possible values for a signed multiplication of this and
  /// \p Other. However, if overflow is possible always return a full range
  /// rather than trying to determine a more precise result.
  ConstantRange smul_fast(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed maximum of a value in this range and a value in \p Other.
  ConstantRange smax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned maximum of a value in this range and a value in \p Other.
  ConstantRange umax(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed minimum of a value in this range and a value in \p Other.
  ConstantRange smin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned minimum of a value in this range and a value in \p Other.
  ConstantRange umin(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned division of a value in this range and a value in
  /// \p Other.
  ConstantRange udiv(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed division of a value in this range and a value in
  /// \p Other. Division by zero and division of SignedMin by -1 are considered
  /// undefined behavior, in line with IR, and do not contribute towards the
  /// result.
  ConstantRange sdiv(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from an unsigned remainder operation of a value in this range and a
  /// value in \p Other.
  ConstantRange urem(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a signed remainder operation of a value in this range and a
  /// value in \p Other.
  ConstantRange srem(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from
  /// a binary-xor of a value in this range by an all-one value,
  /// aka bitwise complement operation.
  ConstantRange binaryNot() const;

  /// Return a new range representing the possible values resulting
  /// from a binary-and of a value in this range by a value in \p Other.
  ConstantRange binaryAnd(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-or of a value in this range by a value in \p Other.
  ConstantRange binaryOr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a binary-xor of a value in this range by a value in \p Other.
  ConstantRange binaryXor(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting
  /// from a left shift of a value in this range by a value in \p Other.
  /// TODO: This isn't fully implemented yet.
  ConstantRange shl(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// logical right shift of a value in this range and a value in \p Other.
  ConstantRange lshr(const ConstantRange &Other) const;

  /// Return a new range representing the possible values resulting from a
  /// arithmetic right shift of a value in this range and a value in \p Other.
  ConstantRange ashr(const ConstantRange &Other) const;

  /// Perform an unsigned saturating addition of two constant ranges.
  ConstantRange uadd_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating addition of two constant ranges.
  ConstantRange sadd_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating subtraction of two constant ranges.
  ConstantRange usub_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating subtraction of two constant ranges.
  ConstantRange ssub_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating multiplication of two constant ranges.
  ConstantRange umul_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating multiplication of two constant ranges.
  ConstantRange smul_sat(const ConstantRange &Other) const;

  /// Perform an unsigned saturating left shift of this constant range by a
  /// value in \p Other.
  ConstantRange ushl_sat(const ConstantRange &Other) const;

  /// Perform a signed saturating left shift of this constant range by a
  /// value in \p Other.
  ConstantRange sshl_sat(const ConstantRange &Other) const;

  /// Return a new range that is the logical not of the current set.
  ConstantRange inverse() const;

  /// Calculate absolute value range. If the original range contains signed
  /// min, then the resulting range will contain signed min if and only if
  /// \p IntMinIsPoison is false.
  ConstantRange abs(bool IntMinIsPoison = false) const;

  /// Calculate ctlz range. If \p ZeroIsPoison is set, the range is computed
  /// ignoring a possible zero value contained in the input range.
  ConstantRange ctlz(bool ZeroIsPoison = false) const;

  /// Calculate cttz range. If \p ZeroIsPoison is set, the range is computed
  /// ignoring a possible zero value contained in the input range.
  ConstantRange cttz(bool ZeroIsPoison = false) const;

  /// Calculate ctpop range.
  ConstantRange ctpop() const;

  /// Represents whether an operation on the given constant range is known to
  /// always or never overflow.
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

  /// Return whether unsigned add of the two ranges always/never overflows.
  OverflowResult unsignedAddMayOverflow(const ConstantRange &Other) const;

  /// Return whether signed add of the two ranges always/never overflows.
  OverflowResult signedAddMayOverflow(const ConstantRange &Other) const;

  /// Return whether unsigned sub of the two ranges always/never overflows.
  OverflowResult unsignedSubMayOverflow(const ConstantRange &Other) const;

  /// Return whether signed sub of the two ranges always/never overflows.
  OverflowResult signedSubMayOverflow(const ConstantRange &Other) const;

  /// Return whether unsigned mul of the two ranges always/never overflows.
  OverflowResult unsignedMulMayOverflow(const ConstantRange &Other) const;

  /// Return known bits for values in this range.
  KnownBits toKnownBits() const;

  /// Print out the bounds to a stream.
  void print(raw_ostream &OS) const;

  /// Allow printing from a debugger easily.
  void dump() const;
};

inline raw_ostream &operator<<(raw_ostream &OS, const ConstantRange &CR) {
  CR.print(OS);
  return OS;
}

/// Parse out a conservative ConstantRange from !range metadata.
///
/// E.g. if RangeMD is !{i32 0, i32 10, i32 15, i32 20} then return [0, 20).
ConstantRange getConstantRangeFromMetadata(const MDNode &RangeMD);

} // end namespace llvm

#endif // LLVM_IR_CONSTANTRANGE_H
