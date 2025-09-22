//===- PatternMatch.h - Match on the LLVM IR --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides a simple and efficient mechanism for performing general
// tree-based pattern matches on the LLVM IR. The power of these routines is
// that it allows you to write concise patterns that are expressive and easy to
// understand. The other major advantage of this is that it allows you to
// trivially capture/bind elements in the pattern to variables. For example,
// you can do something like this:
//
//  Value *Exp = ...
//  Value *X, *Y;  ConstantInt *C1, *C2;      // (X & C1) | (Y & C2)
//  if (match(Exp, m_Or(m_And(m_Value(X), m_ConstantInt(C1)),
//                      m_And(m_Value(Y), m_ConstantInt(C2))))) {
//    ... Pattern is matched and variables are bound ...
//  }
//
// This is primarily useful to things like the instruction combiner, but can
// also be useful for static analysis tools or code generators.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PATTERNMATCH_H
#define LLVM_IR_PATTERNMATCH_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cstdint>

namespace llvm {
namespace PatternMatch {

template <typename Val, typename Pattern> bool match(Val *V, const Pattern &P) {
  return const_cast<Pattern &>(P).match(V);
}

template <typename Pattern> bool match(ArrayRef<int> Mask, const Pattern &P) {
  return const_cast<Pattern &>(P).match(Mask);
}

template <typename SubPattern_t> struct OneUse_match {
  SubPattern_t SubPattern;

  OneUse_match(const SubPattern_t &SP) : SubPattern(SP) {}

  template <typename OpTy> bool match(OpTy *V) {
    return V->hasOneUse() && SubPattern.match(V);
  }
};

template <typename T> inline OneUse_match<T> m_OneUse(const T &SubPattern) {
  return SubPattern;
}

template <typename SubPattern_t> struct AllowReassoc_match {
  SubPattern_t SubPattern;

  AllowReassoc_match(const SubPattern_t &SP) : SubPattern(SP) {}

  template <typename OpTy> bool match(OpTy *V) {
    auto *I = dyn_cast<FPMathOperator>(V);
    return I && I->hasAllowReassoc() && SubPattern.match(I);
  }
};

template <typename T>
inline AllowReassoc_match<T> m_AllowReassoc(const T &SubPattern) {
  return SubPattern;
}

template <typename Class> struct class_match {
  template <typename ITy> bool match(ITy *V) { return isa<Class>(V); }
};

/// Match an arbitrary value and ignore it.
inline class_match<Value> m_Value() { return class_match<Value>(); }

/// Match an arbitrary unary operation and ignore it.
inline class_match<UnaryOperator> m_UnOp() {
  return class_match<UnaryOperator>();
}

/// Match an arbitrary binary operation and ignore it.
inline class_match<BinaryOperator> m_BinOp() {
  return class_match<BinaryOperator>();
}

/// Matches any compare instruction and ignore it.
inline class_match<CmpInst> m_Cmp() { return class_match<CmpInst>(); }

struct undef_match {
  static bool check(const Value *V) {
    if (isa<UndefValue>(V))
      return true;

    const auto *CA = dyn_cast<ConstantAggregate>(V);
    if (!CA)
      return false;

    SmallPtrSet<const ConstantAggregate *, 8> Seen;
    SmallVector<const ConstantAggregate *, 8> Worklist;

    // Either UndefValue, PoisonValue, or an aggregate that only contains
    // these is accepted by matcher.
    // CheckValue returns false if CA cannot satisfy this constraint.
    auto CheckValue = [&](const ConstantAggregate *CA) {
      for (const Value *Op : CA->operand_values()) {
        if (isa<UndefValue>(Op))
          continue;

        const auto *CA = dyn_cast<ConstantAggregate>(Op);
        if (!CA)
          return false;
        if (Seen.insert(CA).second)
          Worklist.emplace_back(CA);
      }

      return true;
    };

    if (!CheckValue(CA))
      return false;

    while (!Worklist.empty()) {
      if (!CheckValue(Worklist.pop_back_val()))
        return false;
    }
    return true;
  }
  template <typename ITy> bool match(ITy *V) { return check(V); }
};

/// Match an arbitrary undef constant. This matches poison as well.
/// If this is an aggregate and contains a non-aggregate element that is
/// neither undef nor poison, the aggregate is not matched.
inline auto m_Undef() { return undef_match(); }

/// Match an arbitrary UndefValue constant.
inline class_match<UndefValue> m_UndefValue() {
  return class_match<UndefValue>();
}

/// Match an arbitrary poison constant.
inline class_match<PoisonValue> m_Poison() {
  return class_match<PoisonValue>();
}

/// Match an arbitrary Constant and ignore it.
inline class_match<Constant> m_Constant() { return class_match<Constant>(); }

/// Match an arbitrary ConstantInt and ignore it.
inline class_match<ConstantInt> m_ConstantInt() {
  return class_match<ConstantInt>();
}

/// Match an arbitrary ConstantFP and ignore it.
inline class_match<ConstantFP> m_ConstantFP() {
  return class_match<ConstantFP>();
}

struct constantexpr_match {
  template <typename ITy> bool match(ITy *V) {
    auto *C = dyn_cast<Constant>(V);
    return C && (isa<ConstantExpr>(C) || C->containsConstantExpression());
  }
};

/// Match a constant expression or a constant that contains a constant
/// expression.
inline constantexpr_match m_ConstantExpr() { return constantexpr_match(); }

/// Match an arbitrary basic block value and ignore it.
inline class_match<BasicBlock> m_BasicBlock() {
  return class_match<BasicBlock>();
}

/// Inverting matcher
template <typename Ty> struct match_unless {
  Ty M;

  match_unless(const Ty &Matcher) : M(Matcher) {}

  template <typename ITy> bool match(ITy *V) { return !M.match(V); }
};

/// Match if the inner matcher does *NOT* match.
template <typename Ty> inline match_unless<Ty> m_Unless(const Ty &M) {
  return match_unless<Ty>(M);
}

/// Matching combinators
template <typename LTy, typename RTy> struct match_combine_or {
  LTy L;
  RTy R;

  match_combine_or(const LTy &Left, const RTy &Right) : L(Left), R(Right) {}

  template <typename ITy> bool match(ITy *V) {
    if (L.match(V))
      return true;
    if (R.match(V))
      return true;
    return false;
  }
};

template <typename LTy, typename RTy> struct match_combine_and {
  LTy L;
  RTy R;

  match_combine_and(const LTy &Left, const RTy &Right) : L(Left), R(Right) {}

  template <typename ITy> bool match(ITy *V) {
    if (L.match(V))
      if (R.match(V))
        return true;
    return false;
  }
};

/// Combine two pattern matchers matching L || R
template <typename LTy, typename RTy>
inline match_combine_or<LTy, RTy> m_CombineOr(const LTy &L, const RTy &R) {
  return match_combine_or<LTy, RTy>(L, R);
}

/// Combine two pattern matchers matching L && R
template <typename LTy, typename RTy>
inline match_combine_and<LTy, RTy> m_CombineAnd(const LTy &L, const RTy &R) {
  return match_combine_and<LTy, RTy>(L, R);
}

struct apint_match {
  const APInt *&Res;
  bool AllowPoison;

  apint_match(const APInt *&Res, bool AllowPoison)
      : Res(Res), AllowPoison(AllowPoison) {}

  template <typename ITy> bool match(ITy *V) {
    if (auto *CI = dyn_cast<ConstantInt>(V)) {
      Res = &CI->getValue();
      return true;
    }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI =
                dyn_cast_or_null<ConstantInt>(C->getSplatValue(AllowPoison))) {
          Res = &CI->getValue();
          return true;
        }
    return false;
  }
};
// Either constexpr if or renaming ConstantFP::getValueAPF to
// ConstantFP::getValue is needed to do it via single template
// function for both apint/apfloat.
struct apfloat_match {
  const APFloat *&Res;
  bool AllowPoison;

  apfloat_match(const APFloat *&Res, bool AllowPoison)
      : Res(Res), AllowPoison(AllowPoison) {}

  template <typename ITy> bool match(ITy *V) {
    if (auto *CI = dyn_cast<ConstantFP>(V)) {
      Res = &CI->getValueAPF();
      return true;
    }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI =
                dyn_cast_or_null<ConstantFP>(C->getSplatValue(AllowPoison))) {
          Res = &CI->getValueAPF();
          return true;
        }
    return false;
  }
};

/// Match a ConstantInt or splatted ConstantVector, binding the
/// specified pointer to the contained APInt.
inline apint_match m_APInt(const APInt *&Res) {
  // Forbid poison by default to maintain previous behavior.
  return apint_match(Res, /* AllowPoison */ false);
}

/// Match APInt while allowing poison in splat vector constants.
inline apint_match m_APIntAllowPoison(const APInt *&Res) {
  return apint_match(Res, /* AllowPoison */ true);
}

/// Match APInt while forbidding poison in splat vector constants.
inline apint_match m_APIntForbidPoison(const APInt *&Res) {
  return apint_match(Res, /* AllowPoison */ false);
}

/// Match a ConstantFP or splatted ConstantVector, binding the
/// specified pointer to the contained APFloat.
inline apfloat_match m_APFloat(const APFloat *&Res) {
  // Forbid undefs by default to maintain previous behavior.
  return apfloat_match(Res, /* AllowPoison */ false);
}

/// Match APFloat while allowing poison in splat vector constants.
inline apfloat_match m_APFloatAllowPoison(const APFloat *&Res) {
  return apfloat_match(Res, /* AllowPoison */ true);
}

/// Match APFloat while forbidding poison in splat vector constants.
inline apfloat_match m_APFloatForbidPoison(const APFloat *&Res) {
  return apfloat_match(Res, /* AllowPoison */ false);
}

template <int64_t Val> struct constantint_match {
  template <typename ITy> bool match(ITy *V) {
    if (const auto *CI = dyn_cast<ConstantInt>(V)) {
      const APInt &CIV = CI->getValue();
      if (Val >= 0)
        return CIV == static_cast<uint64_t>(Val);
      // If Val is negative, and CI is shorter than it, truncate to the right
      // number of bits.  If it is larger, then we have to sign extend.  Just
      // compare their negated values.
      return -CIV == -Val;
    }
    return false;
  }
};

/// Match a ConstantInt with a specific value.
template <int64_t Val> inline constantint_match<Val> m_ConstantInt() {
  return constantint_match<Val>();
}

/// This helper class is used to match constant scalars, vector splats,
/// and fixed width vectors that satisfy a specified predicate.
/// For fixed width vector constants, poison elements are ignored if AllowPoison
/// is true.
template <typename Predicate, typename ConstantVal, bool AllowPoison>
struct cstval_pred_ty : public Predicate {
  const Constant **Res = nullptr;
  template <typename ITy> bool match_impl(ITy *V) {
    if (const auto *CV = dyn_cast<ConstantVal>(V))
      return this->isValue(CV->getValue());
    if (const auto *VTy = dyn_cast<VectorType>(V->getType())) {
      if (const auto *C = dyn_cast<Constant>(V)) {
        if (const auto *CV = dyn_cast_or_null<ConstantVal>(C->getSplatValue()))
          return this->isValue(CV->getValue());

        // Number of elements of a scalable vector unknown at compile time
        auto *FVTy = dyn_cast<FixedVectorType>(VTy);
        if (!FVTy)
          return false;

        // Non-splat vector constant: check each element for a match.
        unsigned NumElts = FVTy->getNumElements();
        assert(NumElts != 0 && "Constant vector with no elements?");
        bool HasNonPoisonElements = false;
        for (unsigned i = 0; i != NumElts; ++i) {
          Constant *Elt = C->getAggregateElement(i);
          if (!Elt)
            return false;
          if (AllowPoison && isa<PoisonValue>(Elt))
            continue;
          auto *CV = dyn_cast<ConstantVal>(Elt);
          if (!CV || !this->isValue(CV->getValue()))
            return false;
          HasNonPoisonElements = true;
        }
        return HasNonPoisonElements;
      }
    }
    return false;
  }

  template <typename ITy> bool match(ITy *V) {
    if (this->match_impl(V)) {
      if (Res)
        *Res = cast<Constant>(V);
      return true;
    }
    return false;
  }
};

/// specialization of cstval_pred_ty for ConstantInt
template <typename Predicate, bool AllowPoison = true>
using cst_pred_ty = cstval_pred_ty<Predicate, ConstantInt, AllowPoison>;

/// specialization of cstval_pred_ty for ConstantFP
template <typename Predicate>
using cstfp_pred_ty = cstval_pred_ty<Predicate, ConstantFP,
                                     /*AllowPoison=*/true>;

/// This helper class is used to match scalar and vector constants that
/// satisfy a specified predicate, and bind them to an APInt.
template <typename Predicate> struct api_pred_ty : public Predicate {
  const APInt *&Res;

  api_pred_ty(const APInt *&R) : Res(R) {}

  template <typename ITy> bool match(ITy *V) {
    if (const auto *CI = dyn_cast<ConstantInt>(V))
      if (this->isValue(CI->getValue())) {
        Res = &CI->getValue();
        return true;
      }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI = dyn_cast_or_null<ConstantInt>(
                C->getSplatValue(/*AllowPoison=*/true)))
          if (this->isValue(CI->getValue())) {
            Res = &CI->getValue();
            return true;
          }

    return false;
  }
};

/// This helper class is used to match scalar and vector constants that
/// satisfy a specified predicate, and bind them to an APFloat.
/// Poison is allowed in splat vector constants.
template <typename Predicate> struct apf_pred_ty : public Predicate {
  const APFloat *&Res;

  apf_pred_ty(const APFloat *&R) : Res(R) {}

  template <typename ITy> bool match(ITy *V) {
    if (const auto *CI = dyn_cast<ConstantFP>(V))
      if (this->isValue(CI->getValue())) {
        Res = &CI->getValue();
        return true;
      }
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CI = dyn_cast_or_null<ConstantFP>(
                C->getSplatValue(/* AllowPoison */ true)))
          if (this->isValue(CI->getValue())) {
            Res = &CI->getValue();
            return true;
          }

    return false;
  }
};

///////////////////////////////////////////////////////////////////////////////
//
// Encapsulate constant value queries for use in templated predicate matchers.
// This allows checking if constants match using compound predicates and works
// with vector constants, possibly with relaxed constraints. For example, ignore
// undef values.
//
///////////////////////////////////////////////////////////////////////////////

template <typename APTy> struct custom_checkfn {
  function_ref<bool(const APTy &)> CheckFn;
  bool isValue(const APTy &C) { return CheckFn(C); }
};

/// Match an integer or vector where CheckFn(ele) for each element is true.
/// For vectors, poison elements are assumed to match.
inline cst_pred_ty<custom_checkfn<APInt>>
m_CheckedInt(function_ref<bool(const APInt &)> CheckFn) {
  return cst_pred_ty<custom_checkfn<APInt>>{{CheckFn}};
}

inline cst_pred_ty<custom_checkfn<APInt>>
m_CheckedInt(const Constant *&V, function_ref<bool(const APInt &)> CheckFn) {
  return cst_pred_ty<custom_checkfn<APInt>>{{CheckFn}, &V};
}

/// Match a float or vector where CheckFn(ele) for each element is true.
/// For vectors, poison elements are assumed to match.
inline cstfp_pred_ty<custom_checkfn<APFloat>>
m_CheckedFp(function_ref<bool(const APFloat &)> CheckFn) {
  return cstfp_pred_ty<custom_checkfn<APFloat>>{{CheckFn}};
}

inline cstfp_pred_ty<custom_checkfn<APFloat>>
m_CheckedFp(const Constant *&V, function_ref<bool(const APFloat &)> CheckFn) {
  return cstfp_pred_ty<custom_checkfn<APFloat>>{{CheckFn}, &V};
}

struct is_any_apint {
  bool isValue(const APInt &C) { return true; }
};
/// Match an integer or vector with any integral constant.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_any_apint> m_AnyIntegralConstant() {
  return cst_pred_ty<is_any_apint>();
}

struct is_shifted_mask {
  bool isValue(const APInt &C) { return C.isShiftedMask(); }
};

inline cst_pred_ty<is_shifted_mask> m_ShiftedMask() {
  return cst_pred_ty<is_shifted_mask>();
}

struct is_all_ones {
  bool isValue(const APInt &C) { return C.isAllOnes(); }
};
/// Match an integer or vector with all bits set.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_all_ones> m_AllOnes() {
  return cst_pred_ty<is_all_ones>();
}

inline cst_pred_ty<is_all_ones, false> m_AllOnesForbidPoison() {
  return cst_pred_ty<is_all_ones, false>();
}

struct is_maxsignedvalue {
  bool isValue(const APInt &C) { return C.isMaxSignedValue(); }
};
/// Match an integer or vector with values having all bits except for the high
/// bit set (0x7f...).
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_maxsignedvalue> m_MaxSignedValue() {
  return cst_pred_ty<is_maxsignedvalue>();
}
inline api_pred_ty<is_maxsignedvalue> m_MaxSignedValue(const APInt *&V) {
  return V;
}

struct is_negative {
  bool isValue(const APInt &C) { return C.isNegative(); }
};
/// Match an integer or vector of negative values.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negative> m_Negative() {
  return cst_pred_ty<is_negative>();
}
inline api_pred_ty<is_negative> m_Negative(const APInt *&V) { return V; }

struct is_nonnegative {
  bool isValue(const APInt &C) { return C.isNonNegative(); }
};
/// Match an integer or vector of non-negative values.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_nonnegative> m_NonNegative() {
  return cst_pred_ty<is_nonnegative>();
}
inline api_pred_ty<is_nonnegative> m_NonNegative(const APInt *&V) { return V; }

struct is_strictlypositive {
  bool isValue(const APInt &C) { return C.isStrictlyPositive(); }
};
/// Match an integer or vector of strictly positive values.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_strictlypositive> m_StrictlyPositive() {
  return cst_pred_ty<is_strictlypositive>();
}
inline api_pred_ty<is_strictlypositive> m_StrictlyPositive(const APInt *&V) {
  return V;
}

struct is_nonpositive {
  bool isValue(const APInt &C) { return C.isNonPositive(); }
};
/// Match an integer or vector of non-positive values.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_nonpositive> m_NonPositive() {
  return cst_pred_ty<is_nonpositive>();
}
inline api_pred_ty<is_nonpositive> m_NonPositive(const APInt *&V) { return V; }

struct is_one {
  bool isValue(const APInt &C) { return C.isOne(); }
};
/// Match an integer 1 or a vector with all elements equal to 1.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_one> m_One() { return cst_pred_ty<is_one>(); }

struct is_zero_int {
  bool isValue(const APInt &C) { return C.isZero(); }
};
/// Match an integer 0 or a vector with all elements equal to 0.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_zero_int> m_ZeroInt() {
  return cst_pred_ty<is_zero_int>();
}

struct is_zero {
  template <typename ITy> bool match(ITy *V) {
    auto *C = dyn_cast<Constant>(V);
    // FIXME: this should be able to do something for scalable vectors
    return C && (C->isNullValue() || cst_pred_ty<is_zero_int>().match(C));
  }
};
/// Match any null constant or a vector with all elements equal to 0.
/// For vectors, this includes constants with undefined elements.
inline is_zero m_Zero() { return is_zero(); }

struct is_power2 {
  bool isValue(const APInt &C) { return C.isPowerOf2(); }
};
/// Match an integer or vector power-of-2.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_power2> m_Power2() { return cst_pred_ty<is_power2>(); }
inline api_pred_ty<is_power2> m_Power2(const APInt *&V) { return V; }

struct is_negated_power2 {
  bool isValue(const APInt &C) { return C.isNegatedPowerOf2(); }
};
/// Match a integer or vector negated power-of-2.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negated_power2> m_NegatedPower2() {
  return cst_pred_ty<is_negated_power2>();
}
inline api_pred_ty<is_negated_power2> m_NegatedPower2(const APInt *&V) {
  return V;
}

struct is_negated_power2_or_zero {
  bool isValue(const APInt &C) { return !C || C.isNegatedPowerOf2(); }
};
/// Match a integer or vector negated power-of-2.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_negated_power2_or_zero> m_NegatedPower2OrZero() {
  return cst_pred_ty<is_negated_power2_or_zero>();
}
inline api_pred_ty<is_negated_power2_or_zero>
m_NegatedPower2OrZero(const APInt *&V) {
  return V;
}

struct is_power2_or_zero {
  bool isValue(const APInt &C) { return !C || C.isPowerOf2(); }
};
/// Match an integer or vector of 0 or power-of-2 values.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_power2_or_zero> m_Power2OrZero() {
  return cst_pred_ty<is_power2_or_zero>();
}
inline api_pred_ty<is_power2_or_zero> m_Power2OrZero(const APInt *&V) {
  return V;
}

struct is_sign_mask {
  bool isValue(const APInt &C) { return C.isSignMask(); }
};
/// Match an integer or vector with only the sign bit(s) set.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_sign_mask> m_SignMask() {
  return cst_pred_ty<is_sign_mask>();
}

struct is_lowbit_mask {
  bool isValue(const APInt &C) { return C.isMask(); }
};
/// Match an integer or vector with only the low bit(s) set.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_lowbit_mask> m_LowBitMask() {
  return cst_pred_ty<is_lowbit_mask>();
}
inline api_pred_ty<is_lowbit_mask> m_LowBitMask(const APInt *&V) { return V; }

struct is_lowbit_mask_or_zero {
  bool isValue(const APInt &C) { return !C || C.isMask(); }
};
/// Match an integer or vector with only the low bit(s) set.
/// For vectors, this includes constants with undefined elements.
inline cst_pred_ty<is_lowbit_mask_or_zero> m_LowBitMaskOrZero() {
  return cst_pred_ty<is_lowbit_mask_or_zero>();
}
inline api_pred_ty<is_lowbit_mask_or_zero> m_LowBitMaskOrZero(const APInt *&V) {
  return V;
}

struct icmp_pred_with_threshold {
  ICmpInst::Predicate Pred;
  const APInt *Thr;
  bool isValue(const APInt &C) { return ICmpInst::compare(C, *Thr, Pred); }
};
/// Match an integer or vector with every element comparing 'pred' (eg/ne/...)
/// to Threshold. For vectors, this includes constants with undefined elements.
inline cst_pred_ty<icmp_pred_with_threshold>
m_SpecificInt_ICMP(ICmpInst::Predicate Predicate, const APInt &Threshold) {
  cst_pred_ty<icmp_pred_with_threshold> P;
  P.Pred = Predicate;
  P.Thr = &Threshold;
  return P;
}

struct is_nan {
  bool isValue(const APFloat &C) { return C.isNaN(); }
};
/// Match an arbitrary NaN constant. This includes quiet and signalling nans.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_nan> m_NaN() { return cstfp_pred_ty<is_nan>(); }

struct is_nonnan {
  bool isValue(const APFloat &C) { return !C.isNaN(); }
};
/// Match a non-NaN FP constant.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_nonnan> m_NonNaN() {
  return cstfp_pred_ty<is_nonnan>();
}

struct is_inf {
  bool isValue(const APFloat &C) { return C.isInfinity(); }
};
/// Match a positive or negative infinity FP constant.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_inf> m_Inf() { return cstfp_pred_ty<is_inf>(); }

struct is_noninf {
  bool isValue(const APFloat &C) { return !C.isInfinity(); }
};
/// Match a non-infinity FP constant, i.e. finite or NaN.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_noninf> m_NonInf() {
  return cstfp_pred_ty<is_noninf>();
}

struct is_finite {
  bool isValue(const APFloat &C) { return C.isFinite(); }
};
/// Match a finite FP constant, i.e. not infinity or NaN.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_finite> m_Finite() {
  return cstfp_pred_ty<is_finite>();
}
inline apf_pred_ty<is_finite> m_Finite(const APFloat *&V) { return V; }

struct is_finitenonzero {
  bool isValue(const APFloat &C) { return C.isFiniteNonZero(); }
};
/// Match a finite non-zero FP constant.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_finitenonzero> m_FiniteNonZero() {
  return cstfp_pred_ty<is_finitenonzero>();
}
inline apf_pred_ty<is_finitenonzero> m_FiniteNonZero(const APFloat *&V) {
  return V;
}

struct is_any_zero_fp {
  bool isValue(const APFloat &C) { return C.isZero(); }
};
/// Match a floating-point negative zero or positive zero.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_any_zero_fp> m_AnyZeroFP() {
  return cstfp_pred_ty<is_any_zero_fp>();
}

struct is_pos_zero_fp {
  bool isValue(const APFloat &C) { return C.isPosZero(); }
};
/// Match a floating-point positive zero.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_pos_zero_fp> m_PosZeroFP() {
  return cstfp_pred_ty<is_pos_zero_fp>();
}

struct is_neg_zero_fp {
  bool isValue(const APFloat &C) { return C.isNegZero(); }
};
/// Match a floating-point negative zero.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_neg_zero_fp> m_NegZeroFP() {
  return cstfp_pred_ty<is_neg_zero_fp>();
}

struct is_non_zero_fp {
  bool isValue(const APFloat &C) { return C.isNonZero(); }
};
/// Match a floating-point non-zero.
/// For vectors, this includes constants with undefined elements.
inline cstfp_pred_ty<is_non_zero_fp> m_NonZeroFP() {
  return cstfp_pred_ty<is_non_zero_fp>();
}

///////////////////////////////////////////////////////////////////////////////

template <typename Class> struct bind_ty {
  Class *&VR;

  bind_ty(Class *&V) : VR(V) {}

  template <typename ITy> bool match(ITy *V) {
    if (auto *CV = dyn_cast<Class>(V)) {
      VR = CV;
      return true;
    }
    return false;
  }
};

/// Match a value, capturing it if we match.
inline bind_ty<Value> m_Value(Value *&V) { return V; }
inline bind_ty<const Value> m_Value(const Value *&V) { return V; }

/// Match an instruction, capturing it if we match.
inline bind_ty<Instruction> m_Instruction(Instruction *&I) { return I; }
/// Match a unary operator, capturing it if we match.
inline bind_ty<UnaryOperator> m_UnOp(UnaryOperator *&I) { return I; }
/// Match a binary operator, capturing it if we match.
inline bind_ty<BinaryOperator> m_BinOp(BinaryOperator *&I) { return I; }
/// Match a with overflow intrinsic, capturing it if we match.
inline bind_ty<WithOverflowInst> m_WithOverflowInst(WithOverflowInst *&I) {
  return I;
}
inline bind_ty<const WithOverflowInst>
m_WithOverflowInst(const WithOverflowInst *&I) {
  return I;
}

/// Match an UndefValue, capturing the value if we match.
inline bind_ty<UndefValue> m_UndefValue(UndefValue *&U) { return U; }

/// Match a Constant, capturing the value if we match.
inline bind_ty<Constant> m_Constant(Constant *&C) { return C; }

/// Match a ConstantInt, capturing the value if we match.
inline bind_ty<ConstantInt> m_ConstantInt(ConstantInt *&CI) { return CI; }

/// Match a ConstantFP, capturing the value if we match.
inline bind_ty<ConstantFP> m_ConstantFP(ConstantFP *&C) { return C; }

/// Match a ConstantExpr, capturing the value if we match.
inline bind_ty<ConstantExpr> m_ConstantExpr(ConstantExpr *&C) { return C; }

/// Match a basic block value, capturing it if we match.
inline bind_ty<BasicBlock> m_BasicBlock(BasicBlock *&V) { return V; }
inline bind_ty<const BasicBlock> m_BasicBlock(const BasicBlock *&V) {
  return V;
}

/// Match an arbitrary immediate Constant and ignore it.
inline match_combine_and<class_match<Constant>,
                         match_unless<constantexpr_match>>
m_ImmConstant() {
  return m_CombineAnd(m_Constant(), m_Unless(m_ConstantExpr()));
}

/// Match an immediate Constant, capturing the value if we match.
inline match_combine_and<bind_ty<Constant>,
                         match_unless<constantexpr_match>>
m_ImmConstant(Constant *&C) {
  return m_CombineAnd(m_Constant(C), m_Unless(m_ConstantExpr()));
}

/// Match a specified Value*.
struct specificval_ty {
  const Value *Val;

  specificval_ty(const Value *V) : Val(V) {}

  template <typename ITy> bool match(ITy *V) { return V == Val; }
};

/// Match if we have a specific specified value.
inline specificval_ty m_Specific(const Value *V) { return V; }

/// Stores a reference to the Value *, not the Value * itself,
/// thus can be used in commutative matchers.
template <typename Class> struct deferredval_ty {
  Class *const &Val;

  deferredval_ty(Class *const &V) : Val(V) {}

  template <typename ITy> bool match(ITy *const V) { return V == Val; }
};

/// Like m_Specific(), but works if the specific value to match is determined
/// as part of the same match() expression. For example:
/// m_Add(m_Value(X), m_Specific(X)) is incorrect, because m_Specific() will
/// bind X before the pattern match starts.
/// m_Add(m_Value(X), m_Deferred(X)) is correct, and will check against
/// whichever value m_Value(X) populated.
inline deferredval_ty<Value> m_Deferred(Value *const &V) { return V; }
inline deferredval_ty<const Value> m_Deferred(const Value *const &V) {
  return V;
}

/// Match a specified floating point value or vector of all elements of
/// that value.
struct specific_fpval {
  double Val;

  specific_fpval(double V) : Val(V) {}

  template <typename ITy> bool match(ITy *V) {
    if (const auto *CFP = dyn_cast<ConstantFP>(V))
      return CFP->isExactlyValue(Val);
    if (V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        if (auto *CFP = dyn_cast_or_null<ConstantFP>(C->getSplatValue()))
          return CFP->isExactlyValue(Val);
    return false;
  }
};

/// Match a specific floating point value or vector with all elements
/// equal to the value.
inline specific_fpval m_SpecificFP(double V) { return specific_fpval(V); }

/// Match a float 1.0 or vector with all elements equal to 1.0.
inline specific_fpval m_FPOne() { return m_SpecificFP(1.0); }

struct bind_const_intval_ty {
  uint64_t &VR;

  bind_const_intval_ty(uint64_t &V) : VR(V) {}

  template <typename ITy> bool match(ITy *V) {
    if (const auto *CV = dyn_cast<ConstantInt>(V))
      if (CV->getValue().ule(UINT64_MAX)) {
        VR = CV->getZExtValue();
        return true;
      }
    return false;
  }
};

/// Match a specified integer value or vector of all elements of that
/// value.
template <bool AllowPoison> struct specific_intval {
  const APInt &Val;

  specific_intval(const APInt &V) : Val(V) {}

  template <typename ITy> bool match(ITy *V) {
    const auto *CI = dyn_cast<ConstantInt>(V);
    if (!CI && V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        CI = dyn_cast_or_null<ConstantInt>(C->getSplatValue(AllowPoison));

    return CI && APInt::isSameValue(CI->getValue(), Val);
  }
};

template <bool AllowPoison> struct specific_intval64 {
  uint64_t Val;

  specific_intval64(uint64_t V) : Val(V) {}

  template <typename ITy> bool match(ITy *V) {
    const auto *CI = dyn_cast<ConstantInt>(V);
    if (!CI && V->getType()->isVectorTy())
      if (const auto *C = dyn_cast<Constant>(V))
        CI = dyn_cast_or_null<ConstantInt>(C->getSplatValue(AllowPoison));

    return CI && CI->getValue() == Val;
  }
};

/// Match a specific integer value or vector with all elements equal to
/// the value.
inline specific_intval<false> m_SpecificInt(const APInt &V) {
  return specific_intval<false>(V);
}

inline specific_intval64<false> m_SpecificInt(uint64_t V) {
  return specific_intval64<false>(V);
}

inline specific_intval<true> m_SpecificIntAllowPoison(const APInt &V) {
  return specific_intval<true>(V);
}

inline specific_intval64<true> m_SpecificIntAllowPoison(uint64_t V) {
  return specific_intval64<true>(V);
}

/// Match a ConstantInt and bind to its value.  This does not match
/// ConstantInts wider than 64-bits.
inline bind_const_intval_ty m_ConstantInt(uint64_t &V) { return V; }

/// Match a specified basic block value.
struct specific_bbval {
  BasicBlock *Val;

  specific_bbval(BasicBlock *Val) : Val(Val) {}

  template <typename ITy> bool match(ITy *V) {
    const auto *BB = dyn_cast<BasicBlock>(V);
    return BB && BB == Val;
  }
};

/// Match a specific basic block value.
inline specific_bbval m_SpecificBB(BasicBlock *BB) {
  return specific_bbval(BB);
}

/// A commutative-friendly version of m_Specific().
inline deferredval_ty<BasicBlock> m_Deferred(BasicBlock *const &BB) {
  return BB;
}
inline deferredval_ty<const BasicBlock>
m_Deferred(const BasicBlock *const &BB) {
  return BB;
}

//===----------------------------------------------------------------------===//
// Matcher for any binary operator.
//
template <typename LHS_t, typename RHS_t, bool Commutable = false>
struct AnyBinaryOp_match {
  LHS_t L;
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  AnyBinaryOp_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<BinaryOperator>(V))
      return (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
             (Commutable && L.match(I->getOperand(1)) &&
              R.match(I->getOperand(0)));
    return false;
  }
};

template <typename LHS, typename RHS>
inline AnyBinaryOp_match<LHS, RHS> m_BinOp(const LHS &L, const RHS &R) {
  return AnyBinaryOp_match<LHS, RHS>(L, R);
}

//===----------------------------------------------------------------------===//
// Matcher for any unary operator.
// TODO fuse unary, binary matcher into n-ary matcher
//
template <typename OP_t> struct AnyUnaryOp_match {
  OP_t X;

  AnyUnaryOp_match(const OP_t &X) : X(X) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<UnaryOperator>(V))
      return X.match(I->getOperand(0));
    return false;
  }
};

template <typename OP_t> inline AnyUnaryOp_match<OP_t> m_UnOp(const OP_t &X) {
  return AnyUnaryOp_match<OP_t>(X);
}

//===----------------------------------------------------------------------===//
// Matchers for specific binary operators.
//

template <typename LHS_t, typename RHS_t, unsigned Opcode,
          bool Commutable = false>
struct BinaryOp_match {
  LHS_t L;
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  BinaryOp_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  template <typename OpTy> inline bool match(unsigned Opc, OpTy *V) {
    if (V->getValueID() == Value::InstructionVal + Opc) {
      auto *I = cast<BinaryOperator>(V);
      return (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
             (Commutable && L.match(I->getOperand(1)) &&
              R.match(I->getOperand(0)));
    }
    return false;
  }

  template <typename OpTy> bool match(OpTy *V) { return match(Opcode, V); }
};

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Add> m_Add(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Add>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FAdd> m_FAdd(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FAdd>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Sub> m_Sub(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Sub>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FSub> m_FSub(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FSub>(L, R);
}

template <typename Op_t> struct FNeg_match {
  Op_t X;

  FNeg_match(const Op_t &Op) : X(Op) {}
  template <typename OpTy> bool match(OpTy *V) {
    auto *FPMO = dyn_cast<FPMathOperator>(V);
    if (!FPMO)
      return false;

    if (FPMO->getOpcode() == Instruction::FNeg)
      return X.match(FPMO->getOperand(0));

    if (FPMO->getOpcode() == Instruction::FSub) {
      if (FPMO->hasNoSignedZeros()) {
        // With 'nsz', any zero goes.
        if (!cstfp_pred_ty<is_any_zero_fp>().match(FPMO->getOperand(0)))
          return false;
      } else {
        // Without 'nsz', we need fsub -0.0, X exactly.
        if (!cstfp_pred_ty<is_neg_zero_fp>().match(FPMO->getOperand(0)))
          return false;
      }

      return X.match(FPMO->getOperand(1));
    }

    return false;
  }
};

/// Match 'fneg X' as 'fsub -0.0, X'.
template <typename OpTy> inline FNeg_match<OpTy> m_FNeg(const OpTy &X) {
  return FNeg_match<OpTy>(X);
}

/// Match 'fneg X' as 'fsub +-0.0, X'.
template <typename RHS>
inline BinaryOp_match<cstfp_pred_ty<is_any_zero_fp>, RHS, Instruction::FSub>
m_FNegNSZ(const RHS &X) {
  return m_FSub(m_AnyZeroFP(), X);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Mul> m_Mul(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Mul>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FMul> m_FMul(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FMul>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::UDiv> m_UDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::UDiv>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::SDiv> m_SDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::SDiv>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FDiv> m_FDiv(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FDiv>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::URem> m_URem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::URem>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::SRem> m_SRem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::SRem>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FRem> m_FRem(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FRem>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::And> m_And(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::And>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Or> m_Or(const LHS &L,
                                                      const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Or>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Xor> m_Xor(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Xor>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Shl> m_Shl(const LHS &L,
                                                        const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Shl>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::LShr> m_LShr(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::LShr>(L, R);
}

template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::AShr> m_AShr(const LHS &L,
                                                          const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::AShr>(L, R);
}

template <typename LHS_t, typename RHS_t, unsigned Opcode,
          unsigned WrapFlags = 0, bool Commutable = false>
struct OverflowingBinaryOp_match {
  LHS_t L;
  RHS_t R;

  OverflowingBinaryOp_match(const LHS_t &LHS, const RHS_t &RHS)
      : L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *Op = dyn_cast<OverflowingBinaryOperator>(V)) {
      if (Op->getOpcode() != Opcode)
        return false;
      if ((WrapFlags & OverflowingBinaryOperator::NoUnsignedWrap) &&
          !Op->hasNoUnsignedWrap())
        return false;
      if ((WrapFlags & OverflowingBinaryOperator::NoSignedWrap) &&
          !Op->hasNoSignedWrap())
        return false;
      return (L.match(Op->getOperand(0)) && R.match(Op->getOperand(1))) ||
             (Commutable && L.match(Op->getOperand(1)) &&
              R.match(Op->getOperand(0)));
    }
    return false;
  }
};

template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWSub(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWMul(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWShl(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                   OverflowingBinaryOperator::NoSignedWrap>(L,
                                                                            R);
}

template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}

template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<
    LHS, RHS, Instruction::Add, OverflowingBinaryOperator::NoUnsignedWrap, true>
m_c_NUWAdd(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                                   OverflowingBinaryOperator::NoUnsignedWrap,
                                   true>(L, R);
}

template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWSub(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Sub,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWMul(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Mul,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}
template <typename LHS, typename RHS>
inline OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                 OverflowingBinaryOperator::NoUnsignedWrap>
m_NUWShl(const LHS &L, const RHS &R) {
  return OverflowingBinaryOp_match<LHS, RHS, Instruction::Shl,
                                   OverflowingBinaryOperator::NoUnsignedWrap>(
      L, R);
}

template <typename LHS_t, typename RHS_t, bool Commutable = false>
struct SpecificBinaryOp_match
    : public BinaryOp_match<LHS_t, RHS_t, 0, Commutable> {
  unsigned Opcode;

  SpecificBinaryOp_match(unsigned Opcode, const LHS_t &LHS, const RHS_t &RHS)
      : BinaryOp_match<LHS_t, RHS_t, 0, Commutable>(LHS, RHS), Opcode(Opcode) {}

  template <typename OpTy> bool match(OpTy *V) {
    return BinaryOp_match<LHS_t, RHS_t, 0, Commutable>::match(Opcode, V);
  }
};

/// Matches a specific opcode.
template <typename LHS, typename RHS>
inline SpecificBinaryOp_match<LHS, RHS> m_BinOp(unsigned Opcode, const LHS &L,
                                                const RHS &R) {
  return SpecificBinaryOp_match<LHS, RHS>(Opcode, L, R);
}

template <typename LHS, typename RHS, bool Commutable = false>
struct DisjointOr_match {
  LHS L;
  RHS R;

  DisjointOr_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *PDI = dyn_cast<PossiblyDisjointInst>(V)) {
      assert(PDI->getOpcode() == Instruction::Or && "Only or can be disjoint");
      if (!PDI->isDisjoint())
        return false;
      return (L.match(PDI->getOperand(0)) && R.match(PDI->getOperand(1))) ||
             (Commutable && L.match(PDI->getOperand(1)) &&
              R.match(PDI->getOperand(0)));
    }
    return false;
  }
};

template <typename LHS, typename RHS>
inline DisjointOr_match<LHS, RHS> m_DisjointOr(const LHS &L, const RHS &R) {
  return DisjointOr_match<LHS, RHS>(L, R);
}

template <typename LHS, typename RHS>
inline DisjointOr_match<LHS, RHS, true> m_c_DisjointOr(const LHS &L,
                                                       const RHS &R) {
  return DisjointOr_match<LHS, RHS, true>(L, R);
}

/// Match either "add" or "or disjoint".
template <typename LHS, typename RHS>
inline match_combine_or<BinaryOp_match<LHS, RHS, Instruction::Add>,
                        DisjointOr_match<LHS, RHS>>
m_AddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_Add(L, R), m_DisjointOr(L, R));
}

/// Match either "add nsw" or "or disjoint"
template <typename LHS, typename RHS>
inline match_combine_or<
    OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                              OverflowingBinaryOperator::NoSignedWrap>,
    DisjointOr_match<LHS, RHS>>
m_NSWAddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_NSWAdd(L, R), m_DisjointOr(L, R));
}

/// Match either "add nuw" or "or disjoint"
template <typename LHS, typename RHS>
inline match_combine_or<
    OverflowingBinaryOp_match<LHS, RHS, Instruction::Add,
                              OverflowingBinaryOperator::NoUnsignedWrap>,
    DisjointOr_match<LHS, RHS>>
m_NUWAddLike(const LHS &L, const RHS &R) {
  return m_CombineOr(m_NUWAdd(L, R), m_DisjointOr(L, R));
}

//===----------------------------------------------------------------------===//
// Class that matches a group of binary opcodes.
//
template <typename LHS_t, typename RHS_t, typename Predicate,
          bool Commutable = false>
struct BinOpPred_match : Predicate {
  LHS_t L;
  RHS_t R;

  BinOpPred_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<Instruction>(V))
      return this->isOpType(I->getOpcode()) &&
             ((L.match(I->getOperand(0)) && R.match(I->getOperand(1))) ||
              (Commutable && L.match(I->getOperand(1)) &&
               R.match(I->getOperand(0))));
    return false;
  }
};

struct is_shift_op {
  bool isOpType(unsigned Opcode) { return Instruction::isShift(Opcode); }
};

struct is_right_shift_op {
  bool isOpType(unsigned Opcode) {
    return Opcode == Instruction::LShr || Opcode == Instruction::AShr;
  }
};

struct is_logical_shift_op {
  bool isOpType(unsigned Opcode) {
    return Opcode == Instruction::LShr || Opcode == Instruction::Shl;
  }
};

struct is_bitwiselogic_op {
  bool isOpType(unsigned Opcode) {
    return Instruction::isBitwiseLogicOp(Opcode);
  }
};

struct is_idiv_op {
  bool isOpType(unsigned Opcode) {
    return Opcode == Instruction::SDiv || Opcode == Instruction::UDiv;
  }
};

struct is_irem_op {
  bool isOpType(unsigned Opcode) {
    return Opcode == Instruction::SRem || Opcode == Instruction::URem;
  }
};

/// Matches shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_shift_op> m_Shift(const LHS &L,
                                                      const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_shift_op>(L, R);
}

/// Matches logical shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_right_shift_op> m_Shr(const LHS &L,
                                                          const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_right_shift_op>(L, R);
}

/// Matches logical shift operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_logical_shift_op>
m_LogicalShift(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_logical_shift_op>(L, R);
}

/// Matches bitwise logic operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_bitwiselogic_op>
m_BitwiseLogic(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_bitwiselogic_op>(L, R);
}

/// Matches bitwise logic operations in either order.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_bitwiselogic_op, true>
m_c_BitwiseLogic(const LHS &L, const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_bitwiselogic_op, true>(L, R);
}

/// Matches integer division operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_idiv_op> m_IDiv(const LHS &L,
                                                    const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_idiv_op>(L, R);
}

/// Matches integer remainder operations.
template <typename LHS, typename RHS>
inline BinOpPred_match<LHS, RHS, is_irem_op> m_IRem(const LHS &L,
                                                    const RHS &R) {
  return BinOpPred_match<LHS, RHS, is_irem_op>(L, R);
}

//===----------------------------------------------------------------------===//
// Class that matches exact binary ops.
//
template <typename SubPattern_t> struct Exact_match {
  SubPattern_t SubPattern;

  Exact_match(const SubPattern_t &SP) : SubPattern(SP) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *PEO = dyn_cast<PossiblyExactOperator>(V))
      return PEO->isExact() && SubPattern.match(V);
    return false;
  }
};

template <typename T> inline Exact_match<T> m_Exact(const T &SubPattern) {
  return SubPattern;
}

//===----------------------------------------------------------------------===//
// Matchers for CmpInst classes
//

template <typename LHS_t, typename RHS_t, typename Class, typename PredicateTy,
          bool Commutable = false>
struct CmpClass_match {
  PredicateTy *Predicate;
  LHS_t L;
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  CmpClass_match(PredicateTy &Pred, const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(&Pred), L(LHS), R(RHS) {}
  CmpClass_match(const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(nullptr), L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<Class>(V)) {
      if (L.match(I->getOperand(0)) && R.match(I->getOperand(1))) {
        if (Predicate)
          *Predicate = I->getPredicate();
        return true;
      } else if (Commutable && L.match(I->getOperand(1)) &&
                 R.match(I->getOperand(0))) {
        if (Predicate)
          *Predicate = I->getSwappedPredicate();
        return true;
      }
    }
    return false;
  }
};

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>
m_Cmp(CmpInst::Predicate &Pred, const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>(Pred, L, R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>
m_ICmp(ICmpInst::Predicate &Pred, const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>(Pred, L, R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>
m_FCmp(FCmpInst::Predicate &Pred, const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>(Pred, L, R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>
m_Cmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>(L, R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>
m_ICmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>(L, R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>
m_FCmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>(L, R);
}

// Same as CmpClass, but instead of saving Pred as out output variable, match a
// specific input pred for equality.
template <typename LHS_t, typename RHS_t, typename Class, typename PredicateTy>
struct SpecificCmpClass_match {
  const PredicateTy Predicate;
  LHS_t L;
  RHS_t R;

  SpecificCmpClass_match(PredicateTy Pred, const LHS_t &LHS, const RHS_t &RHS)
      : Predicate(Pred), L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<Class>(V))
      return I->getPredicate() == Predicate && L.match(I->getOperand(0)) &&
             R.match(I->getOperand(1));
    return false;
  }
};

template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>
m_SpecificCmp(CmpInst::Predicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, CmpInst, CmpInst::Predicate>(
      MatchPred, L, R);
}

template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>
m_SpecificICmp(ICmpInst::Predicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate>(
      MatchPred, L, R);
}

template <typename LHS, typename RHS>
inline SpecificCmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>
m_SpecificFCmp(FCmpInst::Predicate MatchPred, const LHS &L, const RHS &R) {
  return SpecificCmpClass_match<LHS, RHS, FCmpInst, FCmpInst::Predicate>(
      MatchPred, L, R);
}

//===----------------------------------------------------------------------===//
// Matchers for instructions with a given opcode and number of operands.
//

/// Matches instructions with Opcode and three operands.
template <typename T0, unsigned Opcode> struct OneOps_match {
  T0 Op1;

  OneOps_match(const T0 &Op1) : Op1(Op1) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return Op1.match(I->getOperand(0));
    }
    return false;
  }
};

/// Matches instructions with Opcode and three operands.
template <typename T0, typename T1, unsigned Opcode> struct TwoOps_match {
  T0 Op1;
  T1 Op2;

  TwoOps_match(const T0 &Op1, const T1 &Op2) : Op1(Op1), Op2(Op2) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return Op1.match(I->getOperand(0)) && Op2.match(I->getOperand(1));
    }
    return false;
  }
};

/// Matches instructions with Opcode and three operands.
template <typename T0, typename T1, typename T2, unsigned Opcode>
struct ThreeOps_match {
  T0 Op1;
  T1 Op2;
  T2 Op3;

  ThreeOps_match(const T0 &Op1, const T1 &Op2, const T2 &Op3)
      : Op1(Op1), Op2(Op2), Op3(Op3) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return Op1.match(I->getOperand(0)) && Op2.match(I->getOperand(1)) &&
             Op3.match(I->getOperand(2));
    }
    return false;
  }
};

/// Matches instructions with Opcode and any number of operands
template <unsigned Opcode, typename... OperandTypes> struct AnyOps_match {
  std::tuple<OperandTypes...> Operands;

  AnyOps_match(const OperandTypes &...Ops) : Operands(Ops...) {}

  // Operand matching works by recursively calling match_operands, matching the
  // operands left to right. The first version is called for each operand but
  // the last, for which the second version is called. The second version of
  // match_operands is also used to match each individual operand.
  template <int Idx, int Last>
  std::enable_if_t<Idx != Last, bool> match_operands(const Instruction *I) {
    return match_operands<Idx, Idx>(I) && match_operands<Idx + 1, Last>(I);
  }

  template <int Idx, int Last>
  std::enable_if_t<Idx == Last, bool> match_operands(const Instruction *I) {
    return std::get<Idx>(Operands).match(I->getOperand(Idx));
  }

  template <typename OpTy> bool match(OpTy *V) {
    if (V->getValueID() == Value::InstructionVal + Opcode) {
      auto *I = cast<Instruction>(V);
      return I->getNumOperands() == sizeof...(OperandTypes) &&
             match_operands<0, sizeof...(OperandTypes) - 1>(I);
    }
    return false;
  }
};

/// Matches SelectInst.
template <typename Cond, typename LHS, typename RHS>
inline ThreeOps_match<Cond, LHS, RHS, Instruction::Select>
m_Select(const Cond &C, const LHS &L, const RHS &R) {
  return ThreeOps_match<Cond, LHS, RHS, Instruction::Select>(C, L, R);
}

/// This matches a select of two constants, e.g.:
/// m_SelectCst<-1, 0>(m_Value(V))
template <int64_t L, int64_t R, typename Cond>
inline ThreeOps_match<Cond, constantint_match<L>, constantint_match<R>,
                      Instruction::Select>
m_SelectCst(const Cond &C) {
  return m_Select(C, m_ConstantInt<L>(), m_ConstantInt<R>());
}

/// Matches FreezeInst.
template <typename OpTy>
inline OneOps_match<OpTy, Instruction::Freeze> m_Freeze(const OpTy &Op) {
  return OneOps_match<OpTy, Instruction::Freeze>(Op);
}

/// Matches InsertElementInst.
template <typename Val_t, typename Elt_t, typename Idx_t>
inline ThreeOps_match<Val_t, Elt_t, Idx_t, Instruction::InsertElement>
m_InsertElt(const Val_t &Val, const Elt_t &Elt, const Idx_t &Idx) {
  return ThreeOps_match<Val_t, Elt_t, Idx_t, Instruction::InsertElement>(
      Val, Elt, Idx);
}

/// Matches ExtractElementInst.
template <typename Val_t, typename Idx_t>
inline TwoOps_match<Val_t, Idx_t, Instruction::ExtractElement>
m_ExtractElt(const Val_t &Val, const Idx_t &Idx) {
  return TwoOps_match<Val_t, Idx_t, Instruction::ExtractElement>(Val, Idx);
}

/// Matches shuffle.
template <typename T0, typename T1, typename T2> struct Shuffle_match {
  T0 Op1;
  T1 Op2;
  T2 Mask;

  Shuffle_match(const T0 &Op1, const T1 &Op2, const T2 &Mask)
      : Op1(Op1), Op2(Op2), Mask(Mask) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<ShuffleVectorInst>(V)) {
      return Op1.match(I->getOperand(0)) && Op2.match(I->getOperand(1)) &&
             Mask.match(I->getShuffleMask());
    }
    return false;
  }
};

struct m_Mask {
  ArrayRef<int> &MaskRef;
  m_Mask(ArrayRef<int> &MaskRef) : MaskRef(MaskRef) {}
  bool match(ArrayRef<int> Mask) {
    MaskRef = Mask;
    return true;
  }
};

struct m_ZeroMask {
  bool match(ArrayRef<int> Mask) {
    return all_of(Mask, [](int Elem) { return Elem == 0 || Elem == -1; });
  }
};

struct m_SpecificMask {
  ArrayRef<int> &MaskRef;
  m_SpecificMask(ArrayRef<int> &MaskRef) : MaskRef(MaskRef) {}
  bool match(ArrayRef<int> Mask) { return MaskRef == Mask; }
};

struct m_SplatOrPoisonMask {
  int &SplatIndex;
  m_SplatOrPoisonMask(int &SplatIndex) : SplatIndex(SplatIndex) {}
  bool match(ArrayRef<int> Mask) {
    const auto *First = find_if(Mask, [](int Elem) { return Elem != -1; });
    if (First == Mask.end())
      return false;
    SplatIndex = *First;
    return all_of(Mask,
                  [First](int Elem) { return Elem == *First || Elem == -1; });
  }
};

template <typename PointerOpTy, typename OffsetOpTy> struct PtrAdd_match {
  PointerOpTy PointerOp;
  OffsetOpTy OffsetOp;

  PtrAdd_match(const PointerOpTy &PointerOp, const OffsetOpTy &OffsetOp)
      : PointerOp(PointerOp), OffsetOp(OffsetOp) {}

  template <typename OpTy> bool match(OpTy *V) {
    auto *GEP = dyn_cast<GEPOperator>(V);
    return GEP && GEP->getSourceElementType()->isIntegerTy(8) &&
           PointerOp.match(GEP->getPointerOperand()) &&
           OffsetOp.match(GEP->idx_begin()->get());
  }
};

/// Matches ShuffleVectorInst independently of mask value.
template <typename V1_t, typename V2_t>
inline TwoOps_match<V1_t, V2_t, Instruction::ShuffleVector>
m_Shuffle(const V1_t &v1, const V2_t &v2) {
  return TwoOps_match<V1_t, V2_t, Instruction::ShuffleVector>(v1, v2);
}

template <typename V1_t, typename V2_t, typename Mask_t>
inline Shuffle_match<V1_t, V2_t, Mask_t>
m_Shuffle(const V1_t &v1, const V2_t &v2, const Mask_t &mask) {
  return Shuffle_match<V1_t, V2_t, Mask_t>(v1, v2, mask);
}

/// Matches LoadInst.
template <typename OpTy>
inline OneOps_match<OpTy, Instruction::Load> m_Load(const OpTy &Op) {
  return OneOps_match<OpTy, Instruction::Load>(Op);
}

/// Matches StoreInst.
template <typename ValueOpTy, typename PointerOpTy>
inline TwoOps_match<ValueOpTy, PointerOpTy, Instruction::Store>
m_Store(const ValueOpTy &ValueOp, const PointerOpTy &PointerOp) {
  return TwoOps_match<ValueOpTy, PointerOpTy, Instruction::Store>(ValueOp,
                                                                  PointerOp);
}

/// Matches GetElementPtrInst.
template <typename... OperandTypes>
inline auto m_GEP(const OperandTypes &...Ops) {
  return AnyOps_match<Instruction::GetElementPtr, OperandTypes...>(Ops...);
}

/// Matches GEP with i8 source element type
template <typename PointerOpTy, typename OffsetOpTy>
inline PtrAdd_match<PointerOpTy, OffsetOpTy>
m_PtrAdd(const PointerOpTy &PointerOp, const OffsetOpTy &OffsetOp) {
  return PtrAdd_match<PointerOpTy, OffsetOpTy>(PointerOp, OffsetOp);
}

//===----------------------------------------------------------------------===//
// Matchers for CastInst classes
//

template <typename Op_t, unsigned Opcode> struct CastOperator_match {
  Op_t Op;

  CastOperator_match(const Op_t &OpMatch) : Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *O = dyn_cast<Operator>(V))
      return O->getOpcode() == Opcode && Op.match(O->getOperand(0));
    return false;
  }
};

template <typename Op_t, typename Class> struct CastInst_match {
  Op_t Op;

  CastInst_match(const Op_t &OpMatch) : Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<Class>(V))
      return Op.match(I->getOperand(0));
    return false;
  }
};

template <typename Op_t> struct PtrToIntSameSize_match {
  const DataLayout &DL;
  Op_t Op;

  PtrToIntSameSize_match(const DataLayout &DL, const Op_t &OpMatch)
      : DL(DL), Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *O = dyn_cast<Operator>(V))
      return O->getOpcode() == Instruction::PtrToInt &&
             DL.getTypeSizeInBits(O->getType()) ==
                 DL.getTypeSizeInBits(O->getOperand(0)->getType()) &&
             Op.match(O->getOperand(0));
    return false;
  }
};

template <typename Op_t> struct NNegZExt_match {
  Op_t Op;

  NNegZExt_match(const Op_t &OpMatch) : Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<ZExtInst>(V))
      return I->hasNonNeg() && Op.match(I->getOperand(0));
    return false;
  }
};

template <typename Op_t, unsigned WrapFlags = 0> struct NoWrapTrunc_match {
  Op_t Op;

  NoWrapTrunc_match(const Op_t &OpMatch) : Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<TruncInst>(V))
      return (I->getNoWrapKind() & WrapFlags) == WrapFlags &&
             Op.match(I->getOperand(0));
    return false;
  }
};

/// Matches BitCast.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::BitCast>
m_BitCast(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::BitCast>(Op);
}

template <typename Op_t> struct ElementWiseBitCast_match {
  Op_t Op;

  ElementWiseBitCast_match(const Op_t &OpMatch) : Op(OpMatch) {}

  template <typename OpTy> bool match(OpTy *V) {
    auto *I = dyn_cast<BitCastInst>(V);
    if (!I)
      return false;
    Type *SrcType = I->getSrcTy();
    Type *DstType = I->getType();
    // Make sure the bitcast doesn't change between scalar and vector and
    // doesn't change the number of vector elements.
    if (SrcType->isVectorTy() != DstType->isVectorTy())
      return false;
    if (VectorType *SrcVecTy = dyn_cast<VectorType>(SrcType);
        SrcVecTy && SrcVecTy->getElementCount() !=
                        cast<VectorType>(DstType)->getElementCount())
      return false;
    return Op.match(I->getOperand(0));
  }
};

template <typename OpTy>
inline ElementWiseBitCast_match<OpTy> m_ElementWiseBitCast(const OpTy &Op) {
  return ElementWiseBitCast_match<OpTy>(Op);
}

/// Matches PtrToInt.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::PtrToInt>
m_PtrToInt(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::PtrToInt>(Op);
}

template <typename OpTy>
inline PtrToIntSameSize_match<OpTy> m_PtrToIntSameSize(const DataLayout &DL,
                                                       const OpTy &Op) {
  return PtrToIntSameSize_match<OpTy>(DL, Op);
}

/// Matches IntToPtr.
template <typename OpTy>
inline CastOperator_match<OpTy, Instruction::IntToPtr>
m_IntToPtr(const OpTy &Op) {
  return CastOperator_match<OpTy, Instruction::IntToPtr>(Op);
}

/// Matches Trunc.
template <typename OpTy>
inline CastInst_match<OpTy, TruncInst> m_Trunc(const OpTy &Op) {
  return CastInst_match<OpTy, TruncInst>(Op);
}

/// Matches trunc nuw.
template <typename OpTy>
inline NoWrapTrunc_match<OpTy, TruncInst::NoUnsignedWrap>
m_NUWTrunc(const OpTy &Op) {
  return NoWrapTrunc_match<OpTy, TruncInst::NoUnsignedWrap>(Op);
}

/// Matches trunc nsw.
template <typename OpTy>
inline NoWrapTrunc_match<OpTy, TruncInst::NoSignedWrap>
m_NSWTrunc(const OpTy &Op) {
  return NoWrapTrunc_match<OpTy, TruncInst::NoSignedWrap>(Op);
}

template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, TruncInst>, OpTy>
m_TruncOrSelf(const OpTy &Op) {
  return m_CombineOr(m_Trunc(Op), Op);
}

/// Matches SExt.
template <typename OpTy>
inline CastInst_match<OpTy, SExtInst> m_SExt(const OpTy &Op) {
  return CastInst_match<OpTy, SExtInst>(Op);
}

/// Matches ZExt.
template <typename OpTy>
inline CastInst_match<OpTy, ZExtInst> m_ZExt(const OpTy &Op) {
  return CastInst_match<OpTy, ZExtInst>(Op);
}

template <typename OpTy>
inline NNegZExt_match<OpTy> m_NNegZExt(const OpTy &Op) {
  return NNegZExt_match<OpTy>(Op);
}

template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, ZExtInst>, OpTy>
m_ZExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_ZExt(Op), Op);
}

template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, SExtInst>, OpTy>
m_SExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_SExt(Op), Op);
}

/// Match either "sext" or "zext nneg".
template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, SExtInst>, NNegZExt_match<OpTy>>
m_SExtLike(const OpTy &Op) {
  return m_CombineOr(m_SExt(Op), m_NNegZExt(Op));
}

template <typename OpTy>
inline match_combine_or<CastInst_match<OpTy, ZExtInst>,
                        CastInst_match<OpTy, SExtInst>>
m_ZExtOrSExt(const OpTy &Op) {
  return m_CombineOr(m_ZExt(Op), m_SExt(Op));
}

template <typename OpTy>
inline match_combine_or<match_combine_or<CastInst_match<OpTy, ZExtInst>,
                                         CastInst_match<OpTy, SExtInst>>,
                        OpTy>
m_ZExtOrSExtOrSelf(const OpTy &Op) {
  return m_CombineOr(m_ZExtOrSExt(Op), Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, UIToFPInst> m_UIToFP(const OpTy &Op) {
  return CastInst_match<OpTy, UIToFPInst>(Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, SIToFPInst> m_SIToFP(const OpTy &Op) {
  return CastInst_match<OpTy, SIToFPInst>(Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, FPToUIInst> m_FPToUI(const OpTy &Op) {
  return CastInst_match<OpTy, FPToUIInst>(Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, FPToSIInst> m_FPToSI(const OpTy &Op) {
  return CastInst_match<OpTy, FPToSIInst>(Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, FPTruncInst> m_FPTrunc(const OpTy &Op) {
  return CastInst_match<OpTy, FPTruncInst>(Op);
}

template <typename OpTy>
inline CastInst_match<OpTy, FPExtInst> m_FPExt(const OpTy &Op) {
  return CastInst_match<OpTy, FPExtInst>(Op);
}

//===----------------------------------------------------------------------===//
// Matchers for control flow.
//

struct br_match {
  BasicBlock *&Succ;

  br_match(BasicBlock *&Succ) : Succ(Succ) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *BI = dyn_cast<BranchInst>(V))
      if (BI->isUnconditional()) {
        Succ = BI->getSuccessor(0);
        return true;
      }
    return false;
  }
};

inline br_match m_UnconditionalBr(BasicBlock *&Succ) { return br_match(Succ); }

template <typename Cond_t, typename TrueBlock_t, typename FalseBlock_t>
struct brc_match {
  Cond_t Cond;
  TrueBlock_t T;
  FalseBlock_t F;

  brc_match(const Cond_t &C, const TrueBlock_t &t, const FalseBlock_t &f)
      : Cond(C), T(t), F(f) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *BI = dyn_cast<BranchInst>(V))
      if (BI->isConditional() && Cond.match(BI->getCondition()))
        return T.match(BI->getSuccessor(0)) && F.match(BI->getSuccessor(1));
    return false;
  }
};

template <typename Cond_t>
inline brc_match<Cond_t, bind_ty<BasicBlock>, bind_ty<BasicBlock>>
m_Br(const Cond_t &C, BasicBlock *&T, BasicBlock *&F) {
  return brc_match<Cond_t, bind_ty<BasicBlock>, bind_ty<BasicBlock>>(
      C, m_BasicBlock(T), m_BasicBlock(F));
}

template <typename Cond_t, typename TrueBlock_t, typename FalseBlock_t>
inline brc_match<Cond_t, TrueBlock_t, FalseBlock_t>
m_Br(const Cond_t &C, const TrueBlock_t &T, const FalseBlock_t &F) {
  return brc_match<Cond_t, TrueBlock_t, FalseBlock_t>(C, T, F);
}

//===----------------------------------------------------------------------===//
// Matchers for max/min idioms, eg: "select (sgt x, y), x, y" -> smax(x,y).
//

template <typename CmpInst_t, typename LHS_t, typename RHS_t, typename Pred_t,
          bool Commutable = false>
struct MaxMin_match {
  using PredType = Pred_t;
  LHS_t L;
  RHS_t R;

  // The evaluation order is always stable, regardless of Commutability.
  // The LHS is always matched first.
  MaxMin_match(const LHS_t &LHS, const RHS_t &RHS) : L(LHS), R(RHS) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *II = dyn_cast<IntrinsicInst>(V)) {
      Intrinsic::ID IID = II->getIntrinsicID();
      if ((IID == Intrinsic::smax && Pred_t::match(ICmpInst::ICMP_SGT)) ||
          (IID == Intrinsic::smin && Pred_t::match(ICmpInst::ICMP_SLT)) ||
          (IID == Intrinsic::umax && Pred_t::match(ICmpInst::ICMP_UGT)) ||
          (IID == Intrinsic::umin && Pred_t::match(ICmpInst::ICMP_ULT))) {
        Value *LHS = II->getOperand(0), *RHS = II->getOperand(1);
        return (L.match(LHS) && R.match(RHS)) ||
               (Commutable && L.match(RHS) && R.match(LHS));
      }
    }
    // Look for "(x pred y) ? x : y" or "(x pred y) ? y : x".
    auto *SI = dyn_cast<SelectInst>(V);
    if (!SI)
      return false;
    auto *Cmp = dyn_cast<CmpInst_t>(SI->getCondition());
    if (!Cmp)
      return false;
    // At this point we have a select conditioned on a comparison.  Check that
    // it is the values returned by the select that are being compared.
    auto *TrueVal = SI->getTrueValue();
    auto *FalseVal = SI->getFalseValue();
    auto *LHS = Cmp->getOperand(0);
    auto *RHS = Cmp->getOperand(1);
    if ((TrueVal != LHS || FalseVal != RHS) &&
        (TrueVal != RHS || FalseVal != LHS))
      return false;
    typename CmpInst_t::Predicate Pred =
        LHS == TrueVal ? Cmp->getPredicate() : Cmp->getInversePredicate();
    // Does "(x pred y) ? x : y" represent the desired max/min operation?
    if (!Pred_t::match(Pred))
      return false;
    // It does!  Bind the operands.
    return (L.match(LHS) && R.match(RHS)) ||
           (Commutable && L.match(RHS) && R.match(LHS));
  }
};

/// Helper class for identifying signed max predicates.
struct smax_pred_ty {
  static bool match(ICmpInst::Predicate Pred) {
    return Pred == CmpInst::ICMP_SGT || Pred == CmpInst::ICMP_SGE;
  }
};

/// Helper class for identifying signed min predicates.
struct smin_pred_ty {
  static bool match(ICmpInst::Predicate Pred) {
    return Pred == CmpInst::ICMP_SLT || Pred == CmpInst::ICMP_SLE;
  }
};

/// Helper class for identifying unsigned max predicates.
struct umax_pred_ty {
  static bool match(ICmpInst::Predicate Pred) {
    return Pred == CmpInst::ICMP_UGT || Pred == CmpInst::ICMP_UGE;
  }
};

/// Helper class for identifying unsigned min predicates.
struct umin_pred_ty {
  static bool match(ICmpInst::Predicate Pred) {
    return Pred == CmpInst::ICMP_ULT || Pred == CmpInst::ICMP_ULE;
  }
};

/// Helper class for identifying ordered max predicates.
struct ofmax_pred_ty {
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_OGT || Pred == CmpInst::FCMP_OGE;
  }
};

/// Helper class for identifying ordered min predicates.
struct ofmin_pred_ty {
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_OLT || Pred == CmpInst::FCMP_OLE;
  }
};

/// Helper class for identifying unordered max predicates.
struct ufmax_pred_ty {
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_UGT || Pred == CmpInst::FCMP_UGE;
  }
};

/// Helper class for identifying unordered min predicates.
struct ufmin_pred_ty {
  static bool match(FCmpInst::Predicate Pred) {
    return Pred == CmpInst::FCMP_ULT || Pred == CmpInst::FCMP_ULE;
  }
};

template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty> m_SMax(const LHS &L,
                                                             const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty>(L, R);
}

template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty> m_SMin(const LHS &L,
                                                             const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty>(L, R);
}

template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty> m_UMax(const LHS &L,
                                                             const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty>(L, R);
}

template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty> m_UMin(const LHS &L,
                                                             const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty>(L, R);
}

template <typename LHS, typename RHS>
inline match_combine_or<
    match_combine_or<MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty>,
                     MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty>>,
    match_combine_or<MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty>,
                     MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty>>>
m_MaxOrMin(const LHS &L, const RHS &R) {
  return m_CombineOr(m_CombineOr(m_SMax(L, R), m_SMin(L, R)),
                     m_CombineOr(m_UMax(L, R), m_UMin(L, R)));
}

/// Match an 'ordered' floating point maximum function.
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'maximum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ogt/ge, L, R), L, R) semantics matched by this predicate.
///
///                         max(L, R)  iff L and R are not NaN
///  m_OrdFMax(L, R) =      R          iff L or R are NaN
template <typename LHS, typename RHS>
inline MaxMin_match<FCmpInst, LHS, RHS, ofmax_pred_ty> m_OrdFMax(const LHS &L,
                                                                 const RHS &R) {
  return MaxMin_match<FCmpInst, LHS, RHS, ofmax_pred_ty>(L, R);
}

/// Match an 'ordered' floating point minimum function.
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'minimum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(olt/le, L, R), L, R) semantics matched by this predicate.
///
///                         min(L, R)  iff L and R are not NaN
///  m_OrdFMin(L, R) =      R          iff L or R are NaN
template <typename LHS, typename RHS>
inline MaxMin_match<FCmpInst, LHS, RHS, ofmin_pred_ty> m_OrdFMin(const LHS &L,
                                                                 const RHS &R) {
  return MaxMin_match<FCmpInst, LHS, RHS, ofmin_pred_ty>(L, R);
}

/// Match an 'unordered' floating point maximum function.
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'maximum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ugt/ge, L, R), L, R) semantics matched by this predicate.
///
///                         max(L, R)  iff L and R are not NaN
///  m_UnordFMax(L, R) =    L          iff L or R are NaN
template <typename LHS, typename RHS>
inline MaxMin_match<FCmpInst, LHS, RHS, ufmax_pred_ty>
m_UnordFMax(const LHS &L, const RHS &R) {
  return MaxMin_match<FCmpInst, LHS, RHS, ufmax_pred_ty>(L, R);
}

/// Match an 'unordered' floating point minimum function.
/// Floating point has one special value 'NaN'. Therefore, there is no total
/// order. However, if we can ignore the 'NaN' value (for example, because of a
/// 'no-nans-float-math' flag) a combination of a fcmp and select has 'minimum'
/// semantics. In the presence of 'NaN' we have to preserve the original
/// select(fcmp(ult/le, L, R), L, R) semantics matched by this predicate.
///
///                          min(L, R)  iff L and R are not NaN
///  m_UnordFMin(L, R) =     L          iff L or R are NaN
template <typename LHS, typename RHS>
inline MaxMin_match<FCmpInst, LHS, RHS, ufmin_pred_ty>
m_UnordFMin(const LHS &L, const RHS &R) {
  return MaxMin_match<FCmpInst, LHS, RHS, ufmin_pred_ty>(L, R);
}

/// Matches a 'Not' as 'xor V, -1' or 'xor -1, V'.
/// NOTE: we first match the 'Not' (by matching '-1'),
/// and only then match the inner matcher!
template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_all_ones>, ValTy, Instruction::Xor, true>
m_Not(const ValTy &V) {
  return m_c_Xor(m_AllOnes(), V);
}

template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_all_ones, false>, ValTy, Instruction::Xor,
                      true>
m_NotForbidPoison(const ValTy &V) {
  return m_c_Xor(m_AllOnesForbidPoison(), V);
}

//===----------------------------------------------------------------------===//
// Matchers for overflow check patterns: e.g. (a + b) u< a, (a ^ -1) <u b
// Note that S might be matched to other instructions than AddInst.
//

template <typename LHS_t, typename RHS_t, typename Sum_t>
struct UAddWithOverflow_match {
  LHS_t L;
  RHS_t R;
  Sum_t S;

  UAddWithOverflow_match(const LHS_t &L, const RHS_t &R, const Sum_t &S)
      : L(L), R(R), S(S) {}

  template <typename OpTy> bool match(OpTy *V) {
    Value *ICmpLHS, *ICmpRHS;
    ICmpInst::Predicate Pred;
    if (!m_ICmp(Pred, m_Value(ICmpLHS), m_Value(ICmpRHS)).match(V))
      return false;

    Value *AddLHS, *AddRHS;
    auto AddExpr = m_Add(m_Value(AddLHS), m_Value(AddRHS));

    // (a + b) u< a, (a + b) u< b
    if (Pred == ICmpInst::ICMP_ULT)
      if (AddExpr.match(ICmpLHS) && (ICmpRHS == AddLHS || ICmpRHS == AddRHS))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpLHS);

    // a >u (a + b), b >u (a + b)
    if (Pred == ICmpInst::ICMP_UGT)
      if (AddExpr.match(ICmpRHS) && (ICmpLHS == AddLHS || ICmpLHS == AddRHS))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpRHS);

    Value *Op1;
    auto XorExpr = m_OneUse(m_Not(m_Value(Op1)));
    // (~a) <u b
    if (Pred == ICmpInst::ICMP_ULT) {
      if (XorExpr.match(ICmpLHS))
        return L.match(Op1) && R.match(ICmpRHS) && S.match(ICmpLHS);
    }
    //  b > u (~a)
    if (Pred == ICmpInst::ICMP_UGT) {
      if (XorExpr.match(ICmpRHS))
        return L.match(Op1) && R.match(ICmpLHS) && S.match(ICmpRHS);
    }

    // Match special-case for increment-by-1.
    if (Pred == ICmpInst::ICMP_EQ) {
      // (a + 1) == 0
      // (1 + a) == 0
      if (AddExpr.match(ICmpLHS) && m_ZeroInt().match(ICmpRHS) &&
          (m_One().match(AddLHS) || m_One().match(AddRHS)))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpLHS);
      // 0 == (a + 1)
      // 0 == (1 + a)
      if (m_ZeroInt().match(ICmpLHS) && AddExpr.match(ICmpRHS) &&
          (m_One().match(AddLHS) || m_One().match(AddRHS)))
        return L.match(AddLHS) && R.match(AddRHS) && S.match(ICmpRHS);
    }

    return false;
  }
};

/// Match an icmp instruction checking for unsigned overflow on addition.
///
/// S is matched to the addition whose result is being checked for overflow, and
/// L and R are matched to the LHS and RHS of S.
template <typename LHS_t, typename RHS_t, typename Sum_t>
UAddWithOverflow_match<LHS_t, RHS_t, Sum_t>
m_UAddWithOverflow(const LHS_t &L, const RHS_t &R, const Sum_t &S) {
  return UAddWithOverflow_match<LHS_t, RHS_t, Sum_t>(L, R, S);
}

template <typename Opnd_t> struct Argument_match {
  unsigned OpI;
  Opnd_t Val;

  Argument_match(unsigned OpIdx, const Opnd_t &V) : OpI(OpIdx), Val(V) {}

  template <typename OpTy> bool match(OpTy *V) {
    // FIXME: Should likely be switched to use `CallBase`.
    if (const auto *CI = dyn_cast<CallInst>(V))
      return Val.match(CI->getArgOperand(OpI));
    return false;
  }
};

/// Match an argument.
template <unsigned OpI, typename Opnd_t>
inline Argument_match<Opnd_t> m_Argument(const Opnd_t &Op) {
  return Argument_match<Opnd_t>(OpI, Op);
}

/// Intrinsic matchers.
struct IntrinsicID_match {
  unsigned ID;

  IntrinsicID_match(Intrinsic::ID IntrID) : ID(IntrID) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (const auto *CI = dyn_cast<CallInst>(V))
      if (const auto *F = CI->getCalledFunction())
        return F->getIntrinsicID() == ID;
    return false;
  }
};

/// Intrinsic matches are combinations of ID matchers, and argument
/// matchers. Higher arity matcher are defined recursively in terms of and-ing
/// them with lower arity matchers. Here's some convenient typedefs for up to
/// several arguments, and more can be added as needed
template <typename T0 = void, typename T1 = void, typename T2 = void,
          typename T3 = void, typename T4 = void, typename T5 = void,
          typename T6 = void, typename T7 = void, typename T8 = void,
          typename T9 = void, typename T10 = void>
struct m_Intrinsic_Ty;
template <typename T0> struct m_Intrinsic_Ty<T0> {
  using Ty = match_combine_and<IntrinsicID_match, Argument_match<T0>>;
};
template <typename T0, typename T1> struct m_Intrinsic_Ty<T0, T1> {
  using Ty =
      match_combine_and<typename m_Intrinsic_Ty<T0>::Ty, Argument_match<T1>>;
};
template <typename T0, typename T1, typename T2>
struct m_Intrinsic_Ty<T0, T1, T2> {
  using Ty = match_combine_and<typename m_Intrinsic_Ty<T0, T1>::Ty,
                               Argument_match<T2>>;
};
template <typename T0, typename T1, typename T2, typename T3>
struct m_Intrinsic_Ty<T0, T1, T2, T3> {
  using Ty = match_combine_and<typename m_Intrinsic_Ty<T0, T1, T2>::Ty,
                               Argument_match<T3>>;
};

template <typename T0, typename T1, typename T2, typename T3, typename T4>
struct m_Intrinsic_Ty<T0, T1, T2, T3, T4> {
  using Ty = match_combine_and<typename m_Intrinsic_Ty<T0, T1, T2, T3>::Ty,
                               Argument_match<T4>>;
};

template <typename T0, typename T1, typename T2, typename T3, typename T4,
          typename T5>
struct m_Intrinsic_Ty<T0, T1, T2, T3, T4, T5> {
  using Ty = match_combine_and<typename m_Intrinsic_Ty<T0, T1, T2, T3, T4>::Ty,
                               Argument_match<T5>>;
};

/// Match intrinsic calls like this:
/// m_Intrinsic<Intrinsic::fabs>(m_Value(X))
template <Intrinsic::ID IntrID> inline IntrinsicID_match m_Intrinsic() {
  return IntrinsicID_match(IntrID);
}

/// Matches MaskedLoad Intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2, typename Opnd3>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1, Opnd2, Opnd3>::Ty
m_MaskedLoad(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2,
             const Opnd3 &Op3) {
  return m_Intrinsic<Intrinsic::masked_load>(Op0, Op1, Op2, Op3);
}

/// Matches MaskedGather Intrinsic.
template <typename Opnd0, typename Opnd1, typename Opnd2, typename Opnd3>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1, Opnd2, Opnd3>::Ty
m_MaskedGather(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2,
               const Opnd3 &Op3) {
  return m_Intrinsic<Intrinsic::masked_gather>(Op0, Op1, Op2, Op3);
}

template <Intrinsic::ID IntrID, typename T0>
inline typename m_Intrinsic_Ty<T0>::Ty m_Intrinsic(const T0 &Op0) {
  return m_CombineAnd(m_Intrinsic<IntrID>(), m_Argument<0>(Op0));
}

template <Intrinsic::ID IntrID, typename T0, typename T1>
inline typename m_Intrinsic_Ty<T0, T1>::Ty m_Intrinsic(const T0 &Op0,
                                                       const T1 &Op1) {
  return m_CombineAnd(m_Intrinsic<IntrID>(Op0), m_Argument<1>(Op1));
}

template <Intrinsic::ID IntrID, typename T0, typename T1, typename T2>
inline typename m_Intrinsic_Ty<T0, T1, T2>::Ty
m_Intrinsic(const T0 &Op0, const T1 &Op1, const T2 &Op2) {
  return m_CombineAnd(m_Intrinsic<IntrID>(Op0, Op1), m_Argument<2>(Op2));
}

template <Intrinsic::ID IntrID, typename T0, typename T1, typename T2,
          typename T3>
inline typename m_Intrinsic_Ty<T0, T1, T2, T3>::Ty
m_Intrinsic(const T0 &Op0, const T1 &Op1, const T2 &Op2, const T3 &Op3) {
  return m_CombineAnd(m_Intrinsic<IntrID>(Op0, Op1, Op2), m_Argument<3>(Op3));
}

template <Intrinsic::ID IntrID, typename T0, typename T1, typename T2,
          typename T3, typename T4>
inline typename m_Intrinsic_Ty<T0, T1, T2, T3, T4>::Ty
m_Intrinsic(const T0 &Op0, const T1 &Op1, const T2 &Op2, const T3 &Op3,
            const T4 &Op4) {
  return m_CombineAnd(m_Intrinsic<IntrID>(Op0, Op1, Op2, Op3),
                      m_Argument<4>(Op4));
}

template <Intrinsic::ID IntrID, typename T0, typename T1, typename T2,
          typename T3, typename T4, typename T5>
inline typename m_Intrinsic_Ty<T0, T1, T2, T3, T4, T5>::Ty
m_Intrinsic(const T0 &Op0, const T1 &Op1, const T2 &Op2, const T3 &Op3,
            const T4 &Op4, const T5 &Op5) {
  return m_CombineAnd(m_Intrinsic<IntrID>(Op0, Op1, Op2, Op3, Op4),
                      m_Argument<5>(Op5));
}

// Helper intrinsic matching specializations.
template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_BitReverse(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::bitreverse>(Op0);
}

template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_BSwap(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::bswap>(Op0);
}

template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_FAbs(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::fabs>(Op0);
}

template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_FCanonicalize(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::canonicalize>(Op0);
}

template <typename Opnd0, typename Opnd1>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1>::Ty m_FMin(const Opnd0 &Op0,
                                                        const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::minnum>(Op0, Op1);
}

template <typename Opnd0, typename Opnd1>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1>::Ty m_FMax(const Opnd0 &Op0,
                                                        const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::maxnum>(Op0, Op1);
}

template <typename Opnd0, typename Opnd1, typename Opnd2>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1, Opnd2>::Ty
m_FShl(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::fshl>(Op0, Op1, Op2);
}

template <typename Opnd0, typename Opnd1, typename Opnd2>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1, Opnd2>::Ty
m_FShr(const Opnd0 &Op0, const Opnd1 &Op1, const Opnd2 &Op2) {
  return m_Intrinsic<Intrinsic::fshr>(Op0, Op1, Op2);
}

template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_Sqrt(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::sqrt>(Op0);
}

template <typename Opnd0, typename Opnd1>
inline typename m_Intrinsic_Ty<Opnd0, Opnd1>::Ty m_CopySign(const Opnd0 &Op0,
                                                            const Opnd1 &Op1) {
  return m_Intrinsic<Intrinsic::copysign>(Op0, Op1);
}

template <typename Opnd0>
inline typename m_Intrinsic_Ty<Opnd0>::Ty m_VecReverse(const Opnd0 &Op0) {
  return m_Intrinsic<Intrinsic::vector_reverse>(Op0);
}

//===----------------------------------------------------------------------===//
// Matchers for two-operands operators with the operators in either order
//

/// Matches a BinaryOperator with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline AnyBinaryOp_match<LHS, RHS, true> m_c_BinOp(const LHS &L, const RHS &R) {
  return AnyBinaryOp_match<LHS, RHS, true>(L, R);
}

/// Matches an ICmp with a predicate over LHS and RHS in either order.
/// Swaps the predicate if operands are commuted.
template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate, true>
m_c_ICmp(ICmpInst::Predicate &Pred, const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate, true>(Pred, L,
                                                                       R);
}

template <typename LHS, typename RHS>
inline CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate, true>
m_c_ICmp(const LHS &L, const RHS &R) {
  return CmpClass_match<LHS, RHS, ICmpInst, ICmpInst::Predicate, true>(L, R);
}

/// Matches a specific opcode with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline SpecificBinaryOp_match<LHS, RHS, true>
m_c_BinOp(unsigned Opcode, const LHS &L, const RHS &R) {
  return SpecificBinaryOp_match<LHS, RHS, true>(Opcode, L, R);
}

/// Matches a Add with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Add, true> m_c_Add(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Add, true>(L, R);
}

/// Matches a Mul with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Mul, true> m_c_Mul(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Mul, true>(L, R);
}

/// Matches an And with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::And, true> m_c_And(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::And, true>(L, R);
}

/// Matches an Or with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Or, true> m_c_Or(const LHS &L,
                                                              const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Or, true>(L, R);
}

/// Matches an Xor with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::Xor, true> m_c_Xor(const LHS &L,
                                                                const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::Xor, true>(L, R);
}

/// Matches a 'Neg' as 'sub 0, V'.
template <typename ValTy>
inline BinaryOp_match<cst_pred_ty<is_zero_int>, ValTy, Instruction::Sub>
m_Neg(const ValTy &V) {
  return m_Sub(m_ZeroInt(), V);
}

/// Matches a 'Neg' as 'sub nsw 0, V'.
template <typename ValTy>
inline OverflowingBinaryOp_match<cst_pred_ty<is_zero_int>, ValTy,
                                 Instruction::Sub,
                                 OverflowingBinaryOperator::NoSignedWrap>
m_NSWNeg(const ValTy &V) {
  return m_NSWSub(m_ZeroInt(), V);
}

/// Matches an SMin with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty, true>
m_c_SMin(const LHS &L, const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty, true>(L, R);
}
/// Matches an SMax with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty, true>
m_c_SMax(const LHS &L, const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty, true>(L, R);
}
/// Matches a UMin with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty, true>
m_c_UMin(const LHS &L, const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty, true>(L, R);
}
/// Matches a UMax with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty, true>
m_c_UMax(const LHS &L, const RHS &R) {
  return MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty, true>(L, R);
}

template <typename LHS, typename RHS>
inline match_combine_or<
    match_combine_or<MaxMin_match<ICmpInst, LHS, RHS, smax_pred_ty, true>,
                     MaxMin_match<ICmpInst, LHS, RHS, smin_pred_ty, true>>,
    match_combine_or<MaxMin_match<ICmpInst, LHS, RHS, umax_pred_ty, true>,
                     MaxMin_match<ICmpInst, LHS, RHS, umin_pred_ty, true>>>
m_c_MaxOrMin(const LHS &L, const RHS &R) {
  return m_CombineOr(m_CombineOr(m_c_SMax(L, R), m_c_SMin(L, R)),
                     m_CombineOr(m_c_UMax(L, R), m_c_UMin(L, R)));
}

template <Intrinsic::ID IntrID, typename T0, typename T1>
inline match_combine_or<typename m_Intrinsic_Ty<T0, T1>::Ty,
                        typename m_Intrinsic_Ty<T1, T0>::Ty>
m_c_Intrinsic(const T0 &Op0, const T1 &Op1) {
  return m_CombineOr(m_Intrinsic<IntrID>(Op0, Op1),
                     m_Intrinsic<IntrID>(Op1, Op0));
}

/// Matches FAdd with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FAdd, true>
m_c_FAdd(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FAdd, true>(L, R);
}

/// Matches FMul with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline BinaryOp_match<LHS, RHS, Instruction::FMul, true>
m_c_FMul(const LHS &L, const RHS &R) {
  return BinaryOp_match<LHS, RHS, Instruction::FMul, true>(L, R);
}

template <typename Opnd_t> struct Signum_match {
  Opnd_t Val;
  Signum_match(const Opnd_t &V) : Val(V) {}

  template <typename OpTy> bool match(OpTy *V) {
    unsigned TypeSize = V->getType()->getScalarSizeInBits();
    if (TypeSize == 0)
      return false;

    unsigned ShiftWidth = TypeSize - 1;
    Value *OpL = nullptr, *OpR = nullptr;

    // This is the representation of signum we match:
    //
    //  signum(x) == (x >> 63) | (-x >>u 63)
    //
    // An i1 value is its own signum, so it's correct to match
    //
    //  signum(x) == (x >> 0)  | (-x >>u 0)
    //
    // for i1 values.

    auto LHS = m_AShr(m_Value(OpL), m_SpecificInt(ShiftWidth));
    auto RHS = m_LShr(m_Neg(m_Value(OpR)), m_SpecificInt(ShiftWidth));
    auto Signum = m_Or(LHS, RHS);

    return Signum.match(V) && OpL == OpR && Val.match(OpL);
  }
};

/// Matches a signum pattern.
///
/// signum(x) =
///      x >  0  ->  1
///      x == 0  ->  0
///      x <  0  -> -1
template <typename Val_t> inline Signum_match<Val_t> m_Signum(const Val_t &V) {
  return Signum_match<Val_t>(V);
}

template <int Ind, typename Opnd_t> struct ExtractValue_match {
  Opnd_t Val;
  ExtractValue_match(const Opnd_t &V) : Val(V) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<ExtractValueInst>(V)) {
      // If Ind is -1, don't inspect indices
      if (Ind != -1 &&
          !(I->getNumIndices() == 1 && I->getIndices()[0] == (unsigned)Ind))
        return false;
      return Val.match(I->getAggregateOperand());
    }
    return false;
  }
};

/// Match a single index ExtractValue instruction.
/// For example m_ExtractValue<1>(...)
template <int Ind, typename Val_t>
inline ExtractValue_match<Ind, Val_t> m_ExtractValue(const Val_t &V) {
  return ExtractValue_match<Ind, Val_t>(V);
}

/// Match an ExtractValue instruction with any index.
/// For example m_ExtractValue(...)
template <typename Val_t>
inline ExtractValue_match<-1, Val_t> m_ExtractValue(const Val_t &V) {
  return ExtractValue_match<-1, Val_t>(V);
}

/// Matcher for a single index InsertValue instruction.
template <int Ind, typename T0, typename T1> struct InsertValue_match {
  T0 Op0;
  T1 Op1;

  InsertValue_match(const T0 &Op0, const T1 &Op1) : Op0(Op0), Op1(Op1) {}

  template <typename OpTy> bool match(OpTy *V) {
    if (auto *I = dyn_cast<InsertValueInst>(V)) {
      return Op0.match(I->getOperand(0)) && Op1.match(I->getOperand(1)) &&
             I->getNumIndices() == 1 && Ind == I->getIndices()[0];
    }
    return false;
  }
};

/// Matches a single index InsertValue instruction.
template <int Ind, typename Val_t, typename Elt_t>
inline InsertValue_match<Ind, Val_t, Elt_t> m_InsertValue(const Val_t &Val,
                                                          const Elt_t &Elt) {
  return InsertValue_match<Ind, Val_t, Elt_t>(Val, Elt);
}

/// Matches patterns for `vscale`. This can either be a call to `llvm.vscale` or
/// the constant expression
///  `ptrtoint(gep <vscale x 1 x i8>, <vscale x 1 x i8>* null, i32 1>`
/// under the right conditions determined by DataLayout.
struct VScaleVal_match {
  template <typename ITy> bool match(ITy *V) {
    if (m_Intrinsic<Intrinsic::vscale>().match(V))
      return true;

    Value *Ptr;
    if (m_PtrToInt(m_Value(Ptr)).match(V)) {
      if (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
        auto *DerefTy =
            dyn_cast<ScalableVectorType>(GEP->getSourceElementType());
        if (GEP->getNumIndices() == 1 && DerefTy &&
            DerefTy->getElementType()->isIntegerTy(8) &&
            m_Zero().match(GEP->getPointerOperand()) &&
            m_SpecificInt(1).match(GEP->idx_begin()->get()))
          return true;
      }
    }

    return false;
  }
};

inline VScaleVal_match m_VScale() {
  return VScaleVal_match();
}

template <typename LHS, typename RHS, unsigned Opcode, bool Commutable = false>
struct LogicalOp_match {
  LHS L;
  RHS R;

  LogicalOp_match(const LHS &L, const RHS &R) : L(L), R(R) {}

  template <typename T> bool match(T *V) {
    auto *I = dyn_cast<Instruction>(V);
    if (!I || !I->getType()->isIntOrIntVectorTy(1))
      return false;

    if (I->getOpcode() == Opcode) {
      auto *Op0 = I->getOperand(0);
      auto *Op1 = I->getOperand(1);
      return (L.match(Op0) && R.match(Op1)) ||
             (Commutable && L.match(Op1) && R.match(Op0));
    }

    if (auto *Select = dyn_cast<SelectInst>(I)) {
      auto *Cond = Select->getCondition();
      auto *TVal = Select->getTrueValue();
      auto *FVal = Select->getFalseValue();

      // Don't match a scalar select of bool vectors.
      // Transforms expect a single type for operands if this matches.
      if (Cond->getType() != Select->getType())
        return false;

      if (Opcode == Instruction::And) {
        auto *C = dyn_cast<Constant>(FVal);
        if (C && C->isNullValue())
          return (L.match(Cond) && R.match(TVal)) ||
                 (Commutable && L.match(TVal) && R.match(Cond));
      } else {
        assert(Opcode == Instruction::Or);
        auto *C = dyn_cast<Constant>(TVal);
        if (C && C->isOneValue())
          return (L.match(Cond) && R.match(FVal)) ||
                 (Commutable && L.match(FVal) && R.match(Cond));
      }
    }

    return false;
  }
};

/// Matches L && R either in the form of L & R or L ? R : false.
/// Note that the latter form is poison-blocking.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::And> m_LogicalAnd(const LHS &L,
                                                                const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::And>(L, R);
}

/// Matches L && R where L and R are arbitrary values.
inline auto m_LogicalAnd() { return m_LogicalAnd(m_Value(), m_Value()); }

/// Matches L && R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::And, true>
m_c_LogicalAnd(const LHS &L, const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::And, true>(L, R);
}

/// Matches L || R either in the form of L | R or L ? true : R.
/// Note that the latter form is poison-blocking.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::Or> m_LogicalOr(const LHS &L,
                                                              const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::Or>(L, R);
}

/// Matches L || R where L and R are arbitrary values.
inline auto m_LogicalOr() { return m_LogicalOr(m_Value(), m_Value()); }

/// Matches L || R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline LogicalOp_match<LHS, RHS, Instruction::Or, true>
m_c_LogicalOr(const LHS &L, const RHS &R) {
  return LogicalOp_match<LHS, RHS, Instruction::Or, true>(L, R);
}

/// Matches either L && R or L || R,
/// either one being in the either binary or logical form.
/// Note that the latter form is poison-blocking.
template <typename LHS, typename RHS, bool Commutable = false>
inline auto m_LogicalOp(const LHS &L, const RHS &R) {
  return m_CombineOr(
      LogicalOp_match<LHS, RHS, Instruction::And, Commutable>(L, R),
      LogicalOp_match<LHS, RHS, Instruction::Or, Commutable>(L, R));
}

/// Matches either L && R or L || R where L and R are arbitrary values.
inline auto m_LogicalOp() { return m_LogicalOp(m_Value(), m_Value()); }

/// Matches either L && R or L || R with LHS and RHS in either order.
template <typename LHS, typename RHS>
inline auto m_c_LogicalOp(const LHS &L, const RHS &R) {
  return m_LogicalOp<LHS, RHS, /*Commutable=*/true>(L, R);
}

} // end namespace PatternMatch
} // end namespace llvm

#endif // LLVM_IR_PATTERNMATCH_H
