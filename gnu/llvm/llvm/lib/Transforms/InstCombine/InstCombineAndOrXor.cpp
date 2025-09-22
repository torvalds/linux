//===- InstCombineAndOrXor.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitAnd, visitOr, and visitXor functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/Analysis/CmpInstAnalysis.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "instcombine"

/// This is the complement of getICmpCode, which turns an opcode and two
/// operands into either a constant true or false, or a brand new ICmp
/// instruction. The sign is passed in to determine which kind of predicate to
/// use in the new icmp instruction.
static Value *getNewICmpValue(unsigned Code, bool Sign, Value *LHS, Value *RHS,
                              InstCombiner::BuilderTy &Builder) {
  ICmpInst::Predicate NewPred;
  if (Constant *TorF = getPredForICmpCode(Code, Sign, LHS->getType(), NewPred))
    return TorF;
  return Builder.CreateICmp(NewPred, LHS, RHS);
}

/// This is the complement of getFCmpCode, which turns an opcode and two
/// operands into either a FCmp instruction, or a true/false constant.
static Value *getFCmpValue(unsigned Code, Value *LHS, Value *RHS,
                           InstCombiner::BuilderTy &Builder) {
  FCmpInst::Predicate NewPred;
  if (Constant *TorF = getPredForFCmpCode(Code, LHS->getType(), NewPred))
    return TorF;
  return Builder.CreateFCmp(NewPred, LHS, RHS);
}

/// Emit a computation of: (V >= Lo && V < Hi) if Inside is true, otherwise
/// (V < Lo || V >= Hi). This method expects that Lo < Hi. IsSigned indicates
/// whether to treat V, Lo, and Hi as signed or not.
Value *InstCombinerImpl::insertRangeTest(Value *V, const APInt &Lo,
                                         const APInt &Hi, bool isSigned,
                                         bool Inside) {
  assert((isSigned ? Lo.slt(Hi) : Lo.ult(Hi)) &&
         "Lo is not < Hi in range emission code!");

  Type *Ty = V->getType();

  // V >= Min && V <  Hi --> V <  Hi
  // V <  Min || V >= Hi --> V >= Hi
  ICmpInst::Predicate Pred = Inside ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_UGE;
  if (isSigned ? Lo.isMinSignedValue() : Lo.isMinValue()) {
    Pred = isSigned ? ICmpInst::getSignedPredicate(Pred) : Pred;
    return Builder.CreateICmp(Pred, V, ConstantInt::get(Ty, Hi));
  }

  // V >= Lo && V <  Hi --> V - Lo u<  Hi - Lo
  // V <  Lo || V >= Hi --> V - Lo u>= Hi - Lo
  Value *VMinusLo =
      Builder.CreateSub(V, ConstantInt::get(Ty, Lo), V->getName() + ".off");
  Constant *HiMinusLo = ConstantInt::get(Ty, Hi - Lo);
  return Builder.CreateICmp(Pred, VMinusLo, HiMinusLo);
}

/// Classify (icmp eq (A & B), C) and (icmp ne (A & B), C) as matching patterns
/// that can be simplified.
/// One of A and B is considered the mask. The other is the value. This is
/// described as the "AMask" or "BMask" part of the enum. If the enum contains
/// only "Mask", then both A and B can be considered masks. If A is the mask,
/// then it was proven that (A & C) == C. This is trivial if C == A or C == 0.
/// If both A and C are constants, this proof is also easy.
/// For the following explanations, we assume that A is the mask.
///
/// "AllOnes" declares that the comparison is true only if (A & B) == A or all
/// bits of A are set in B.
///   Example: (icmp eq (A & 3), 3) -> AMask_AllOnes
///
/// "AllZeros" declares that the comparison is true only if (A & B) == 0 or all
/// bits of A are cleared in B.
///   Example: (icmp eq (A & 3), 0) -> Mask_AllZeroes
///
/// "Mixed" declares that (A & B) == C and C might or might not contain any
/// number of one bits and zero bits.
///   Example: (icmp eq (A & 3), 1) -> AMask_Mixed
///
/// "Not" means that in above descriptions "==" should be replaced by "!=".
///   Example: (icmp ne (A & 3), 3) -> AMask_NotAllOnes
///
/// If the mask A contains a single bit, then the following is equivalent:
///    (icmp eq (A & B), A) equals (icmp ne (A & B), 0)
///    (icmp ne (A & B), A) equals (icmp eq (A & B), 0)
enum MaskedICmpType {
  AMask_AllOnes           =     1,
  AMask_NotAllOnes        =     2,
  BMask_AllOnes           =     4,
  BMask_NotAllOnes        =     8,
  Mask_AllZeros           =    16,
  Mask_NotAllZeros        =    32,
  AMask_Mixed             =    64,
  AMask_NotMixed          =   128,
  BMask_Mixed             =   256,
  BMask_NotMixed          =   512
};

/// Return the set of patterns (from MaskedICmpType) that (icmp SCC (A & B), C)
/// satisfies.
static unsigned getMaskedICmpType(Value *A, Value *B, Value *C,
                                  ICmpInst::Predicate Pred) {
  const APInt *ConstA = nullptr, *ConstB = nullptr, *ConstC = nullptr;
  match(A, m_APInt(ConstA));
  match(B, m_APInt(ConstB));
  match(C, m_APInt(ConstC));
  bool IsEq = (Pred == ICmpInst::ICMP_EQ);
  bool IsAPow2 = ConstA && ConstA->isPowerOf2();
  bool IsBPow2 = ConstB && ConstB->isPowerOf2();
  unsigned MaskVal = 0;
  if (ConstC && ConstC->isZero()) {
    // if C is zero, then both A and B qualify as mask
    MaskVal |= (IsEq ? (Mask_AllZeros | AMask_Mixed | BMask_Mixed)
                     : (Mask_NotAllZeros | AMask_NotMixed | BMask_NotMixed));
    if (IsAPow2)
      MaskVal |= (IsEq ? (AMask_NotAllOnes | AMask_NotMixed)
                       : (AMask_AllOnes | AMask_Mixed));
    if (IsBPow2)
      MaskVal |= (IsEq ? (BMask_NotAllOnes | BMask_NotMixed)
                       : (BMask_AllOnes | BMask_Mixed));
    return MaskVal;
  }

  if (A == C) {
    MaskVal |= (IsEq ? (AMask_AllOnes | AMask_Mixed)
                     : (AMask_NotAllOnes | AMask_NotMixed));
    if (IsAPow2)
      MaskVal |= (IsEq ? (Mask_NotAllZeros | AMask_NotMixed)
                       : (Mask_AllZeros | AMask_Mixed));
  } else if (ConstA && ConstC && ConstC->isSubsetOf(*ConstA)) {
    MaskVal |= (IsEq ? AMask_Mixed : AMask_NotMixed);
  }

  if (B == C) {
    MaskVal |= (IsEq ? (BMask_AllOnes | BMask_Mixed)
                     : (BMask_NotAllOnes | BMask_NotMixed));
    if (IsBPow2)
      MaskVal |= (IsEq ? (Mask_NotAllZeros | BMask_NotMixed)
                       : (Mask_AllZeros | BMask_Mixed));
  } else if (ConstB && ConstC && ConstC->isSubsetOf(*ConstB)) {
    MaskVal |= (IsEq ? BMask_Mixed : BMask_NotMixed);
  }

  return MaskVal;
}

/// Convert an analysis of a masked ICmp into its equivalent if all boolean
/// operations had the opposite sense. Since each "NotXXX" flag (recording !=)
/// is adjacent to the corresponding normal flag (recording ==), this just
/// involves swapping those bits over.
static unsigned conjugateICmpMask(unsigned Mask) {
  unsigned NewMask;
  NewMask = (Mask & (AMask_AllOnes | BMask_AllOnes | Mask_AllZeros |
                     AMask_Mixed | BMask_Mixed))
            << 1;

  NewMask |= (Mask & (AMask_NotAllOnes | BMask_NotAllOnes | Mask_NotAllZeros |
                      AMask_NotMixed | BMask_NotMixed))
             >> 1;

  return NewMask;
}

// Adapts the external decomposeBitTestICmp for local use.
static bool decomposeBitTestICmp(Value *LHS, Value *RHS, CmpInst::Predicate &Pred,
                                 Value *&X, Value *&Y, Value *&Z) {
  APInt Mask;
  if (!llvm::decomposeBitTestICmp(LHS, RHS, Pred, X, Mask))
    return false;

  Y = ConstantInt::get(X->getType(), Mask);
  Z = ConstantInt::get(X->getType(), 0);
  return true;
}

/// Handle (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E).
/// Return the pattern classes (from MaskedICmpType) for the left hand side and
/// the right hand side as a pair.
/// LHS and RHS are the left hand side and the right hand side ICmps and PredL
/// and PredR are their predicates, respectively.
static std::optional<std::pair<unsigned, unsigned>> getMaskedTypeForICmpPair(
    Value *&A, Value *&B, Value *&C, Value *&D, Value *&E, ICmpInst *LHS,
    ICmpInst *RHS, ICmpInst::Predicate &PredL, ICmpInst::Predicate &PredR) {
  // Don't allow pointers. Splat vectors are fine.
  if (!LHS->getOperand(0)->getType()->isIntOrIntVectorTy() ||
      !RHS->getOperand(0)->getType()->isIntOrIntVectorTy())
    return std::nullopt;

  // Here comes the tricky part:
  // LHS might be of the form L11 & L12 == X, X == L21 & L22,
  // and L11 & L12 == L21 & L22. The same goes for RHS.
  // Now we must find those components L** and R**, that are equal, so
  // that we can extract the parameters A, B, C, D, and E for the canonical
  // above.
  Value *L1 = LHS->getOperand(0);
  Value *L2 = LHS->getOperand(1);
  Value *L11, *L12, *L21, *L22;
  // Check whether the icmp can be decomposed into a bit test.
  if (decomposeBitTestICmp(L1, L2, PredL, L11, L12, L2)) {
    L21 = L22 = L1 = nullptr;
  } else {
    // Look for ANDs in the LHS icmp.
    if (!match(L1, m_And(m_Value(L11), m_Value(L12)))) {
      // Any icmp can be viewed as being trivially masked; if it allows us to
      // remove one, it's worth it.
      L11 = L1;
      L12 = Constant::getAllOnesValue(L1->getType());
    }

    if (!match(L2, m_And(m_Value(L21), m_Value(L22)))) {
      L21 = L2;
      L22 = Constant::getAllOnesValue(L2->getType());
    }
  }

  // Bail if LHS was a icmp that can't be decomposed into an equality.
  if (!ICmpInst::isEquality(PredL))
    return std::nullopt;

  Value *R1 = RHS->getOperand(0);
  Value *R2 = RHS->getOperand(1);
  Value *R11, *R12;
  bool Ok = false;
  if (decomposeBitTestICmp(R1, R2, PredR, R11, R12, R2)) {
    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
    } else {
      return std::nullopt;
    }
    E = R2;
    R1 = nullptr;
    Ok = true;
  } else {
    if (!match(R1, m_And(m_Value(R11), m_Value(R12)))) {
      // As before, model no mask as a trivial mask if it'll let us do an
      // optimization.
      R11 = R1;
      R12 = Constant::getAllOnesValue(R1->getType());
    }

    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
      E = R2;
      Ok = true;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
      E = R2;
      Ok = true;
    }
  }

  // Bail if RHS was a icmp that can't be decomposed into an equality.
  if (!ICmpInst::isEquality(PredR))
    return std::nullopt;

  // Look for ANDs on the right side of the RHS icmp.
  if (!Ok) {
    if (!match(R2, m_And(m_Value(R11), m_Value(R12)))) {
      R11 = R2;
      R12 = Constant::getAllOnesValue(R2->getType());
    }

    if (R11 == L11 || R11 == L12 || R11 == L21 || R11 == L22) {
      A = R11;
      D = R12;
      E = R1;
      Ok = true;
    } else if (R12 == L11 || R12 == L12 || R12 == L21 || R12 == L22) {
      A = R12;
      D = R11;
      E = R1;
      Ok = true;
    } else {
      return std::nullopt;
    }

    assert(Ok && "Failed to find AND on the right side of the RHS icmp.");
  }

  if (L11 == A) {
    B = L12;
    C = L2;
  } else if (L12 == A) {
    B = L11;
    C = L2;
  } else if (L21 == A) {
    B = L22;
    C = L1;
  } else if (L22 == A) {
    B = L21;
    C = L1;
  }

  unsigned LeftType = getMaskedICmpType(A, B, C, PredL);
  unsigned RightType = getMaskedICmpType(A, D, E, PredR);
  return std::optional<std::pair<unsigned, unsigned>>(
      std::make_pair(LeftType, RightType));
}

/// Try to fold (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E) into a single
/// (icmp(A & X) ==/!= Y), where the left-hand side is of type Mask_NotAllZeros
/// and the right hand side is of type BMask_Mixed. For example,
/// (icmp (A & 12) != 0) & (icmp (A & 15) == 8) -> (icmp (A & 15) == 8).
/// Also used for logical and/or, must be poison safe.
static Value *foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
    ICmpInst *LHS, ICmpInst *RHS, bool IsAnd, Value *A, Value *B, Value *C,
    Value *D, Value *E, ICmpInst::Predicate PredL, ICmpInst::Predicate PredR,
    InstCombiner::BuilderTy &Builder) {
  // We are given the canonical form:
  //   (icmp ne (A & B), 0) & (icmp eq (A & D), E).
  // where D & E == E.
  //
  // If IsAnd is false, we get it in negated form:
  //   (icmp eq (A & B), 0) | (icmp ne (A & D), E) ->
  //      !((icmp ne (A & B), 0) & (icmp eq (A & D), E)).
  //
  // We currently handle the case of B, C, D, E are constant.
  //
  const APInt *BCst, *CCst, *DCst, *OrigECst;
  if (!match(B, m_APInt(BCst)) || !match(C, m_APInt(CCst)) ||
      !match(D, m_APInt(DCst)) || !match(E, m_APInt(OrigECst)))
    return nullptr;

  ICmpInst::Predicate NewCC = IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;

  // Update E to the canonical form when D is a power of two and RHS is
  // canonicalized as,
  // (icmp ne (A & D), 0) -> (icmp eq (A & D), D) or
  // (icmp ne (A & D), D) -> (icmp eq (A & D), 0).
  APInt ECst = *OrigECst;
  if (PredR != NewCC)
    ECst ^= *DCst;

  // If B or D is zero, skip because if LHS or RHS can be trivially folded by
  // other folding rules and this pattern won't apply any more.
  if (*BCst == 0 || *DCst == 0)
    return nullptr;

  // If B and D don't intersect, ie. (B & D) == 0, no folding because we can't
  // deduce anything from it.
  // For example,
  // (icmp ne (A & 12), 0) & (icmp eq (A & 3), 1) -> no folding.
  if ((*BCst & *DCst) == 0)
    return nullptr;

  // If the following two conditions are met:
  //
  // 1. mask B covers only a single bit that's not covered by mask D, that is,
  // (B & (B ^ D)) is a power of 2 (in other words, B minus the intersection of
  // B and D has only one bit set) and,
  //
  // 2. RHS (and E) indicates that the rest of B's bits are zero (in other
  // words, the intersection of B and D is zero), that is, ((B & D) & E) == 0
  //
  // then that single bit in B must be one and thus the whole expression can be
  // folded to
  //   (A & (B | D)) == (B & (B ^ D)) | E.
  //
  // For example,
  // (icmp ne (A & 12), 0) & (icmp eq (A & 7), 1) -> (icmp eq (A & 15), 9)
  // (icmp ne (A & 15), 0) & (icmp eq (A & 7), 0) -> (icmp eq (A & 15), 8)
  if ((((*BCst & *DCst) & ECst) == 0) &&
      (*BCst & (*BCst ^ *DCst)).isPowerOf2()) {
    APInt BorD = *BCst | *DCst;
    APInt BandBxorDorE = (*BCst & (*BCst ^ *DCst)) | ECst;
    Value *NewMask = ConstantInt::get(A->getType(), BorD);
    Value *NewMaskedValue = ConstantInt::get(A->getType(), BandBxorDorE);
    Value *NewAnd = Builder.CreateAnd(A, NewMask);
    return Builder.CreateICmp(NewCC, NewAnd, NewMaskedValue);
  }

  auto IsSubSetOrEqual = [](const APInt *C1, const APInt *C2) {
    return (*C1 & *C2) == *C1;
  };
  auto IsSuperSetOrEqual = [](const APInt *C1, const APInt *C2) {
    return (*C1 & *C2) == *C2;
  };

  // In the following, we consider only the cases where B is a superset of D, B
  // is a subset of D, or B == D because otherwise there's at least one bit
  // covered by B but not D, in which case we can't deduce much from it, so
  // no folding (aside from the single must-be-one bit case right above.)
  // For example,
  // (icmp ne (A & 14), 0) & (icmp eq (A & 3), 1) -> no folding.
  if (!IsSubSetOrEqual(BCst, DCst) && !IsSuperSetOrEqual(BCst, DCst))
    return nullptr;

  // At this point, either B is a superset of D, B is a subset of D or B == D.

  // If E is zero, if B is a subset of (or equal to) D, LHS and RHS contradict
  // and the whole expression becomes false (or true if negated), otherwise, no
  // folding.
  // For example,
  // (icmp ne (A & 3), 0) & (icmp eq (A & 7), 0) -> false.
  // (icmp ne (A & 15), 0) & (icmp eq (A & 3), 0) -> no folding.
  if (ECst.isZero()) {
    if (IsSubSetOrEqual(BCst, DCst))
      return ConstantInt::get(LHS->getType(), !IsAnd);
    return nullptr;
  }

  // At this point, B, D, E aren't zero and (B & D) == B, (B & D) == D or B ==
  // D. If B is a superset of (or equal to) D, since E is not zero, LHS is
  // subsumed by RHS (RHS implies LHS.) So the whole expression becomes
  // RHS. For example,
  // (icmp ne (A & 255), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  // (icmp ne (A & 15), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  if (IsSuperSetOrEqual(BCst, DCst))
    return RHS;
  // Otherwise, B is a subset of D. If B and E have a common bit set,
  // ie. (B & E) != 0, then LHS is subsumed by RHS. For example.
  // (icmp ne (A & 12), 0) & (icmp eq (A & 15), 8) -> (icmp eq (A & 15), 8).
  assert(IsSubSetOrEqual(BCst, DCst) && "Precondition due to above code");
  if ((*BCst & ECst) != 0)
    return RHS;
  // Otherwise, LHS and RHS contradict and the whole expression becomes false
  // (or true if negated.) For example,
  // (icmp ne (A & 7), 0) & (icmp eq (A & 15), 8) -> false.
  // (icmp ne (A & 6), 0) & (icmp eq (A & 15), 8) -> false.
  return ConstantInt::get(LHS->getType(), !IsAnd);
}

/// Try to fold (icmp(A & B) ==/!= 0) &/| (icmp(A & D) ==/!= E) into a single
/// (icmp(A & X) ==/!= Y), where the left-hand side and the right hand side
/// aren't of the common mask pattern type.
/// Also used for logical and/or, must be poison safe.
static Value *foldLogOpOfMaskedICmpsAsymmetric(
    ICmpInst *LHS, ICmpInst *RHS, bool IsAnd, Value *A, Value *B, Value *C,
    Value *D, Value *E, ICmpInst::Predicate PredL, ICmpInst::Predicate PredR,
    unsigned LHSMask, unsigned RHSMask, InstCombiner::BuilderTy &Builder) {
  assert(ICmpInst::isEquality(PredL) && ICmpInst::isEquality(PredR) &&
         "Expected equality predicates for masked type of icmps.");
  // Handle Mask_NotAllZeros-BMask_Mixed cases.
  // (icmp ne/eq (A & B), C) &/| (icmp eq/ne (A & D), E), or
  // (icmp eq/ne (A & B), C) &/| (icmp ne/eq (A & D), E)
  //    which gets swapped to
  //    (icmp ne/eq (A & D), E) &/| (icmp eq/ne (A & B), C).
  if (!IsAnd) {
    LHSMask = conjugateICmpMask(LHSMask);
    RHSMask = conjugateICmpMask(RHSMask);
  }
  if ((LHSMask & Mask_NotAllZeros) && (RHSMask & BMask_Mixed)) {
    if (Value *V = foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
            LHS, RHS, IsAnd, A, B, C, D, E,
            PredL, PredR, Builder)) {
      return V;
    }
  } else if ((LHSMask & BMask_Mixed) && (RHSMask & Mask_NotAllZeros)) {
    if (Value *V = foldLogOpOfMaskedICmps_NotAllZeros_BMask_Mixed(
            RHS, LHS, IsAnd, A, D, E, B, C,
            PredR, PredL, Builder)) {
      return V;
    }
  }
  return nullptr;
}

/// Try to fold (icmp(A & B) ==/!= C) &/| (icmp(A & D) ==/!= E)
/// into a single (icmp(A & X) ==/!= Y).
static Value *foldLogOpOfMaskedICmps(ICmpInst *LHS, ICmpInst *RHS, bool IsAnd,
                                     bool IsLogical,
                                     InstCombiner::BuilderTy &Builder) {
  Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr, *E = nullptr;
  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  std::optional<std::pair<unsigned, unsigned>> MaskPair =
      getMaskedTypeForICmpPair(A, B, C, D, E, LHS, RHS, PredL, PredR);
  if (!MaskPair)
    return nullptr;
  assert(ICmpInst::isEquality(PredL) && ICmpInst::isEquality(PredR) &&
         "Expected equality predicates for masked type of icmps.");
  unsigned LHSMask = MaskPair->first;
  unsigned RHSMask = MaskPair->second;
  unsigned Mask = LHSMask & RHSMask;
  if (Mask == 0) {
    // Even if the two sides don't share a common pattern, check if folding can
    // still happen.
    if (Value *V = foldLogOpOfMaskedICmpsAsymmetric(
            LHS, RHS, IsAnd, A, B, C, D, E, PredL, PredR, LHSMask, RHSMask,
            Builder))
      return V;
    return nullptr;
  }

  // In full generality:
  //     (icmp (A & B) Op C) | (icmp (A & D) Op E)
  // ==  ![ (icmp (A & B) !Op C) & (icmp (A & D) !Op E) ]
  //
  // If the latter can be converted into (icmp (A & X) Op Y) then the former is
  // equivalent to (icmp (A & X) !Op Y).
  //
  // Therefore, we can pretend for the rest of this function that we're dealing
  // with the conjunction, provided we flip the sense of any comparisons (both
  // input and output).

  // In most cases we're going to produce an EQ for the "&&" case.
  ICmpInst::Predicate NewCC = IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE;
  if (!IsAnd) {
    // Convert the masking analysis into its equivalent with negated
    // comparisons.
    Mask = conjugateICmpMask(Mask);
  }

  if (Mask & Mask_AllZeros) {
    // (icmp eq (A & B), 0) & (icmp eq (A & D), 0)
    // -> (icmp eq (A & (B|D)), 0)
    if (IsLogical && !isGuaranteedNotToBeUndefOrPoison(D))
      return nullptr; // TODO: Use freeze?
    Value *NewOr = Builder.CreateOr(B, D);
    Value *NewAnd = Builder.CreateAnd(A, NewOr);
    // We can't use C as zero because we might actually handle
    //   (icmp ne (A & B), B) & (icmp ne (A & D), D)
    // with B and D, having a single bit set.
    Value *Zero = Constant::getNullValue(A->getType());
    return Builder.CreateICmp(NewCC, NewAnd, Zero);
  }
  if (Mask & BMask_AllOnes) {
    // (icmp eq (A & B), B) & (icmp eq (A & D), D)
    // -> (icmp eq (A & (B|D)), (B|D))
    if (IsLogical && !isGuaranteedNotToBeUndefOrPoison(D))
      return nullptr; // TODO: Use freeze?
    Value *NewOr = Builder.CreateOr(B, D);
    Value *NewAnd = Builder.CreateAnd(A, NewOr);
    return Builder.CreateICmp(NewCC, NewAnd, NewOr);
  }
  if (Mask & AMask_AllOnes) {
    // (icmp eq (A & B), A) & (icmp eq (A & D), A)
    // -> (icmp eq (A & (B&D)), A)
    if (IsLogical && !isGuaranteedNotToBeUndefOrPoison(D))
      return nullptr; // TODO: Use freeze?
    Value *NewAnd1 = Builder.CreateAnd(B, D);
    Value *NewAnd2 = Builder.CreateAnd(A, NewAnd1);
    return Builder.CreateICmp(NewCC, NewAnd2, A);
  }

  // Remaining cases assume at least that B and D are constant, and depend on
  // their actual values. This isn't strictly necessary, just a "handle the
  // easy cases for now" decision.
  const APInt *ConstB, *ConstD;
  if (!match(B, m_APInt(ConstB)) || !match(D, m_APInt(ConstD)))
    return nullptr;

  if (Mask & (Mask_NotAllZeros | BMask_NotAllOnes)) {
    // (icmp ne (A & B), 0) & (icmp ne (A & D), 0) and
    // (icmp ne (A & B), B) & (icmp ne (A & D), D)
    //     -> (icmp ne (A & B), 0) or (icmp ne (A & D), 0)
    // Only valid if one of the masks is a superset of the other (check "B&D" is
    // the same as either B or D).
    APInt NewMask = *ConstB & *ConstD;
    if (NewMask == *ConstB)
      return LHS;
    else if (NewMask == *ConstD)
      return RHS;
  }

  if (Mask & AMask_NotAllOnes) {
    // (icmp ne (A & B), B) & (icmp ne (A & D), D)
    //     -> (icmp ne (A & B), A) or (icmp ne (A & D), A)
    // Only valid if one of the masks is a superset of the other (check "B|D" is
    // the same as either B or D).
    APInt NewMask = *ConstB | *ConstD;
    if (NewMask == *ConstB)
      return LHS;
    else if (NewMask == *ConstD)
      return RHS;
  }

  if (Mask & (BMask_Mixed | BMask_NotMixed)) {
    // Mixed:
    // (icmp eq (A & B), C) & (icmp eq (A & D), E)
    // We already know that B & C == C && D & E == E.
    // If we can prove that (B & D) & (C ^ E) == 0, that is, the bits of
    // C and E, which are shared by both the mask B and the mask D, don't
    // contradict, then we can transform to
    // -> (icmp eq (A & (B|D)), (C|E))
    // Currently, we only handle the case of B, C, D, and E being constant.
    // We can't simply use C and E because we might actually handle
    //   (icmp ne (A & B), B) & (icmp eq (A & D), D)
    // with B and D, having a single bit set.

    // NotMixed:
    // (icmp ne (A & B), C) & (icmp ne (A & D), E)
    // -> (icmp ne (A & (B & D)), (C & E))
    // Check the intersection (B & D) for inequality.
    // Assume that (B & D) == B || (B & D) == D, i.e B/D is a subset of D/B
    // and (B & D) & (C ^ E) == 0, bits of C and E, which are shared by both the
    // B and the D, don't contradict.
    // Note that we can assume (~B & C) == 0 && (~D & E) == 0, previous
    // operation should delete these icmps if it hadn't been met.

    const APInt *OldConstC, *OldConstE;
    if (!match(C, m_APInt(OldConstC)) || !match(E, m_APInt(OldConstE)))
      return nullptr;

    auto FoldBMixed = [&](ICmpInst::Predicate CC, bool IsNot) -> Value * {
      CC = IsNot ? CmpInst::getInversePredicate(CC) : CC;
      const APInt ConstC = PredL != CC ? *ConstB ^ *OldConstC : *OldConstC;
      const APInt ConstE = PredR != CC ? *ConstD ^ *OldConstE : *OldConstE;

      if (((*ConstB & *ConstD) & (ConstC ^ ConstE)).getBoolValue())
        return IsNot ? nullptr : ConstantInt::get(LHS->getType(), !IsAnd);

      if (IsNot && !ConstB->isSubsetOf(*ConstD) && !ConstD->isSubsetOf(*ConstB))
        return nullptr;

      APInt BD, CE;
      if (IsNot) {
        BD = *ConstB & *ConstD;
        CE = ConstC & ConstE;
      } else {
        BD = *ConstB | *ConstD;
        CE = ConstC | ConstE;
      }
      Value *NewAnd = Builder.CreateAnd(A, BD);
      Value *CEVal = ConstantInt::get(A->getType(), CE);
      return Builder.CreateICmp(CC, CEVal, NewAnd);
    };

    if (Mask & BMask_Mixed)
      return FoldBMixed(NewCC, false);
    if (Mask & BMask_NotMixed) // can be else also
      return FoldBMixed(NewCC, true);
  }
  return nullptr;
}

/// Try to fold a signed range checked with lower bound 0 to an unsigned icmp.
/// Example: (icmp sge x, 0) & (icmp slt x, n) --> icmp ult x, n
/// If \p Inverted is true then the check is for the inverted range, e.g.
/// (icmp slt x, 0) | (icmp sgt x, n) --> icmp ugt x, n
Value *InstCombinerImpl::simplifyRangeCheck(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                            bool Inverted) {
  // Check the lower range comparison, e.g. x >= 0
  // InstCombine already ensured that if there is a constant it's on the RHS.
  ConstantInt *RangeStart = dyn_cast<ConstantInt>(Cmp0->getOperand(1));
  if (!RangeStart)
    return nullptr;

  ICmpInst::Predicate Pred0 = (Inverted ? Cmp0->getInversePredicate() :
                               Cmp0->getPredicate());

  // Accept x > -1 or x >= 0 (after potentially inverting the predicate).
  if (!((Pred0 == ICmpInst::ICMP_SGT && RangeStart->isMinusOne()) ||
        (Pred0 == ICmpInst::ICMP_SGE && RangeStart->isZero())))
    return nullptr;

  ICmpInst::Predicate Pred1 = (Inverted ? Cmp1->getInversePredicate() :
                               Cmp1->getPredicate());

  Value *Input = Cmp0->getOperand(0);
  Value *RangeEnd;
  if (Cmp1->getOperand(0) == Input) {
    // For the upper range compare we have: icmp x, n
    RangeEnd = Cmp1->getOperand(1);
  } else if (Cmp1->getOperand(1) == Input) {
    // For the upper range compare we have: icmp n, x
    RangeEnd = Cmp1->getOperand(0);
    Pred1 = ICmpInst::getSwappedPredicate(Pred1);
  } else {
    return nullptr;
  }

  // Check the upper range comparison, e.g. x < n
  ICmpInst::Predicate NewPred;
  switch (Pred1) {
    case ICmpInst::ICMP_SLT: NewPred = ICmpInst::ICMP_ULT; break;
    case ICmpInst::ICMP_SLE: NewPred = ICmpInst::ICMP_ULE; break;
    default: return nullptr;
  }

  // This simplification is only valid if the upper range is not negative.
  KnownBits Known = computeKnownBits(RangeEnd, /*Depth=*/0, Cmp1);
  if (!Known.isNonNegative())
    return nullptr;

  if (Inverted)
    NewPred = ICmpInst::getInversePredicate(NewPred);

  return Builder.CreateICmp(NewPred, Input, RangeEnd);
}

// (or (icmp eq X, 0), (icmp eq X, Pow2OrZero))
//      -> (icmp eq (and X, Pow2OrZero), X)
// (and (icmp ne X, 0), (icmp ne X, Pow2OrZero))
//      -> (icmp ne (and X, Pow2OrZero), X)
static Value *
foldAndOrOfICmpsWithPow2AndWithZero(InstCombiner::BuilderTy &Builder,
                                    ICmpInst *LHS, ICmpInst *RHS, bool IsAnd,
                                    const SimplifyQuery &Q) {
  CmpInst::Predicate Pred = IsAnd ? CmpInst::ICMP_NE : CmpInst::ICMP_EQ;
  // Make sure we have right compares for our op.
  if (LHS->getPredicate() != Pred || RHS->getPredicate() != Pred)
    return nullptr;

  // Make it so we can match LHS against the (icmp eq/ne X, 0) just for
  // simplicity.
  if (match(RHS->getOperand(1), m_Zero()))
    std::swap(LHS, RHS);

  Value *Pow2, *Op;
  // Match the desired pattern:
  // LHS: (icmp eq/ne X, 0)
  // RHS: (icmp eq/ne X, Pow2OrZero)
  // Skip if Pow2OrZero is 1. Either way it gets folded to (icmp ugt X, 1) but
  // this form ends up slightly less canonical.
  // We could potentially be more sophisticated than requiring LHS/RHS
  // be one-use. We don't create additional instructions if only one
  // of them is one-use. So cases where one is one-use and the other
  // is two-use might be profitable.
  if (!match(LHS, m_OneUse(m_ICmp(Pred, m_Value(Op), m_Zero()))) ||
      !match(RHS, m_OneUse(m_c_ICmp(Pred, m_Specific(Op), m_Value(Pow2)))) ||
      match(Pow2, m_One()) ||
      !isKnownToBeAPowerOfTwo(Pow2, Q.DL, /*OrZero=*/true, /*Depth=*/0, Q.AC,
                              Q.CxtI, Q.DT))
    return nullptr;

  Value *And = Builder.CreateAnd(Op, Pow2);
  return Builder.CreateICmp(Pred, And, Op);
}

// Fold (iszero(A & K1) | iszero(A & K2)) -> (A & (K1 | K2)) != (K1 | K2)
// Fold (!iszero(A & K1) & !iszero(A & K2)) -> (A & (K1 | K2)) == (K1 | K2)
Value *InstCombinerImpl::foldAndOrOfICmpsOfAndWithPow2(ICmpInst *LHS,
                                                       ICmpInst *RHS,
                                                       Instruction *CxtI,
                                                       bool IsAnd,
                                                       bool IsLogical) {
  CmpInst::Predicate Pred = IsAnd ? CmpInst::ICMP_NE : CmpInst::ICMP_EQ;
  if (LHS->getPredicate() != Pred || RHS->getPredicate() != Pred)
    return nullptr;

  if (!match(LHS->getOperand(1), m_Zero()) ||
      !match(RHS->getOperand(1), m_Zero()))
    return nullptr;

  Value *L1, *L2, *R1, *R2;
  if (match(LHS->getOperand(0), m_And(m_Value(L1), m_Value(L2))) &&
      match(RHS->getOperand(0), m_And(m_Value(R1), m_Value(R2)))) {
    if (L1 == R2 || L2 == R2)
      std::swap(R1, R2);
    if (L2 == R1)
      std::swap(L1, L2);

    if (L1 == R1 &&
        isKnownToBeAPowerOfTwo(L2, false, 0, CxtI) &&
        isKnownToBeAPowerOfTwo(R2, false, 0, CxtI)) {
      // If this is a logical and/or, then we must prevent propagation of a
      // poison value from the RHS by inserting freeze.
      if (IsLogical)
        R2 = Builder.CreateFreeze(R2);
      Value *Mask = Builder.CreateOr(L2, R2);
      Value *Masked = Builder.CreateAnd(L1, Mask);
      auto NewPred = IsAnd ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE;
      return Builder.CreateICmp(NewPred, Masked, Mask);
    }
  }

  return nullptr;
}

/// General pattern:
///   X & Y
///
/// Where Y is checking that all the high bits (covered by a mask 4294967168)
/// are uniform, i.e.  %arg & 4294967168  can be either  4294967168  or  0
/// Pattern can be one of:
///   %t = add        i32 %arg,    128
///   %r = icmp   ult i32 %t,      256
/// Or
///   %t0 = shl       i32 %arg,    24
///   %t1 = ashr      i32 %t0,     24
///   %r  = icmp  eq  i32 %t1,     %arg
/// Or
///   %t0 = trunc     i32 %arg  to i8
///   %t1 = sext      i8  %t0   to i32
///   %r  = icmp  eq  i32 %t1,     %arg
/// This pattern is a signed truncation check.
///
/// And X is checking that some bit in that same mask is zero.
/// I.e. can be one of:
///   %r = icmp sgt i32   %arg,    -1
/// Or
///   %t = and      i32   %arg,    2147483648
///   %r = icmp eq  i32   %t,      0
///
/// Since we are checking that all the bits in that mask are the same,
/// and a particular bit is zero, what we are really checking is that all the
/// masked bits are zero.
/// So this should be transformed to:
///   %r = icmp ult i32 %arg, 128
static Value *foldSignedTruncationCheck(ICmpInst *ICmp0, ICmpInst *ICmp1,
                                        Instruction &CxtI,
                                        InstCombiner::BuilderTy &Builder) {
  assert(CxtI.getOpcode() == Instruction::And);

  // Match  icmp ult (add %arg, C01), C1   (C1 == C01 << 1; powers of two)
  auto tryToMatchSignedTruncationCheck = [](ICmpInst *ICmp, Value *&X,
                                            APInt &SignBitMask) -> bool {
    CmpInst::Predicate Pred;
    const APInt *I01, *I1; // powers of two; I1 == I01 << 1
    if (!(match(ICmp,
                m_ICmp(Pred, m_Add(m_Value(X), m_Power2(I01)), m_Power2(I1))) &&
          Pred == ICmpInst::ICMP_ULT && I1->ugt(*I01) && I01->shl(1) == *I1))
      return false;
    // Which bit is the new sign bit as per the 'signed truncation' pattern?
    SignBitMask = *I01;
    return true;
  };

  // One icmp needs to be 'signed truncation check'.
  // We need to match this first, else we will mismatch commutative cases.
  Value *X1;
  APInt HighestBit;
  ICmpInst *OtherICmp;
  if (tryToMatchSignedTruncationCheck(ICmp1, X1, HighestBit))
    OtherICmp = ICmp0;
  else if (tryToMatchSignedTruncationCheck(ICmp0, X1, HighestBit))
    OtherICmp = ICmp1;
  else
    return nullptr;

  assert(HighestBit.isPowerOf2() && "expected to be power of two (non-zero)");

  // Try to match/decompose into:  icmp eq (X & Mask), 0
  auto tryToDecompose = [](ICmpInst *ICmp, Value *&X,
                           APInt &UnsetBitsMask) -> bool {
    CmpInst::Predicate Pred = ICmp->getPredicate();
    // Can it be decomposed into  icmp eq (X & Mask), 0  ?
    if (llvm::decomposeBitTestICmp(ICmp->getOperand(0), ICmp->getOperand(1),
                                   Pred, X, UnsetBitsMask,
                                   /*LookThroughTrunc=*/false) &&
        Pred == ICmpInst::ICMP_EQ)
      return true;
    // Is it  icmp eq (X & Mask), 0  already?
    const APInt *Mask;
    if (match(ICmp, m_ICmp(Pred, m_And(m_Value(X), m_APInt(Mask)), m_Zero())) &&
        Pred == ICmpInst::ICMP_EQ) {
      UnsetBitsMask = *Mask;
      return true;
    }
    return false;
  };

  // And the other icmp needs to be decomposable into a bit test.
  Value *X0;
  APInt UnsetBitsMask;
  if (!tryToDecompose(OtherICmp, X0, UnsetBitsMask))
    return nullptr;

  assert(!UnsetBitsMask.isZero() && "empty mask makes no sense.");

  // Are they working on the same value?
  Value *X;
  if (X1 == X0) {
    // Ok as is.
    X = X1;
  } else if (match(X0, m_Trunc(m_Specific(X1)))) {
    UnsetBitsMask = UnsetBitsMask.zext(X1->getType()->getScalarSizeInBits());
    X = X1;
  } else
    return nullptr;

  // So which bits should be uniform as per the 'signed truncation check'?
  // (all the bits starting with (i.e. including) HighestBit)
  APInt SignBitsMask = ~(HighestBit - 1U);

  // UnsetBitsMask must have some common bits with SignBitsMask,
  if (!UnsetBitsMask.intersects(SignBitsMask))
    return nullptr;

  // Does UnsetBitsMask contain any bits outside of SignBitsMask?
  if (!UnsetBitsMask.isSubsetOf(SignBitsMask)) {
    APInt OtherHighestBit = (~UnsetBitsMask) + 1U;
    if (!OtherHighestBit.isPowerOf2())
      return nullptr;
    HighestBit = APIntOps::umin(HighestBit, OtherHighestBit);
  }
  // Else, if it does not, then all is ok as-is.

  // %r = icmp ult %X, SignBit
  return Builder.CreateICmpULT(X, ConstantInt::get(X->getType(), HighestBit),
                               CxtI.getName() + ".simplified");
}

/// Fold (icmp eq ctpop(X) 1) | (icmp eq X 0) into (icmp ult ctpop(X) 2) and
/// fold (icmp ne ctpop(X) 1) & (icmp ne X 0) into (icmp ugt ctpop(X) 1).
/// Also used for logical and/or, must be poison safe.
static Value *foldIsPowerOf2OrZero(ICmpInst *Cmp0, ICmpInst *Cmp1, bool IsAnd,
                                   InstCombiner::BuilderTy &Builder) {
  CmpInst::Predicate Pred0, Pred1;
  Value *X;
  if (!match(Cmp0, m_ICmp(Pred0, m_Intrinsic<Intrinsic::ctpop>(m_Value(X)),
                          m_SpecificInt(1))) ||
      !match(Cmp1, m_ICmp(Pred1, m_Specific(X), m_ZeroInt())))
    return nullptr;

  Value *CtPop = Cmp0->getOperand(0);
  if (IsAnd && Pred0 == ICmpInst::ICMP_NE && Pred1 == ICmpInst::ICMP_NE)
    return Builder.CreateICmpUGT(CtPop, ConstantInt::get(CtPop->getType(), 1));
  if (!IsAnd && Pred0 == ICmpInst::ICMP_EQ && Pred1 == ICmpInst::ICMP_EQ)
    return Builder.CreateICmpULT(CtPop, ConstantInt::get(CtPop->getType(), 2));

  return nullptr;
}

/// Reduce a pair of compares that check if a value has exactly 1 bit set.
/// Also used for logical and/or, must be poison safe if range attributes are
/// dropped.
static Value *foldIsPowerOf2(ICmpInst *Cmp0, ICmpInst *Cmp1, bool JoinedByAnd,
                             InstCombiner::BuilderTy &Builder,
                             InstCombinerImpl &IC) {
  // Handle 'and' / 'or' commutation: make the equality check the first operand.
  if (JoinedByAnd && Cmp1->getPredicate() == ICmpInst::ICMP_NE)
    std::swap(Cmp0, Cmp1);
  else if (!JoinedByAnd && Cmp1->getPredicate() == ICmpInst::ICMP_EQ)
    std::swap(Cmp0, Cmp1);

  // (X != 0) && (ctpop(X) u< 2) --> ctpop(X) == 1
  CmpInst::Predicate Pred0, Pred1;
  Value *X;
  if (JoinedByAnd && match(Cmp0, m_ICmp(Pred0, m_Value(X), m_ZeroInt())) &&
      match(Cmp1, m_ICmp(Pred1, m_Intrinsic<Intrinsic::ctpop>(m_Specific(X)),
                         m_SpecificInt(2))) &&
      Pred0 == ICmpInst::ICMP_NE && Pred1 == ICmpInst::ICMP_ULT) {
    auto *CtPop = cast<Instruction>(Cmp1->getOperand(0));
    // Drop range attributes and re-infer them in the next iteration.
    CtPop->dropPoisonGeneratingAnnotations();
    IC.addToWorklist(CtPop);
    return Builder.CreateICmpEQ(CtPop, ConstantInt::get(CtPop->getType(), 1));
  }
  // (X == 0) || (ctpop(X) u> 1) --> ctpop(X) != 1
  if (!JoinedByAnd && match(Cmp0, m_ICmp(Pred0, m_Value(X), m_ZeroInt())) &&
      match(Cmp1, m_ICmp(Pred1, m_Intrinsic<Intrinsic::ctpop>(m_Specific(X)),
                         m_SpecificInt(1))) &&
      Pred0 == ICmpInst::ICMP_EQ && Pred1 == ICmpInst::ICMP_UGT) {
    auto *CtPop = cast<Instruction>(Cmp1->getOperand(0));
    // Drop range attributes and re-infer them in the next iteration.
    CtPop->dropPoisonGeneratingAnnotations();
    IC.addToWorklist(CtPop);
    return Builder.CreateICmpNE(CtPop, ConstantInt::get(CtPop->getType(), 1));
  }
  return nullptr;
}

/// Try to fold (icmp(A & B) == 0) & (icmp(A & D) != E) into (icmp A u< D) iff
/// B is a contiguous set of ones starting from the most significant bit
/// (negative power of 2), D and E are equal, and D is a contiguous set of ones
/// starting at the most significant zero bit in B. Parameter B supports masking
/// using undef/poison in either scalar or vector values.
static Value *foldNegativePower2AndShiftedMask(
    Value *A, Value *B, Value *D, Value *E, ICmpInst::Predicate PredL,
    ICmpInst::Predicate PredR, InstCombiner::BuilderTy &Builder) {
  assert(ICmpInst::isEquality(PredL) && ICmpInst::isEquality(PredR) &&
         "Expected equality predicates for masked type of icmps.");
  if (PredL != ICmpInst::ICMP_EQ || PredR != ICmpInst::ICMP_NE)
    return nullptr;

  if (!match(B, m_NegatedPower2()) || !match(D, m_ShiftedMask()) ||
      !match(E, m_ShiftedMask()))
    return nullptr;

  // Test scalar arguments for conversion. B has been validated earlier to be a
  // negative power of two and thus is guaranteed to have one or more contiguous
  // ones starting from the MSB followed by zero or more contiguous zeros. D has
  // been validated earlier to be a shifted set of one or more contiguous ones.
  // In order to match, B leading ones and D leading zeros should be equal. The
  // predicate that B be a negative power of 2 prevents the condition of there
  // ever being zero leading ones. Thus 0 == 0 cannot occur. The predicate that
  // D always be a shifted mask prevents the condition of D equaling 0. This
  // prevents matching the condition where B contains the maximum number of
  // leading one bits (-1) and D contains the maximum number of leading zero
  // bits (0).
  auto isReducible = [](const Value *B, const Value *D, const Value *E) {
    const APInt *BCst, *DCst, *ECst;
    return match(B, m_APIntAllowPoison(BCst)) && match(D, m_APInt(DCst)) &&
           match(E, m_APInt(ECst)) && *DCst == *ECst &&
           (isa<PoisonValue>(B) ||
            (BCst->countLeadingOnes() == DCst->countLeadingZeros()));
  };

  // Test vector type arguments for conversion.
  if (const auto *BVTy = dyn_cast<VectorType>(B->getType())) {
    const auto *BFVTy = dyn_cast<FixedVectorType>(BVTy);
    const auto *BConst = dyn_cast<Constant>(B);
    const auto *DConst = dyn_cast<Constant>(D);
    const auto *EConst = dyn_cast<Constant>(E);

    if (!BFVTy || !BConst || !DConst || !EConst)
      return nullptr;

    for (unsigned I = 0; I != BFVTy->getNumElements(); ++I) {
      const auto *BElt = BConst->getAggregateElement(I);
      const auto *DElt = DConst->getAggregateElement(I);
      const auto *EElt = EConst->getAggregateElement(I);

      if (!BElt || !DElt || !EElt)
        return nullptr;
      if (!isReducible(BElt, DElt, EElt))
        return nullptr;
    }
  } else {
    // Test scalar type arguments for conversion.
    if (!isReducible(B, D, E))
      return nullptr;
  }
  return Builder.CreateICmp(ICmpInst::ICMP_ULT, A, D);
}

/// Try to fold ((icmp X u< P) & (icmp(X & M) != M)) or ((icmp X s> -1) &
/// (icmp(X & M) != M)) into (icmp X u< M). Where P is a power of 2, M < P, and
/// M is a contiguous shifted mask starting at the right most significant zero
/// bit in P. SGT is supported as when P is the largest representable power of
/// 2, an earlier optimization converts the expression into (icmp X s> -1).
/// Parameter P supports masking using undef/poison in either scalar or vector
/// values.
static Value *foldPowerOf2AndShiftedMask(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                         bool JoinedByAnd,
                                         InstCombiner::BuilderTy &Builder) {
  if (!JoinedByAnd)
    return nullptr;
  Value *A = nullptr, *B = nullptr, *C = nullptr, *D = nullptr, *E = nullptr;
  ICmpInst::Predicate CmpPred0 = Cmp0->getPredicate(),
                      CmpPred1 = Cmp1->getPredicate();
  // Assuming P is a 2^n, getMaskedTypeForICmpPair will normalize (icmp X u<
  // 2^n) into (icmp (X & ~(2^n-1)) == 0) and (icmp X s> -1) into (icmp (X &
  // SignMask) == 0).
  std::optional<std::pair<unsigned, unsigned>> MaskPair =
      getMaskedTypeForICmpPair(A, B, C, D, E, Cmp0, Cmp1, CmpPred0, CmpPred1);
  if (!MaskPair)
    return nullptr;

  const auto compareBMask = BMask_NotMixed | BMask_NotAllOnes;
  unsigned CmpMask0 = MaskPair->first;
  unsigned CmpMask1 = MaskPair->second;
  if ((CmpMask0 & Mask_AllZeros) && (CmpMask1 == compareBMask)) {
    if (Value *V = foldNegativePower2AndShiftedMask(A, B, D, E, CmpPred0,
                                                    CmpPred1, Builder))
      return V;
  } else if ((CmpMask0 == compareBMask) && (CmpMask1 & Mask_AllZeros)) {
    if (Value *V = foldNegativePower2AndShiftedMask(A, D, B, C, CmpPred1,
                                                    CmpPred0, Builder))
      return V;
  }
  return nullptr;
}

/// Commuted variants are assumed to be handled by calling this function again
/// with the parameters swapped.
static Value *foldUnsignedUnderflowCheck(ICmpInst *ZeroICmp,
                                         ICmpInst *UnsignedICmp, bool IsAnd,
                                         const SimplifyQuery &Q,
                                         InstCombiner::BuilderTy &Builder) {
  Value *ZeroCmpOp;
  ICmpInst::Predicate EqPred;
  if (!match(ZeroICmp, m_ICmp(EqPred, m_Value(ZeroCmpOp), m_Zero())) ||
      !ICmpInst::isEquality(EqPred))
    return nullptr;

  ICmpInst::Predicate UnsignedPred;

  Value *A, *B;
  if (match(UnsignedICmp,
            m_c_ICmp(UnsignedPred, m_Specific(ZeroCmpOp), m_Value(A))) &&
      match(ZeroCmpOp, m_c_Add(m_Specific(A), m_Value(B))) &&
      (ZeroICmp->hasOneUse() || UnsignedICmp->hasOneUse())) {
    auto GetKnownNonZeroAndOther = [&](Value *&NonZero, Value *&Other) {
      if (!isKnownNonZero(NonZero, Q))
        std::swap(NonZero, Other);
      return isKnownNonZero(NonZero, Q);
    };

    // Given  ZeroCmpOp = (A + B)
    //   ZeroCmpOp <  A && ZeroCmpOp != 0  -->  (0-X) <  Y  iff
    //   ZeroCmpOp >= A || ZeroCmpOp == 0  -->  (0-X) >= Y  iff
    //     with X being the value (A/B) that is known to be non-zero,
    //     and Y being remaining value.
    if (UnsignedPred == ICmpInst::ICMP_ULT && EqPred == ICmpInst::ICMP_NE &&
        IsAnd && GetKnownNonZeroAndOther(B, A))
      return Builder.CreateICmpULT(Builder.CreateNeg(B), A);
    if (UnsignedPred == ICmpInst::ICMP_UGE && EqPred == ICmpInst::ICMP_EQ &&
        !IsAnd && GetKnownNonZeroAndOther(B, A))
      return Builder.CreateICmpUGE(Builder.CreateNeg(B), A);
  }

  return nullptr;
}

struct IntPart {
  Value *From;
  unsigned StartBit;
  unsigned NumBits;
};

/// Match an extraction of bits from an integer.
static std::optional<IntPart> matchIntPart(Value *V) {
  Value *X;
  if (!match(V, m_OneUse(m_Trunc(m_Value(X)))))
    return std::nullopt;

  unsigned NumOriginalBits = X->getType()->getScalarSizeInBits();
  unsigned NumExtractedBits = V->getType()->getScalarSizeInBits();
  Value *Y;
  const APInt *Shift;
  // For a trunc(lshr Y, Shift) pattern, make sure we're only extracting bits
  // from Y, not any shifted-in zeroes.
  if (match(X, m_OneUse(m_LShr(m_Value(Y), m_APInt(Shift)))) &&
      Shift->ule(NumOriginalBits - NumExtractedBits))
    return {{Y, (unsigned)Shift->getZExtValue(), NumExtractedBits}};
  return {{X, 0, NumExtractedBits}};
}

/// Materialize an extraction of bits from an integer in IR.
static Value *extractIntPart(const IntPart &P, IRBuilderBase &Builder) {
  Value *V = P.From;
  if (P.StartBit)
    V = Builder.CreateLShr(V, P.StartBit);
  Type *TruncTy = V->getType()->getWithNewBitWidth(P.NumBits);
  if (TruncTy != V->getType())
    V = Builder.CreateTrunc(V, TruncTy);
  return V;
}

/// (icmp eq X0, Y0) & (icmp eq X1, Y1) -> icmp eq X01, Y01
/// (icmp ne X0, Y0) | (icmp ne X1, Y1) -> icmp ne X01, Y01
/// where X0, X1 and Y0, Y1 are adjacent parts extracted from an integer.
Value *InstCombinerImpl::foldEqOfParts(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                       bool IsAnd) {
  if (!Cmp0->hasOneUse() || !Cmp1->hasOneUse())
    return nullptr;

  CmpInst::Predicate Pred = IsAnd ? CmpInst::ICMP_EQ : CmpInst::ICMP_NE;
  auto GetMatchPart = [&](ICmpInst *Cmp,
                          unsigned OpNo) -> std::optional<IntPart> {
    if (Pred == Cmp->getPredicate())
      return matchIntPart(Cmp->getOperand(OpNo));

    const APInt *C;
    // (icmp eq (lshr x, C), (lshr y, C)) gets optimized to:
    // (icmp ult (xor x, y), 1 << C) so also look for that.
    if (Pred == CmpInst::ICMP_EQ && Cmp->getPredicate() == CmpInst::ICMP_ULT) {
      if (!match(Cmp->getOperand(1), m_Power2(C)) ||
          !match(Cmp->getOperand(0), m_Xor(m_Value(), m_Value())))
        return std::nullopt;
    }

    // (icmp ne (lshr x, C), (lshr y, C)) gets optimized to:
    // (icmp ugt (xor x, y), (1 << C) - 1) so also look for that.
    else if (Pred == CmpInst::ICMP_NE &&
             Cmp->getPredicate() == CmpInst::ICMP_UGT) {
      if (!match(Cmp->getOperand(1), m_LowBitMask(C)) ||
          !match(Cmp->getOperand(0), m_Xor(m_Value(), m_Value())))
        return std::nullopt;
    } else {
      return std::nullopt;
    }

    unsigned From = Pred == CmpInst::ICMP_NE ? C->popcount() : C->countr_zero();
    Instruction *I = cast<Instruction>(Cmp->getOperand(0));
    return {{I->getOperand(OpNo), From, C->getBitWidth() - From}};
  };

  std::optional<IntPart> L0 = GetMatchPart(Cmp0, 0);
  std::optional<IntPart> R0 = GetMatchPart(Cmp0, 1);
  std::optional<IntPart> L1 = GetMatchPart(Cmp1, 0);
  std::optional<IntPart> R1 = GetMatchPart(Cmp1, 1);
  if (!L0 || !R0 || !L1 || !R1)
    return nullptr;

  // Make sure the LHS/RHS compare a part of the same value, possibly after
  // an operand swap.
  if (L0->From != L1->From || R0->From != R1->From) {
    if (L0->From != R1->From || R0->From != L1->From)
      return nullptr;
    std::swap(L1, R1);
  }

  // Make sure the extracted parts are adjacent, canonicalizing to L0/R0 being
  // the low part and L1/R1 being the high part.
  if (L0->StartBit + L0->NumBits != L1->StartBit ||
      R0->StartBit + R0->NumBits != R1->StartBit) {
    if (L1->StartBit + L1->NumBits != L0->StartBit ||
        R1->StartBit + R1->NumBits != R0->StartBit)
      return nullptr;
    std::swap(L0, L1);
    std::swap(R0, R1);
  }

  // We can simplify to a comparison of these larger parts of the integers.
  IntPart L = {L0->From, L0->StartBit, L0->NumBits + L1->NumBits};
  IntPart R = {R0->From, R0->StartBit, R0->NumBits + R1->NumBits};
  Value *LValue = extractIntPart(L, Builder);
  Value *RValue = extractIntPart(R, Builder);
  return Builder.CreateICmp(Pred, LValue, RValue);
}

/// Reduce logic-of-compares with equality to a constant by substituting a
/// common operand with the constant. Callers are expected to call this with
/// Cmp0/Cmp1 switched to handle logic op commutativity.
static Value *foldAndOrOfICmpsWithConstEq(ICmpInst *Cmp0, ICmpInst *Cmp1,
                                          bool IsAnd, bool IsLogical,
                                          InstCombiner::BuilderTy &Builder,
                                          const SimplifyQuery &Q) {
  // Match an equality compare with a non-poison constant as Cmp0.
  // Also, give up if the compare can be constant-folded to avoid looping.
  ICmpInst::Predicate Pred0;
  Value *X;
  Constant *C;
  if (!match(Cmp0, m_ICmp(Pred0, m_Value(X), m_Constant(C))) ||
      !isGuaranteedNotToBeUndefOrPoison(C) || isa<Constant>(X))
    return nullptr;
  if ((IsAnd && Pred0 != ICmpInst::ICMP_EQ) ||
      (!IsAnd && Pred0 != ICmpInst::ICMP_NE))
    return nullptr;

  // The other compare must include a common operand (X). Canonicalize the
  // common operand as operand 1 (Pred1 is swapped if the common operand was
  // operand 0).
  Value *Y;
  ICmpInst::Predicate Pred1;
  if (!match(Cmp1, m_c_ICmp(Pred1, m_Value(Y), m_Specific(X))))
    return nullptr;

  // Replace variable with constant value equivalence to remove a variable use:
  // (X == C) && (Y Pred1 X) --> (X == C) && (Y Pred1 C)
  // (X != C) || (Y Pred1 X) --> (X != C) || (Y Pred1 C)
  // Can think of the 'or' substitution with the 'and' bool equivalent:
  // A || B --> A || (!A && B)
  Value *SubstituteCmp = simplifyICmpInst(Pred1, Y, C, Q);
  if (!SubstituteCmp) {
    // If we need to create a new instruction, require that the old compare can
    // be removed.
    if (!Cmp1->hasOneUse())
      return nullptr;
    SubstituteCmp = Builder.CreateICmp(Pred1, Y, C);
  }
  if (IsLogical)
    return IsAnd ? Builder.CreateLogicalAnd(Cmp0, SubstituteCmp)
                 : Builder.CreateLogicalOr(Cmp0, SubstituteCmp);
  return Builder.CreateBinOp(IsAnd ? Instruction::And : Instruction::Or, Cmp0,
                             SubstituteCmp);
}

/// Fold (icmp Pred1 V1, C1) & (icmp Pred2 V2, C2)
/// or   (icmp Pred1 V1, C1) | (icmp Pred2 V2, C2)
/// into a single comparison using range-based reasoning.
/// NOTE: This is also used for logical and/or, must be poison-safe!
Value *InstCombinerImpl::foldAndOrOfICmpsUsingRanges(ICmpInst *ICmp1,
                                                     ICmpInst *ICmp2,
                                                     bool IsAnd) {
  ICmpInst::Predicate Pred1, Pred2;
  Value *V1, *V2;
  const APInt *C1, *C2;
  if (!match(ICmp1, m_ICmp(Pred1, m_Value(V1), m_APInt(C1))) ||
      !match(ICmp2, m_ICmp(Pred2, m_Value(V2), m_APInt(C2))))
    return nullptr;

  // Look through add of a constant offset on V1, V2, or both operands. This
  // allows us to interpret the V + C' < C'' range idiom into a proper range.
  const APInt *Offset1 = nullptr, *Offset2 = nullptr;
  if (V1 != V2) {
    Value *X;
    if (match(V1, m_Add(m_Value(X), m_APInt(Offset1))))
      V1 = X;
    if (match(V2, m_Add(m_Value(X), m_APInt(Offset2))))
      V2 = X;
  }

  if (V1 != V2)
    return nullptr;

  ConstantRange CR1 = ConstantRange::makeExactICmpRegion(
      IsAnd ? ICmpInst::getInversePredicate(Pred1) : Pred1, *C1);
  if (Offset1)
    CR1 = CR1.subtract(*Offset1);

  ConstantRange CR2 = ConstantRange::makeExactICmpRegion(
      IsAnd ? ICmpInst::getInversePredicate(Pred2) : Pred2, *C2);
  if (Offset2)
    CR2 = CR2.subtract(*Offset2);

  Type *Ty = V1->getType();
  Value *NewV = V1;
  std::optional<ConstantRange> CR = CR1.exactUnionWith(CR2);
  if (!CR) {
    if (!(ICmp1->hasOneUse() && ICmp2->hasOneUse()) || CR1.isWrappedSet() ||
        CR2.isWrappedSet())
      return nullptr;

    // Check whether we have equal-size ranges that only differ by one bit.
    // In that case we can apply a mask to map one range onto the other.
    APInt LowerDiff = CR1.getLower() ^ CR2.getLower();
    APInt UpperDiff = (CR1.getUpper() - 1) ^ (CR2.getUpper() - 1);
    APInt CR1Size = CR1.getUpper() - CR1.getLower();
    if (!LowerDiff.isPowerOf2() || LowerDiff != UpperDiff ||
        CR1Size != CR2.getUpper() - CR2.getLower())
      return nullptr;

    CR = CR1.getLower().ult(CR2.getLower()) ? CR1 : CR2;
    NewV = Builder.CreateAnd(NewV, ConstantInt::get(Ty, ~LowerDiff));
  }

  if (IsAnd)
    CR = CR->inverse();

  CmpInst::Predicate NewPred;
  APInt NewC, Offset;
  CR->getEquivalentICmp(NewPred, NewC, Offset);

  if (Offset != 0)
    NewV = Builder.CreateAdd(NewV, ConstantInt::get(Ty, Offset));
  return Builder.CreateICmp(NewPred, NewV, ConstantInt::get(Ty, NewC));
}

/// Ignore all operations which only change the sign of a value, returning the
/// underlying magnitude value.
static Value *stripSignOnlyFPOps(Value *Val) {
  match(Val, m_FNeg(m_Value(Val)));
  match(Val, m_FAbs(m_Value(Val)));
  match(Val, m_CopySign(m_Value(Val), m_Value()));
  return Val;
}

/// Matches canonical form of isnan, fcmp ord x, 0
static bool matchIsNotNaN(FCmpInst::Predicate P, Value *LHS, Value *RHS) {
  return P == FCmpInst::FCMP_ORD && match(RHS, m_AnyZeroFP());
}

/// Matches fcmp u__ x, +/-inf
static bool matchUnorderedInfCompare(FCmpInst::Predicate P, Value *LHS,
                                     Value *RHS) {
  return FCmpInst::isUnordered(P) && match(RHS, m_Inf());
}

/// and (fcmp ord x, 0), (fcmp u* x, inf) -> fcmp o* x, inf
///
/// Clang emits this pattern for doing an isfinite check in __builtin_isnormal.
static Value *matchIsFiniteTest(InstCombiner::BuilderTy &Builder, FCmpInst *LHS,
                                FCmpInst *RHS) {
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);
  FCmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();

  if (!matchIsNotNaN(PredL, LHS0, LHS1) ||
      !matchUnorderedInfCompare(PredR, RHS0, RHS1))
    return nullptr;

  IRBuilder<>::FastMathFlagGuard FMFG(Builder);
  FastMathFlags FMF = LHS->getFastMathFlags();
  FMF &= RHS->getFastMathFlags();
  Builder.setFastMathFlags(FMF);

  return Builder.CreateFCmp(FCmpInst::getOrderedPredicate(PredR), RHS0, RHS1);
}

Value *InstCombinerImpl::foldLogicOfFCmps(FCmpInst *LHS, FCmpInst *RHS,
                                          bool IsAnd, bool IsLogicalSelect) {
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);
  FCmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();

  if (LHS0 == RHS1 && RHS0 == LHS1) {
    // Swap RHS operands to match LHS.
    PredR = FCmpInst::getSwappedPredicate(PredR);
    std::swap(RHS0, RHS1);
  }

  // Simplify (fcmp cc0 x, y) & (fcmp cc1 x, y).
  // Suppose the relation between x and y is R, where R is one of
  // U(1000), L(0100), G(0010) or E(0001), and CC0 and CC1 are the bitmasks for
  // testing the desired relations.
  //
  // Since (R & CC0) and (R & CC1) are either R or 0, we actually have this:
  //    bool(R & CC0) && bool(R & CC1)
  //  = bool((R & CC0) & (R & CC1))
  //  = bool(R & (CC0 & CC1)) <= by re-association, commutation, and idempotency
  //
  // Since (R & CC0) and (R & CC1) are either R or 0, we actually have this:
  //    bool(R & CC0) || bool(R & CC1)
  //  = bool((R & CC0) | (R & CC1))
  //  = bool(R & (CC0 | CC1)) <= by reversed distribution (contribution? ;)
  if (LHS0 == RHS0 && LHS1 == RHS1) {
    unsigned FCmpCodeL = getFCmpCode(PredL);
    unsigned FCmpCodeR = getFCmpCode(PredR);
    unsigned NewPred = IsAnd ? FCmpCodeL & FCmpCodeR : FCmpCodeL | FCmpCodeR;

    // Intersect the fast math flags.
    // TODO: We can union the fast math flags unless this is a logical select.
    IRBuilder<>::FastMathFlagGuard FMFG(Builder);
    FastMathFlags FMF = LHS->getFastMathFlags();
    FMF &= RHS->getFastMathFlags();
    Builder.setFastMathFlags(FMF);

    return getFCmpValue(NewPred, LHS0, LHS1, Builder);
  }

  // This transform is not valid for a logical select.
  if (!IsLogicalSelect &&
      ((PredL == FCmpInst::FCMP_ORD && PredR == FCmpInst::FCMP_ORD && IsAnd) ||
       (PredL == FCmpInst::FCMP_UNO && PredR == FCmpInst::FCMP_UNO &&
        !IsAnd))) {
    if (LHS0->getType() != RHS0->getType())
      return nullptr;

    // FCmp canonicalization ensures that (fcmp ord/uno X, X) and
    // (fcmp ord/uno X, C) will be transformed to (fcmp X, +0.0).
    if (match(LHS1, m_PosZeroFP()) && match(RHS1, m_PosZeroFP()))
      // Ignore the constants because they are obviously not NANs:
      // (fcmp ord x, 0.0) & (fcmp ord y, 0.0)  -> (fcmp ord x, y)
      // (fcmp uno x, 0.0) | (fcmp uno y, 0.0)  -> (fcmp uno x, y)
      return Builder.CreateFCmp(PredL, LHS0, RHS0);
  }

  if (IsAnd && stripSignOnlyFPOps(LHS0) == stripSignOnlyFPOps(RHS0)) {
    // and (fcmp ord x, 0), (fcmp u* x, inf) -> fcmp o* x, inf
    // and (fcmp ord x, 0), (fcmp u* fabs(x), inf) -> fcmp o* x, inf
    if (Value *Left = matchIsFiniteTest(Builder, LHS, RHS))
      return Left;
    if (Value *Right = matchIsFiniteTest(Builder, RHS, LHS))
      return Right;
  }

  // Turn at least two fcmps with constants into llvm.is.fpclass.
  //
  // If we can represent a combined value test with one class call, we can
  // potentially eliminate 4-6 instructions. If we can represent a test with a
  // single fcmp with fneg and fabs, that's likely a better canonical form.
  if (LHS->hasOneUse() && RHS->hasOneUse()) {
    auto [ClassValRHS, ClassMaskRHS] =
        fcmpToClassTest(PredR, *RHS->getFunction(), RHS0, RHS1);
    if (ClassValRHS) {
      auto [ClassValLHS, ClassMaskLHS] =
          fcmpToClassTest(PredL, *LHS->getFunction(), LHS0, LHS1);
      if (ClassValLHS == ClassValRHS) {
        unsigned CombinedMask = IsAnd ? (ClassMaskLHS & ClassMaskRHS)
                                      : (ClassMaskLHS | ClassMaskRHS);
        return Builder.CreateIntrinsic(
            Intrinsic::is_fpclass, {ClassValLHS->getType()},
            {ClassValLHS, Builder.getInt32(CombinedMask)});
      }
    }
  }

  // Canonicalize the range check idiom:
  // and (fcmp olt/ole/ult/ule x, C), (fcmp ogt/oge/ugt/uge x, -C)
  // --> fabs(x) olt/ole/ult/ule C
  // or  (fcmp ogt/oge/ugt/uge x, C), (fcmp olt/ole/ult/ule x, -C)
  // --> fabs(x) ogt/oge/ugt/uge C
  // TODO: Generalize to handle a negated variable operand?
  const APFloat *LHSC, *RHSC;
  if (LHS0 == RHS0 && LHS->hasOneUse() && RHS->hasOneUse() &&
      FCmpInst::getSwappedPredicate(PredL) == PredR &&
      match(LHS1, m_APFloatAllowPoison(LHSC)) &&
      match(RHS1, m_APFloatAllowPoison(RHSC)) &&
      LHSC->bitwiseIsEqual(neg(*RHSC))) {
    auto IsLessThanOrLessEqual = [](FCmpInst::Predicate Pred) {
      switch (Pred) {
      case FCmpInst::FCMP_OLT:
      case FCmpInst::FCMP_OLE:
      case FCmpInst::FCMP_ULT:
      case FCmpInst::FCMP_ULE:
        return true;
      default:
        return false;
      }
    };
    if (IsLessThanOrLessEqual(IsAnd ? PredR : PredL)) {
      std::swap(LHSC, RHSC);
      std::swap(PredL, PredR);
    }
    if (IsLessThanOrLessEqual(IsAnd ? PredL : PredR)) {
      BuilderTy::FastMathFlagGuard Guard(Builder);
      Builder.setFastMathFlags(LHS->getFastMathFlags() |
                               RHS->getFastMathFlags());

      Value *FAbs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, LHS0);
      return Builder.CreateFCmp(PredL, FAbs,
                                ConstantFP::get(LHS0->getType(), *LHSC));
    }
  }

  return nullptr;
}

/// Match an fcmp against a special value that performs a test possible by
/// llvm.is.fpclass.
static bool matchIsFPClassLikeFCmp(Value *Op, Value *&ClassVal,
                                   uint64_t &ClassMask) {
  auto *FCmp = dyn_cast<FCmpInst>(Op);
  if (!FCmp || !FCmp->hasOneUse())
    return false;

  std::tie(ClassVal, ClassMask) =
      fcmpToClassTest(FCmp->getPredicate(), *FCmp->getParent()->getParent(),
                      FCmp->getOperand(0), FCmp->getOperand(1));
  return ClassVal != nullptr;
}

/// or (is_fpclass x, mask0), (is_fpclass x, mask1)
///     -> is_fpclass x, (mask0 | mask1)
/// and (is_fpclass x, mask0), (is_fpclass x, mask1)
///     -> is_fpclass x, (mask0 & mask1)
/// xor (is_fpclass x, mask0), (is_fpclass x, mask1)
///     -> is_fpclass x, (mask0 ^ mask1)
Instruction *InstCombinerImpl::foldLogicOfIsFPClass(BinaryOperator &BO,
                                                    Value *Op0, Value *Op1) {
  Value *ClassVal0 = nullptr;
  Value *ClassVal1 = nullptr;
  uint64_t ClassMask0, ClassMask1;

  // Restrict to folding one fcmp into one is.fpclass for now, don't introduce a
  // new class.
  //
  // TODO: Support forming is.fpclass out of 2 separate fcmps when codegen is
  // better.

  bool IsLHSClass =
      match(Op0, m_OneUse(m_Intrinsic<Intrinsic::is_fpclass>(
                     m_Value(ClassVal0), m_ConstantInt(ClassMask0))));
  bool IsRHSClass =
      match(Op1, m_OneUse(m_Intrinsic<Intrinsic::is_fpclass>(
                     m_Value(ClassVal1), m_ConstantInt(ClassMask1))));
  if ((((IsLHSClass || matchIsFPClassLikeFCmp(Op0, ClassVal0, ClassMask0)) &&
        (IsRHSClass || matchIsFPClassLikeFCmp(Op1, ClassVal1, ClassMask1)))) &&
      ClassVal0 == ClassVal1) {
    unsigned NewClassMask;
    switch (BO.getOpcode()) {
    case Instruction::And:
      NewClassMask = ClassMask0 & ClassMask1;
      break;
    case Instruction::Or:
      NewClassMask = ClassMask0 | ClassMask1;
      break;
    case Instruction::Xor:
      NewClassMask = ClassMask0 ^ ClassMask1;
      break;
    default:
      llvm_unreachable("not a binary logic operator");
    }

    if (IsLHSClass) {
      auto *II = cast<IntrinsicInst>(Op0);
      II->setArgOperand(
          1, ConstantInt::get(II->getArgOperand(1)->getType(), NewClassMask));
      return replaceInstUsesWith(BO, II);
    }

    if (IsRHSClass) {
      auto *II = cast<IntrinsicInst>(Op1);
      II->setArgOperand(
          1, ConstantInt::get(II->getArgOperand(1)->getType(), NewClassMask));
      return replaceInstUsesWith(BO, II);
    }

    CallInst *NewClass =
        Builder.CreateIntrinsic(Intrinsic::is_fpclass, {ClassVal0->getType()},
                                {ClassVal0, Builder.getInt32(NewClassMask)});
    return replaceInstUsesWith(BO, NewClass);
  }

  return nullptr;
}

/// Look for the pattern that conditionally negates a value via math operations:
///   cond.splat = sext i1 cond
///   sub = add cond.splat, x
///   xor = xor sub, cond.splat
/// and rewrite it to do the same, but via logical operations:
///   value.neg = sub 0, value
///   cond = select i1 neg, value.neg, value
Instruction *InstCombinerImpl::canonicalizeConditionalNegationViaMathToSelect(
    BinaryOperator &I) {
  assert(I.getOpcode() == BinaryOperator::Xor && "Only for xor!");
  Value *Cond, *X;
  // As per complexity ordering, `xor` is not commutative here.
  if (!match(&I, m_c_BinOp(m_OneUse(m_Value()), m_Value())) ||
      !match(I.getOperand(1), m_SExt(m_Value(Cond))) ||
      !Cond->getType()->isIntOrIntVectorTy(1) ||
      !match(I.getOperand(0), m_c_Add(m_SExt(m_Specific(Cond)), m_Value(X))))
    return nullptr;
  return SelectInst::Create(Cond, Builder.CreateNeg(X, X->getName() + ".neg"),
                            X);
}

/// This a limited reassociation for a special case (see above) where we are
/// checking if two values are either both NAN (unordered) or not-NAN (ordered).
/// This could be handled more generally in '-reassociation', but it seems like
/// an unlikely pattern for a large number of logic ops and fcmps.
static Instruction *reassociateFCmps(BinaryOperator &BO,
                                     InstCombiner::BuilderTy &Builder) {
  Instruction::BinaryOps Opcode = BO.getOpcode();
  assert((Opcode == Instruction::And || Opcode == Instruction::Or) &&
         "Expecting and/or op for fcmp transform");

  // There are 4 commuted variants of the pattern. Canonicalize operands of this
  // logic op so an fcmp is operand 0 and a matching logic op is operand 1.
  Value *Op0 = BO.getOperand(0), *Op1 = BO.getOperand(1), *X;
  FCmpInst::Predicate Pred;
  if (match(Op1, m_FCmp(Pred, m_Value(), m_AnyZeroFP())))
    std::swap(Op0, Op1);

  // Match inner binop and the predicate for combining 2 NAN checks into 1.
  Value *BO10, *BO11;
  FCmpInst::Predicate NanPred = Opcode == Instruction::And ? FCmpInst::FCMP_ORD
                                                           : FCmpInst::FCMP_UNO;
  if (!match(Op0, m_FCmp(Pred, m_Value(X), m_AnyZeroFP())) || Pred != NanPred ||
      !match(Op1, m_BinOp(Opcode, m_Value(BO10), m_Value(BO11))))
    return nullptr;

  // The inner logic op must have a matching fcmp operand.
  Value *Y;
  if (!match(BO10, m_FCmp(Pred, m_Value(Y), m_AnyZeroFP())) ||
      Pred != NanPred || X->getType() != Y->getType())
    std::swap(BO10, BO11);

  if (!match(BO10, m_FCmp(Pred, m_Value(Y), m_AnyZeroFP())) ||
      Pred != NanPred || X->getType() != Y->getType())
    return nullptr;

  // and (fcmp ord X, 0), (and (fcmp ord Y, 0), Z) --> and (fcmp ord X, Y), Z
  // or  (fcmp uno X, 0), (or  (fcmp uno Y, 0), Z) --> or  (fcmp uno X, Y), Z
  Value *NewFCmp = Builder.CreateFCmp(Pred, X, Y);
  if (auto *NewFCmpInst = dyn_cast<FCmpInst>(NewFCmp)) {
    // Intersect FMF from the 2 source fcmps.
    NewFCmpInst->copyIRFlags(Op0);
    NewFCmpInst->andIRFlags(BO10);
  }
  return BinaryOperator::Create(Opcode, NewFCmp, BO11);
}

/// Match variations of De Morgan's Laws:
/// (~A & ~B) == (~(A | B))
/// (~A | ~B) == (~(A & B))
static Instruction *matchDeMorgansLaws(BinaryOperator &I,
                                       InstCombiner &IC) {
  const Instruction::BinaryOps Opcode = I.getOpcode();
  assert((Opcode == Instruction::And || Opcode == Instruction::Or) &&
         "Trying to match De Morgan's Laws with something other than and/or");

  // Flip the logic operation.
  const Instruction::BinaryOps FlippedOpcode =
      (Opcode == Instruction::And) ? Instruction::Or : Instruction::And;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *A, *B;
  if (match(Op0, m_OneUse(m_Not(m_Value(A)))) &&
      match(Op1, m_OneUse(m_Not(m_Value(B)))) &&
      !IC.isFreeToInvert(A, A->hasOneUse()) &&
      !IC.isFreeToInvert(B, B->hasOneUse())) {
    Value *AndOr =
        IC.Builder.CreateBinOp(FlippedOpcode, A, B, I.getName() + ".demorgan");
    return BinaryOperator::CreateNot(AndOr);
  }

  // The 'not' ops may require reassociation.
  // (A & ~B) & ~C --> A & ~(B | C)
  // (~B & A) & ~C --> A & ~(B | C)
  // (A | ~B) | ~C --> A | ~(B & C)
  // (~B | A) | ~C --> A | ~(B & C)
  Value *C;
  if (match(Op0, m_OneUse(m_c_BinOp(Opcode, m_Value(A), m_Not(m_Value(B))))) &&
      match(Op1, m_Not(m_Value(C)))) {
    Value *FlippedBO = IC.Builder.CreateBinOp(FlippedOpcode, B, C);
    return BinaryOperator::Create(Opcode, A, IC.Builder.CreateNot(FlippedBO));
  }

  return nullptr;
}

bool InstCombinerImpl::shouldOptimizeCast(CastInst *CI) {
  Value *CastSrc = CI->getOperand(0);

  // Noop casts and casts of constants should be eliminated trivially.
  if (CI->getSrcTy() == CI->getDestTy() || isa<Constant>(CastSrc))
    return false;

  // If this cast is paired with another cast that can be eliminated, we prefer
  // to have it eliminated.
  if (const auto *PrecedingCI = dyn_cast<CastInst>(CastSrc))
    if (isEliminableCastPair(PrecedingCI, CI))
      return false;

  return true;
}

/// Fold {and,or,xor} (cast X), C.
static Instruction *foldLogicCastConstant(BinaryOperator &Logic, CastInst *Cast,
                                          InstCombinerImpl &IC) {
  Constant *C = dyn_cast<Constant>(Logic.getOperand(1));
  if (!C)
    return nullptr;

  auto LogicOpc = Logic.getOpcode();
  Type *DestTy = Logic.getType();
  Type *SrcTy = Cast->getSrcTy();

  // Move the logic operation ahead of a zext or sext if the constant is
  // unchanged in the smaller source type. Performing the logic in a smaller
  // type may provide more information to later folds, and the smaller logic
  // instruction may be cheaper (particularly in the case of vectors).
  Value *X;
  if (match(Cast, m_OneUse(m_ZExt(m_Value(X))))) {
    if (Constant *TruncC = IC.getLosslessUnsignedTrunc(C, SrcTy)) {
      // LogicOpc (zext X), C --> zext (LogicOpc X, C)
      Value *NewOp = IC.Builder.CreateBinOp(LogicOpc, X, TruncC);
      return new ZExtInst(NewOp, DestTy);
    }
  }

  if (match(Cast, m_OneUse(m_SExtLike(m_Value(X))))) {
    if (Constant *TruncC = IC.getLosslessSignedTrunc(C, SrcTy)) {
      // LogicOpc (sext X), C --> sext (LogicOpc X, C)
      Value *NewOp = IC.Builder.CreateBinOp(LogicOpc, X, TruncC);
      return new SExtInst(NewOp, DestTy);
    }
  }

  return nullptr;
}

/// Fold {and,or,xor} (cast X), Y.
Instruction *InstCombinerImpl::foldCastedBitwiseLogic(BinaryOperator &I) {
  auto LogicOpc = I.getOpcode();
  assert(I.isBitwiseLogicOp() && "Unexpected opcode for bitwise logic folding");

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  // fold bitwise(A >> BW - 1, zext(icmp))     (BW is the scalar bits of the
  // type of A)
  //   -> bitwise(zext(A < 0), zext(icmp))
  //   -> zext(bitwise(A < 0, icmp))
  auto FoldBitwiseICmpZeroWithICmp = [&](Value *Op0,
                                         Value *Op1) -> Instruction * {
    ICmpInst::Predicate Pred;
    Value *A;
    bool IsMatched =
        match(Op0,
              m_OneUse(m_LShr(
                  m_Value(A),
                  m_SpecificInt(Op0->getType()->getScalarSizeInBits() - 1)))) &&
        match(Op1, m_OneUse(m_ZExt(m_ICmp(Pred, m_Value(), m_Value()))));

    if (!IsMatched)
      return nullptr;

    auto *ICmpL =
        Builder.CreateICmpSLT(A, Constant::getNullValue(A->getType()));
    auto *ICmpR = cast<ZExtInst>(Op1)->getOperand(0);
    auto *BitwiseOp = Builder.CreateBinOp(LogicOpc, ICmpL, ICmpR);

    return new ZExtInst(BitwiseOp, Op0->getType());
  };

  if (auto *Ret = FoldBitwiseICmpZeroWithICmp(Op0, Op1))
    return Ret;

  if (auto *Ret = FoldBitwiseICmpZeroWithICmp(Op1, Op0))
    return Ret;

  CastInst *Cast0 = dyn_cast<CastInst>(Op0);
  if (!Cast0)
    return nullptr;

  // This must be a cast from an integer or integer vector source type to allow
  // transformation of the logic operation to the source type.
  Type *DestTy = I.getType();
  Type *SrcTy = Cast0->getSrcTy();
  if (!SrcTy->isIntOrIntVectorTy())
    return nullptr;

  if (Instruction *Ret = foldLogicCastConstant(I, Cast0, *this))
    return Ret;

  CastInst *Cast1 = dyn_cast<CastInst>(Op1);
  if (!Cast1)
    return nullptr;

  // Both operands of the logic operation are casts. The casts must be the
  // same kind for reduction.
  Instruction::CastOps CastOpcode = Cast0->getOpcode();
  if (CastOpcode != Cast1->getOpcode())
    return nullptr;

  // If the source types do not match, but the casts are matching extends, we
  // can still narrow the logic op.
  if (SrcTy != Cast1->getSrcTy()) {
    Value *X, *Y;
    if (match(Cast0, m_OneUse(m_ZExtOrSExt(m_Value(X)))) &&
        match(Cast1, m_OneUse(m_ZExtOrSExt(m_Value(Y))))) {
      // Cast the narrower source to the wider source type.
      unsigned XNumBits = X->getType()->getScalarSizeInBits();
      unsigned YNumBits = Y->getType()->getScalarSizeInBits();
      if (XNumBits < YNumBits)
        X = Builder.CreateCast(CastOpcode, X, Y->getType());
      else
        Y = Builder.CreateCast(CastOpcode, Y, X->getType());
      // Do the logic op in the intermediate width, then widen more.
      Value *NarrowLogic = Builder.CreateBinOp(LogicOpc, X, Y);
      return CastInst::Create(CastOpcode, NarrowLogic, DestTy);
    }

    // Give up for other cast opcodes.
    return nullptr;
  }

  Value *Cast0Src = Cast0->getOperand(0);
  Value *Cast1Src = Cast1->getOperand(0);

  // fold logic(cast(A), cast(B)) -> cast(logic(A, B))
  if ((Cast0->hasOneUse() || Cast1->hasOneUse()) &&
      shouldOptimizeCast(Cast0) && shouldOptimizeCast(Cast1)) {
    Value *NewOp = Builder.CreateBinOp(LogicOpc, Cast0Src, Cast1Src,
                                       I.getName());
    return CastInst::Create(CastOpcode, NewOp, DestTy);
  }

  return nullptr;
}

static Instruction *foldAndToXor(BinaryOperator &I,
                                 InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::And);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // Operand complexity canonicalization guarantees that the 'or' is Op0.
  // (A | B) & ~(A & B) --> A ^ B
  // (A | B) & ~(B & A) --> A ^ B
  if (match(&I, m_BinOp(m_Or(m_Value(A), m_Value(B)),
                        m_Not(m_c_And(m_Deferred(A), m_Deferred(B))))))
    return BinaryOperator::CreateXor(A, B);

  // (A | ~B) & (~A | B) --> ~(A ^ B)
  // (A | ~B) & (B | ~A) --> ~(A ^ B)
  // (~B | A) & (~A | B) --> ~(A ^ B)
  // (~B | A) & (B | ~A) --> ~(A ^ B)
  if (Op0->hasOneUse() || Op1->hasOneUse())
    if (match(&I, m_BinOp(m_c_Or(m_Value(A), m_Not(m_Value(B))),
                          m_c_Or(m_Not(m_Deferred(A)), m_Deferred(B)))))
      return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  return nullptr;
}

static Instruction *foldOrToXor(BinaryOperator &I,
                                InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::Or);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // Operand complexity canonicalization guarantees that the 'and' is Op0.
  // (A & B) | ~(A | B) --> ~(A ^ B)
  // (A & B) | ~(B | A) --> ~(A ^ B)
  if (Op0->hasOneUse() || Op1->hasOneUse())
    if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
        match(Op1, m_Not(m_c_Or(m_Specific(A), m_Specific(B)))))
      return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  // Operand complexity canonicalization guarantees that the 'xor' is Op0.
  // (A ^ B) | ~(A | B) --> ~(A & B)
  // (A ^ B) | ~(B | A) --> ~(A & B)
  if (Op0->hasOneUse() || Op1->hasOneUse())
    if (match(Op0, m_Xor(m_Value(A), m_Value(B))) &&
        match(Op1, m_Not(m_c_Or(m_Specific(A), m_Specific(B)))))
      return BinaryOperator::CreateNot(Builder.CreateAnd(A, B));

  // (A & ~B) | (~A & B) --> A ^ B
  // (A & ~B) | (B & ~A) --> A ^ B
  // (~B & A) | (~A & B) --> A ^ B
  // (~B & A) | (B & ~A) --> A ^ B
  if (match(Op0, m_c_And(m_Value(A), m_Not(m_Value(B)))) &&
      match(Op1, m_c_And(m_Not(m_Specific(A)), m_Specific(B))))
    return BinaryOperator::CreateXor(A, B);

  return nullptr;
}

/// Return true if a constant shift amount is always less than the specified
/// bit-width. If not, the shift could create poison in the narrower type.
static bool canNarrowShiftAmt(Constant *C, unsigned BitWidth) {
  APInt Threshold(C->getType()->getScalarSizeInBits(), BitWidth);
  return match(C, m_SpecificInt_ICMP(ICmpInst::ICMP_ULT, Threshold));
}

/// Try to use narrower ops (sink zext ops) for an 'and' with binop operand and
/// a common zext operand: and (binop (zext X), C), (zext X).
Instruction *InstCombinerImpl::narrowMaskedBinOp(BinaryOperator &And) {
  // This transform could also apply to {or, and, xor}, but there are better
  // folds for those cases, so we don't expect those patterns here. AShr is not
  // handled because it should always be transformed to LShr in this sequence.
  // The subtract transform is different because it has a constant on the left.
  // Add/mul commute the constant to RHS; sub with constant RHS becomes add.
  Value *Op0 = And.getOperand(0), *Op1 = And.getOperand(1);
  Constant *C;
  if (!match(Op0, m_OneUse(m_Add(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Mul(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_LShr(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Shl(m_Specific(Op1), m_Constant(C)))) &&
      !match(Op0, m_OneUse(m_Sub(m_Constant(C), m_Specific(Op1)))))
    return nullptr;

  Value *X;
  if (!match(Op1, m_ZExt(m_Value(X))) || Op1->hasNUsesOrMore(3))
    return nullptr;

  Type *Ty = And.getType();
  if (!isa<VectorType>(Ty) && !shouldChangeType(Ty, X->getType()))
    return nullptr;

  // If we're narrowing a shift, the shift amount must be safe (less than the
  // width) in the narrower type. If the shift amount is greater, instsimplify
  // usually handles that case, but we can't guarantee/assert it.
  Instruction::BinaryOps Opc = cast<BinaryOperator>(Op0)->getOpcode();
  if (Opc == Instruction::LShr || Opc == Instruction::Shl)
    if (!canNarrowShiftAmt(C, X->getType()->getScalarSizeInBits()))
      return nullptr;

  // and (sub C, (zext X)), (zext X) --> zext (and (sub C', X), X)
  // and (binop (zext X), C), (zext X) --> zext (and (binop X, C'), X)
  Value *NewC = ConstantExpr::getTrunc(C, X->getType());
  Value *NewBO = Opc == Instruction::Sub ? Builder.CreateBinOp(Opc, NewC, X)
                                         : Builder.CreateBinOp(Opc, X, NewC);
  return new ZExtInst(Builder.CreateAnd(NewBO, X), Ty);
}

/// Try folding relatively complex patterns for both And and Or operations
/// with all And and Or swapped.
static Instruction *foldComplexAndOrPatterns(BinaryOperator &I,
                                             InstCombiner::BuilderTy &Builder) {
  const Instruction::BinaryOps Opcode = I.getOpcode();
  assert(Opcode == Instruction::And || Opcode == Instruction::Or);

  // Flip the logic operation.
  const Instruction::BinaryOps FlippedOpcode =
      (Opcode == Instruction::And) ? Instruction::Or : Instruction::And;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *A, *B, *C, *X, *Y, *Dummy;

  // Match following expressions:
  // (~(A | B) & C)
  // (~(A & B) | C)
  // Captures X = ~(A | B) or ~(A & B)
  const auto matchNotOrAnd =
      [Opcode, FlippedOpcode](Value *Op, auto m_A, auto m_B, auto m_C,
                              Value *&X, bool CountUses = false) -> bool {
    if (CountUses && !Op->hasOneUse())
      return false;

    if (match(Op, m_c_BinOp(FlippedOpcode,
                            m_CombineAnd(m_Value(X),
                                         m_Not(m_c_BinOp(Opcode, m_A, m_B))),
                            m_C)))
      return !CountUses || X->hasOneUse();

    return false;
  };

  // (~(A | B) & C) | ... --> ...
  // (~(A & B) | C) & ... --> ...
  // TODO: One use checks are conservative. We just need to check that a total
  //       number of multiple used values does not exceed reduction
  //       in operations.
  if (matchNotOrAnd(Op0, m_Value(A), m_Value(B), m_Value(C), X)) {
    // (~(A | B) & C) | (~(A | C) & B) --> (B ^ C) & ~A
    // (~(A & B) | C) & (~(A & C) | B) --> ~((B ^ C) & A)
    if (matchNotOrAnd(Op1, m_Specific(A), m_Specific(C), m_Specific(B), Dummy,
                      true)) {
      Value *Xor = Builder.CreateXor(B, C);
      return (Opcode == Instruction::Or)
                 ? BinaryOperator::CreateAnd(Xor, Builder.CreateNot(A))
                 : BinaryOperator::CreateNot(Builder.CreateAnd(Xor, A));
    }

    // (~(A | B) & C) | (~(B | C) & A) --> (A ^ C) & ~B
    // (~(A & B) | C) & (~(B & C) | A) --> ~((A ^ C) & B)
    if (matchNotOrAnd(Op1, m_Specific(B), m_Specific(C), m_Specific(A), Dummy,
                      true)) {
      Value *Xor = Builder.CreateXor(A, C);
      return (Opcode == Instruction::Or)
                 ? BinaryOperator::CreateAnd(Xor, Builder.CreateNot(B))
                 : BinaryOperator::CreateNot(Builder.CreateAnd(Xor, B));
    }

    // (~(A | B) & C) | ~(A | C) --> ~((B & C) | A)
    // (~(A & B) | C) & ~(A & C) --> ~((B | C) & A)
    if (match(Op1, m_OneUse(m_Not(m_OneUse(
                       m_c_BinOp(Opcode, m_Specific(A), m_Specific(C)))))))
      return BinaryOperator::CreateNot(Builder.CreateBinOp(
          Opcode, Builder.CreateBinOp(FlippedOpcode, B, C), A));

    // (~(A | B) & C) | ~(B | C) --> ~((A & C) | B)
    // (~(A & B) | C) & ~(B & C) --> ~((A | C) & B)
    if (match(Op1, m_OneUse(m_Not(m_OneUse(
                       m_c_BinOp(Opcode, m_Specific(B), m_Specific(C)))))))
      return BinaryOperator::CreateNot(Builder.CreateBinOp(
          Opcode, Builder.CreateBinOp(FlippedOpcode, A, C), B));

    // (~(A | B) & C) | ~(C | (A ^ B)) --> ~((A | B) & (C | (A ^ B)))
    // Note, the pattern with swapped and/or is not handled because the
    // result is more undefined than a source:
    // (~(A & B) | C) & ~(C & (A ^ B)) --> (A ^ B ^ C) | ~(A | C) is invalid.
    if (Opcode == Instruction::Or && Op0->hasOneUse() &&
        match(Op1, m_OneUse(m_Not(m_CombineAnd(
                       m_Value(Y),
                       m_c_BinOp(Opcode, m_Specific(C),
                                 m_c_Xor(m_Specific(A), m_Specific(B)))))))) {
      // X = ~(A | B)
      // Y = (C | (A ^ B)
      Value *Or = cast<BinaryOperator>(X)->getOperand(0);
      return BinaryOperator::CreateNot(Builder.CreateAnd(Or, Y));
    }
  }

  // (~A & B & C) | ... --> ...
  // (~A | B | C) | ... --> ...
  // TODO: One use checks are conservative. We just need to check that a total
  //       number of multiple used values does not exceed reduction
  //       in operations.
  if (match(Op0,
            m_OneUse(m_c_BinOp(FlippedOpcode,
                               m_BinOp(FlippedOpcode, m_Value(B), m_Value(C)),
                               m_CombineAnd(m_Value(X), m_Not(m_Value(A)))))) ||
      match(Op0, m_OneUse(m_c_BinOp(
                     FlippedOpcode,
                     m_c_BinOp(FlippedOpcode, m_Value(C),
                               m_CombineAnd(m_Value(X), m_Not(m_Value(A)))),
                     m_Value(B))))) {
    // X = ~A
    // (~A & B & C) | ~(A | B | C) --> ~(A | (B ^ C))
    // (~A | B | C) & ~(A & B & C) --> (~A | (B ^ C))
    if (match(Op1, m_OneUse(m_Not(m_c_BinOp(
                       Opcode, m_c_BinOp(Opcode, m_Specific(A), m_Specific(B)),
                       m_Specific(C))))) ||
        match(Op1, m_OneUse(m_Not(m_c_BinOp(
                       Opcode, m_c_BinOp(Opcode, m_Specific(B), m_Specific(C)),
                       m_Specific(A))))) ||
        match(Op1, m_OneUse(m_Not(m_c_BinOp(
                       Opcode, m_c_BinOp(Opcode, m_Specific(A), m_Specific(C)),
                       m_Specific(B)))))) {
      Value *Xor = Builder.CreateXor(B, C);
      return (Opcode == Instruction::Or)
                 ? BinaryOperator::CreateNot(Builder.CreateOr(Xor, A))
                 : BinaryOperator::CreateOr(Xor, X);
    }

    // (~A & B & C) | ~(A | B) --> (C | ~B) & ~A
    // (~A | B | C) & ~(A & B) --> (C & ~B) | ~A
    if (match(Op1, m_OneUse(m_Not(m_OneUse(
                       m_c_BinOp(Opcode, m_Specific(A), m_Specific(B)))))))
      return BinaryOperator::Create(
          FlippedOpcode, Builder.CreateBinOp(Opcode, C, Builder.CreateNot(B)),
          X);

    // (~A & B & C) | ~(A | C) --> (B | ~C) & ~A
    // (~A | B | C) & ~(A & C) --> (B & ~C) | ~A
    if (match(Op1, m_OneUse(m_Not(m_OneUse(
                       m_c_BinOp(Opcode, m_Specific(A), m_Specific(C)))))))
      return BinaryOperator::Create(
          FlippedOpcode, Builder.CreateBinOp(Opcode, B, Builder.CreateNot(C)),
          X);
  }

  return nullptr;
}

/// Try to reassociate a pair of binops so that values with one use only are
/// part of the same instruction. This may enable folds that are limited with
/// multi-use restrictions and makes it more likely to match other patterns that
/// are looking for a common operand.
static Instruction *reassociateForUses(BinaryOperator &BO,
                                       InstCombinerImpl::BuilderTy &Builder) {
  Instruction::BinaryOps Opcode = BO.getOpcode();
  Value *X, *Y, *Z;
  if (match(&BO,
            m_c_BinOp(Opcode, m_OneUse(m_BinOp(Opcode, m_Value(X), m_Value(Y))),
                      m_OneUse(m_Value(Z))))) {
    if (!isa<Constant>(X) && !isa<Constant>(Y) && !isa<Constant>(Z)) {
      // (X op Y) op Z --> (Y op Z) op X
      if (!X->hasOneUse()) {
        Value *YZ = Builder.CreateBinOp(Opcode, Y, Z);
        return BinaryOperator::Create(Opcode, YZ, X);
      }
      // (X op Y) op Z --> (X op Z) op Y
      if (!Y->hasOneUse()) {
        Value *XZ = Builder.CreateBinOp(Opcode, X, Z);
        return BinaryOperator::Create(Opcode, XZ, Y);
      }
    }
  }

  return nullptr;
}

// Match
// (X + C2) | C
// (X + C2) ^ C
// (X + C2) & C
// and convert to do the bitwise logic first:
// (X | C) + C2
// (X ^ C) + C2
// (X & C) + C2
// iff bits affected by logic op are lower than last bit affected by math op
static Instruction *canonicalizeLogicFirst(BinaryOperator &I,
                                           InstCombiner::BuilderTy &Builder) {
  Type *Ty = I.getType();
  Instruction::BinaryOps OpC = I.getOpcode();
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *X;
  const APInt *C, *C2;

  if (!(match(Op0, m_OneUse(m_Add(m_Value(X), m_APInt(C2)))) &&
        match(Op1, m_APInt(C))))
    return nullptr;

  unsigned Width = Ty->getScalarSizeInBits();
  unsigned LastOneMath = Width - C2->countr_zero();

  switch (OpC) {
  case Instruction::And:
    if (C->countl_one() < LastOneMath)
      return nullptr;
    break;
  case Instruction::Xor:
  case Instruction::Or:
    if (C->countl_zero() < LastOneMath)
      return nullptr;
    break;
  default:
    llvm_unreachable("Unexpected BinaryOp!");
  }

  Value *NewBinOp = Builder.CreateBinOp(OpC, X, ConstantInt::get(Ty, *C));
  return BinaryOperator::CreateWithCopiedFlags(Instruction::Add, NewBinOp,
                                               ConstantInt::get(Ty, *C2), Op0);
}

// binop(shift(ShiftedC1, ShAmt), shift(ShiftedC2, add(ShAmt, AddC))) ->
// shift(binop(ShiftedC1, shift(ShiftedC2, AddC)), ShAmt)
// where both shifts are the same and AddC is a valid shift amount.
Instruction *InstCombinerImpl::foldBinOpOfDisplacedShifts(BinaryOperator &I) {
  assert((I.isBitwiseLogicOp() || I.getOpcode() == Instruction::Add) &&
         "Unexpected opcode");

  Value *ShAmt;
  Constant *ShiftedC1, *ShiftedC2, *AddC;
  Type *Ty = I.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  if (!match(&I, m_c_BinOp(m_Shift(m_ImmConstant(ShiftedC1), m_Value(ShAmt)),
                           m_Shift(m_ImmConstant(ShiftedC2),
                                   m_AddLike(m_Deferred(ShAmt),
                                             m_ImmConstant(AddC))))))
    return nullptr;

  // Make sure the add constant is a valid shift amount.
  if (!match(AddC,
             m_SpecificInt_ICMP(ICmpInst::ICMP_ULT, APInt(BitWidth, BitWidth))))
    return nullptr;

  // Avoid constant expressions.
  auto *Op0Inst = dyn_cast<Instruction>(I.getOperand(0));
  auto *Op1Inst = dyn_cast<Instruction>(I.getOperand(1));
  if (!Op0Inst || !Op1Inst)
    return nullptr;

  // Both shifts must be the same.
  Instruction::BinaryOps ShiftOp =
      static_cast<Instruction::BinaryOps>(Op0Inst->getOpcode());
  if (ShiftOp != Op1Inst->getOpcode())
    return nullptr;

  // For adds, only left shifts are supported.
  if (I.getOpcode() == Instruction::Add && ShiftOp != Instruction::Shl)
    return nullptr;

  Value *NewC = Builder.CreateBinOp(
      I.getOpcode(), ShiftedC1, Builder.CreateBinOp(ShiftOp, ShiftedC2, AddC));
  return BinaryOperator::Create(ShiftOp, NewC, ShAmt);
}

// Fold and/or/xor with two equal intrinsic IDs:
// bitwise(fshl (A, B, ShAmt), fshl(C, D, ShAmt))
// -> fshl(bitwise(A, C), bitwise(B, D), ShAmt)
// bitwise(fshr (A, B, ShAmt), fshr(C, D, ShAmt))
// -> fshr(bitwise(A, C), bitwise(B, D), ShAmt)
// bitwise(bswap(A), bswap(B)) -> bswap(bitwise(A, B))
// bitwise(bswap(A), C) -> bswap(bitwise(A, bswap(C)))
// bitwise(bitreverse(A), bitreverse(B)) -> bitreverse(bitwise(A, B))
// bitwise(bitreverse(A), C) -> bitreverse(bitwise(A, bitreverse(C)))
static Instruction *
foldBitwiseLogicWithIntrinsics(BinaryOperator &I,
                               InstCombiner::BuilderTy &Builder) {
  assert(I.isBitwiseLogicOp() && "Should and/or/xor");
  if (!I.getOperand(0)->hasOneUse())
    return nullptr;
  IntrinsicInst *X = dyn_cast<IntrinsicInst>(I.getOperand(0));
  if (!X)
    return nullptr;

  IntrinsicInst *Y = dyn_cast<IntrinsicInst>(I.getOperand(1));
  if (Y && (!Y->hasOneUse() || X->getIntrinsicID() != Y->getIntrinsicID()))
    return nullptr;

  Intrinsic::ID IID = X->getIntrinsicID();
  const APInt *RHSC;
  // Try to match constant RHS.
  if (!Y && (!(IID == Intrinsic::bswap || IID == Intrinsic::bitreverse) ||
             !match(I.getOperand(1), m_APInt(RHSC))))
    return nullptr;

  switch (IID) {
  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    if (X->getOperand(2) != Y->getOperand(2))
      return nullptr;
    Value *NewOp0 =
        Builder.CreateBinOp(I.getOpcode(), X->getOperand(0), Y->getOperand(0));
    Value *NewOp1 =
        Builder.CreateBinOp(I.getOpcode(), X->getOperand(1), Y->getOperand(1));
    Function *F = Intrinsic::getDeclaration(I.getModule(), IID, I.getType());
    return CallInst::Create(F, {NewOp0, NewOp1, X->getOperand(2)});
  }
  case Intrinsic::bswap:
  case Intrinsic::bitreverse: {
    Value *NewOp0 = Builder.CreateBinOp(
        I.getOpcode(), X->getOperand(0),
        Y ? Y->getOperand(0)
          : ConstantInt::get(I.getType(), IID == Intrinsic::bswap
                                              ? RHSC->byteSwap()
                                              : RHSC->reverseBits()));
    Function *F = Intrinsic::getDeclaration(I.getModule(), IID, I.getType());
    return CallInst::Create(F, {NewOp0});
  }
  default:
    return nullptr;
  }
}

// Try to simplify V by replacing occurrences of Op with RepOp, but only look
// through bitwise operations. In particular, for X | Y we try to replace Y with
// 0 inside X and for X & Y we try to replace Y with -1 inside X.
// Return the simplified result of X if successful, and nullptr otherwise.
// If SimplifyOnly is true, no new instructions will be created.
static Value *simplifyAndOrWithOpReplaced(Value *V, Value *Op, Value *RepOp,
                                          bool SimplifyOnly,
                                          InstCombinerImpl &IC,
                                          unsigned Depth = 0) {
  if (Op == RepOp)
    return nullptr;

  if (V == Op)
    return RepOp;

  auto *I = dyn_cast<BinaryOperator>(V);
  if (!I || !I->isBitwiseLogicOp() || Depth >= 3)
    return nullptr;

  if (!I->hasOneUse())
    SimplifyOnly = true;

  Value *NewOp0 = simplifyAndOrWithOpReplaced(I->getOperand(0), Op, RepOp,
                                              SimplifyOnly, IC, Depth + 1);
  Value *NewOp1 = simplifyAndOrWithOpReplaced(I->getOperand(1), Op, RepOp,
                                              SimplifyOnly, IC, Depth + 1);
  if (!NewOp0 && !NewOp1)
    return nullptr;

  if (!NewOp0)
    NewOp0 = I->getOperand(0);
  if (!NewOp1)
    NewOp1 = I->getOperand(1);

  if (Value *Res = simplifyBinOp(I->getOpcode(), NewOp0, NewOp1,
                                 IC.getSimplifyQuery().getWithInstruction(I)))
    return Res;

  if (SimplifyOnly)
    return nullptr;
  return IC.Builder.CreateBinOp(I->getOpcode(), NewOp0, NewOp1);
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombinerImpl::visitAnd(BinaryOperator &I) {
  Type *Ty = I.getType();

  if (Value *V = simplifyAndInst(I.getOperand(0), I.getOperand(1),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Do this before using distributive laws to catch simple and/or/not patterns.
  if (Instruction *Xor = foldAndToXor(I, Builder))
    return Xor;

  if (Instruction *X = foldComplexAndOrPatterns(I, Builder))
    return X;

  // (A|B)&(A|C) -> A|(B&C) etc
  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  if (Instruction *R = foldBinOpShiftWithShift(I))
    return R;

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);

  Value *X, *Y;
  const APInt *C;
  if ((match(Op0, m_OneUse(m_LogicalShift(m_One(), m_Value(X)))) ||
       (match(Op0, m_OneUse(m_Shl(m_APInt(C), m_Value(X)))) && (*C)[0])) &&
      match(Op1, m_One())) {
    // (1 >> X) & 1 --> zext(X == 0)
    // (C << X) & 1 --> zext(X == 0), when C is odd
    Value *IsZero = Builder.CreateICmpEQ(X, ConstantInt::get(Ty, 0));
    return new ZExtInst(IsZero, Ty);
  }

  // (-(X & 1)) & Y --> (X & 1) == 0 ? 0 : Y
  Value *Neg;
  if (match(&I,
            m_c_And(m_CombineAnd(m_Value(Neg),
                                 m_OneUse(m_Neg(m_And(m_Value(), m_One())))),
                    m_Value(Y)))) {
    Value *Cmp = Builder.CreateIsNull(Neg);
    return SelectInst::Create(Cmp, ConstantInt::getNullValue(Ty), Y);
  }

  // Canonicalize:
  // (X +/- Y) & Y --> ~X & Y when Y is a power of 2.
  if (match(&I, m_c_And(m_Value(Y), m_OneUse(m_CombineOr(
                                        m_c_Add(m_Value(X), m_Deferred(Y)),
                                        m_Sub(m_Value(X), m_Deferred(Y)))))) &&
      isKnownToBeAPowerOfTwo(Y, /*OrZero*/ true, /*Depth*/ 0, &I))
    return BinaryOperator::CreateAnd(Builder.CreateNot(X), Y);

  if (match(Op1, m_APInt(C))) {
    const APInt *XorC;
    if (match(Op0, m_OneUse(m_Xor(m_Value(X), m_APInt(XorC))))) {
      // (X ^ C1) & C2 --> (X & C2) ^ (C1&C2)
      Constant *NewC = ConstantInt::get(Ty, *C & *XorC);
      Value *And = Builder.CreateAnd(X, Op1);
      And->takeName(Op0);
      return BinaryOperator::CreateXor(And, NewC);
    }

    const APInt *OrC;
    if (match(Op0, m_OneUse(m_Or(m_Value(X), m_APInt(OrC))))) {
      // (X | C1) & C2 --> (X & C2^(C1&C2)) | (C1&C2)
      // NOTE: This reduces the number of bits set in the & mask, which
      // can expose opportunities for store narrowing for scalars.
      // NOTE: SimplifyDemandedBits should have already removed bits from C1
      // that aren't set in C2. Meaning we can replace (C1&C2) with C1 in
      // above, but this feels safer.
      APInt Together = *C & *OrC;
      Value *And = Builder.CreateAnd(X, ConstantInt::get(Ty, Together ^ *C));
      And->takeName(Op0);
      return BinaryOperator::CreateOr(And, ConstantInt::get(Ty, Together));
    }

    unsigned Width = Ty->getScalarSizeInBits();
    const APInt *ShiftC;
    if (match(Op0, m_OneUse(m_SExt(m_AShr(m_Value(X), m_APInt(ShiftC))))) &&
        ShiftC->ult(Width)) {
      if (*C == APInt::getLowBitsSet(Width, Width - ShiftC->getZExtValue())) {
        // We are clearing high bits that were potentially set by sext+ashr:
        // and (sext (ashr X, ShiftC)), C --> lshr (sext X), ShiftC
        Value *Sext = Builder.CreateSExt(X, Ty);
        Constant *ShAmtC = ConstantInt::get(Ty, ShiftC->zext(Width));
        return BinaryOperator::CreateLShr(Sext, ShAmtC);
      }
    }

    // If this 'and' clears the sign-bits added by ashr, replace with lshr:
    // and (ashr X, ShiftC), C --> lshr X, ShiftC
    if (match(Op0, m_AShr(m_Value(X), m_APInt(ShiftC))) && ShiftC->ult(Width) &&
        C->isMask(Width - ShiftC->getZExtValue()))
      return BinaryOperator::CreateLShr(X, ConstantInt::get(Ty, *ShiftC));

    const APInt *AddC;
    if (match(Op0, m_Add(m_Value(X), m_APInt(AddC)))) {
      // If we are masking the result of the add down to exactly one bit and
      // the constant we are adding has no bits set below that bit, then the
      // add is flipping a single bit. Example:
      // (X + 4) & 4 --> (X & 4) ^ 4
      if (Op0->hasOneUse() && C->isPowerOf2() && (*AddC & (*C - 1)) == 0) {
        assert((*C & *AddC) != 0 && "Expected common bit");
        Value *NewAnd = Builder.CreateAnd(X, Op1);
        return BinaryOperator::CreateXor(NewAnd, Op1);
      }
    }

    // ((C1 OP zext(X)) & C2) -> zext((C1 OP X) & C2) if C2 fits in the
    // bitwidth of X and OP behaves well when given trunc(C1) and X.
    auto isNarrowableBinOpcode = [](BinaryOperator *B) {
      switch (B->getOpcode()) {
      case Instruction::Xor:
      case Instruction::Or:
      case Instruction::Mul:
      case Instruction::Add:
      case Instruction::Sub:
        return true;
      default:
        return false;
      }
    };
    BinaryOperator *BO;
    if (match(Op0, m_OneUse(m_BinOp(BO))) && isNarrowableBinOpcode(BO)) {
      Instruction::BinaryOps BOpcode = BO->getOpcode();
      Value *X;
      const APInt *C1;
      // TODO: The one-use restrictions could be relaxed a little if the AND
      // is going to be removed.
      // Try to narrow the 'and' and a binop with constant operand:
      // and (bo (zext X), C1), C --> zext (and (bo X, TruncC1), TruncC)
      if (match(BO, m_c_BinOp(m_OneUse(m_ZExt(m_Value(X))), m_APInt(C1))) &&
          C->isIntN(X->getType()->getScalarSizeInBits())) {
        unsigned XWidth = X->getType()->getScalarSizeInBits();
        Constant *TruncC1 = ConstantInt::get(X->getType(), C1->trunc(XWidth));
        Value *BinOp = isa<ZExtInst>(BO->getOperand(0))
                           ? Builder.CreateBinOp(BOpcode, X, TruncC1)
                           : Builder.CreateBinOp(BOpcode, TruncC1, X);
        Constant *TruncC = ConstantInt::get(X->getType(), C->trunc(XWidth));
        Value *And = Builder.CreateAnd(BinOp, TruncC);
        return new ZExtInst(And, Ty);
      }

      // Similar to above: if the mask matches the zext input width, then the
      // 'and' can be eliminated, so we can truncate the other variable op:
      // and (bo (zext X), Y), C --> zext (bo X, (trunc Y))
      if (isa<Instruction>(BO->getOperand(0)) &&
          match(BO->getOperand(0), m_OneUse(m_ZExt(m_Value(X)))) &&
          C->isMask(X->getType()->getScalarSizeInBits())) {
        Y = BO->getOperand(1);
        Value *TrY = Builder.CreateTrunc(Y, X->getType(), Y->getName() + ".tr");
        Value *NewBO =
            Builder.CreateBinOp(BOpcode, X, TrY, BO->getName() + ".narrow");
        return new ZExtInst(NewBO, Ty);
      }
      // and (bo Y, (zext X)), C --> zext (bo (trunc Y), X)
      if (isa<Instruction>(BO->getOperand(1)) &&
          match(BO->getOperand(1), m_OneUse(m_ZExt(m_Value(X)))) &&
          C->isMask(X->getType()->getScalarSizeInBits())) {
        Y = BO->getOperand(0);
        Value *TrY = Builder.CreateTrunc(Y, X->getType(), Y->getName() + ".tr");
        Value *NewBO =
            Builder.CreateBinOp(BOpcode, TrY, X, BO->getName() + ".narrow");
        return new ZExtInst(NewBO, Ty);
      }
    }

    // This is intentionally placed after the narrowing transforms for
    // efficiency (transform directly to the narrow logic op if possible).
    // If the mask is only needed on one incoming arm, push the 'and' op up.
    if (match(Op0, m_OneUse(m_Xor(m_Value(X), m_Value(Y)))) ||
        match(Op0, m_OneUse(m_Or(m_Value(X), m_Value(Y))))) {
      APInt NotAndMask(~(*C));
      BinaryOperator::BinaryOps BinOp = cast<BinaryOperator>(Op0)->getOpcode();
      if (MaskedValueIsZero(X, NotAndMask, 0, &I)) {
        // Not masking anything out for the LHS, move mask to RHS.
        // and ({x}or X, Y), C --> {x}or X, (and Y, C)
        Value *NewRHS = Builder.CreateAnd(Y, Op1, Y->getName() + ".masked");
        return BinaryOperator::Create(BinOp, X, NewRHS);
      }
      if (!isa<Constant>(Y) && MaskedValueIsZero(Y, NotAndMask, 0, &I)) {
        // Not masking anything out for the RHS, move mask to LHS.
        // and ({x}or X, Y), C --> {x}or (and X, C), Y
        Value *NewLHS = Builder.CreateAnd(X, Op1, X->getName() + ".masked");
        return BinaryOperator::Create(BinOp, NewLHS, Y);
      }
    }

    // When the mask is a power-of-2 constant and op0 is a shifted-power-of-2
    // constant, test if the shift amount equals the offset bit index:
    // (ShiftC << X) & C --> X == (log2(C) - log2(ShiftC)) ? C : 0
    // (ShiftC >> X) & C --> X == (log2(ShiftC) - log2(C)) ? C : 0
    if (C->isPowerOf2() &&
        match(Op0, m_OneUse(m_LogicalShift(m_Power2(ShiftC), m_Value(X))))) {
      int Log2ShiftC = ShiftC->exactLogBase2();
      int Log2C = C->exactLogBase2();
      bool IsShiftLeft =
         cast<BinaryOperator>(Op0)->getOpcode() == Instruction::Shl;
      int BitNum = IsShiftLeft ? Log2C - Log2ShiftC : Log2ShiftC - Log2C;
      assert(BitNum >= 0 && "Expected demanded bits to handle impossible mask");
      Value *Cmp = Builder.CreateICmpEQ(X, ConstantInt::get(Ty, BitNum));
      return SelectInst::Create(Cmp, ConstantInt::get(Ty, *C),
                                ConstantInt::getNullValue(Ty));
    }

    Constant *C1, *C2;
    const APInt *C3 = C;
    Value *X;
    if (C3->isPowerOf2()) {
      Constant *Log2C3 = ConstantInt::get(Ty, C3->countr_zero());
      if (match(Op0, m_OneUse(m_LShr(m_Shl(m_ImmConstant(C1), m_Value(X)),
                                     m_ImmConstant(C2)))) &&
          match(C1, m_Power2())) {
        Constant *Log2C1 = ConstantExpr::getExactLogBase2(C1);
        Constant *LshrC = ConstantExpr::getAdd(C2, Log2C3);
        KnownBits KnownLShrc = computeKnownBits(LshrC, 0, nullptr);
        if (KnownLShrc.getMaxValue().ult(Width)) {
          // iff C1,C3 is pow2 and C2 + cttz(C3) < BitWidth:
          // ((C1 << X) >> C2) & C3 -> X == (cttz(C3)+C2-cttz(C1)) ? C3 : 0
          Constant *CmpC = ConstantExpr::getSub(LshrC, Log2C1);
          Value *Cmp = Builder.CreateICmpEQ(X, CmpC);
          return SelectInst::Create(Cmp, ConstantInt::get(Ty, *C3),
                                    ConstantInt::getNullValue(Ty));
        }
      }

      if (match(Op0, m_OneUse(m_Shl(m_LShr(m_ImmConstant(C1), m_Value(X)),
                                    m_ImmConstant(C2)))) &&
          match(C1, m_Power2())) {
        Constant *Log2C1 = ConstantExpr::getExactLogBase2(C1);
        Constant *Cmp =
            ConstantFoldCompareInstOperands(ICmpInst::ICMP_ULT, Log2C3, C2, DL);
        if (Cmp && Cmp->isZeroValue()) {
          // iff C1,C3 is pow2 and Log2(C3) >= C2:
          // ((C1 >> X) << C2) & C3 -> X == (cttz(C1)+C2-cttz(C3)) ? C3 : 0
          Constant *ShlC = ConstantExpr::getAdd(C2, Log2C1);
          Constant *CmpC = ConstantExpr::getSub(ShlC, Log2C3);
          Value *Cmp = Builder.CreateICmpEQ(X, CmpC);
          return SelectInst::Create(Cmp, ConstantInt::get(Ty, *C3),
                                    ConstantInt::getNullValue(Ty));
        }
      }
    }
  }

  // If we are clearing the sign bit of a floating-point value, convert this to
  // fabs, then cast back to integer.
  //
  // This is a generous interpretation for noimplicitfloat, this is not a true
  // floating-point operation.
  //
  // Assumes any IEEE-represented type has the sign bit in the high bit.
  // TODO: Unify with APInt matcher. This version allows undef unlike m_APInt
  Value *CastOp;
  if (match(Op0, m_ElementWiseBitCast(m_Value(CastOp))) &&
      match(Op1, m_MaxSignedValue()) &&
      !Builder.GetInsertBlock()->getParent()->hasFnAttribute(
          Attribute::NoImplicitFloat)) {
    Type *EltTy = CastOp->getType()->getScalarType();
    if (EltTy->isFloatingPointTy() && EltTy->isIEEE()) {
      Value *FAbs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, CastOp);
      return new BitCastInst(FAbs, I.getType());
    }
  }

  // and(shl(zext(X), Y), SignMask) -> and(sext(X), SignMask)
  // where Y is a valid shift amount.
  if (match(&I, m_And(m_OneUse(m_Shl(m_ZExt(m_Value(X)), m_Value(Y))),
                      m_SignMask())) &&
      match(Y, m_SpecificInt_ICMP(
                   ICmpInst::Predicate::ICMP_EQ,
                   APInt(Ty->getScalarSizeInBits(),
                         Ty->getScalarSizeInBits() -
                             X->getType()->getScalarSizeInBits())))) {
    auto *SExt = Builder.CreateSExt(X, Ty, X->getName() + ".signext");
    return BinaryOperator::CreateAnd(SExt, Op1);
  }

  if (Instruction *Z = narrowMaskedBinOp(I))
    return Z;

  if (I.getType()->isIntOrIntVectorTy(1)) {
    if (auto *SI0 = dyn_cast<SelectInst>(Op0)) {
      if (auto *R =
              foldAndOrOfSelectUsingImpliedCond(Op1, *SI0, /* IsAnd */ true))
        return R;
    }
    if (auto *SI1 = dyn_cast<SelectInst>(Op1)) {
      if (auto *R =
              foldAndOrOfSelectUsingImpliedCond(Op0, *SI1, /* IsAnd */ true))
        return R;
    }
  }

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  if (Instruction *DeMorgan = matchDeMorgansLaws(I, *this))
    return DeMorgan;

  {
    Value *A, *B, *C;
    // A & ~(A ^ B) --> A & B
    if (match(Op1, m_Not(m_c_Xor(m_Specific(Op0), m_Value(B)))))
      return BinaryOperator::CreateAnd(Op0, B);
    // ~(A ^ B) & A --> A & B
    if (match(Op0, m_Not(m_c_Xor(m_Specific(Op1), m_Value(B)))))
      return BinaryOperator::CreateAnd(Op1, B);

    // (A ^ B) & ((B ^ C) ^ A) -> (A ^ B) & ~C
    if (match(Op0, m_Xor(m_Value(A), m_Value(B))) &&
        match(Op1, m_Xor(m_Xor(m_Specific(B), m_Value(C)), m_Specific(A)))) {
      Value *NotC = Op1->hasOneUse()
                        ? Builder.CreateNot(C)
                        : getFreelyInverted(C, C->hasOneUse(), &Builder);
      if (NotC != nullptr)
        return BinaryOperator::CreateAnd(Op0, NotC);
    }

    // ((A ^ C) ^ B) & (B ^ A) -> (B ^ A) & ~C
    if (match(Op0, m_Xor(m_Xor(m_Value(A), m_Value(C)), m_Value(B))) &&
        match(Op1, m_Xor(m_Specific(B), m_Specific(A)))) {
      Value *NotC = Op0->hasOneUse()
                        ? Builder.CreateNot(C)
                        : getFreelyInverted(C, C->hasOneUse(), &Builder);
      if (NotC != nullptr)
        return BinaryOperator::CreateAnd(Op1, Builder.CreateNot(C));
    }

    // (A | B) & (~A ^ B) -> A & B
    // (A | B) & (B ^ ~A) -> A & B
    // (B | A) & (~A ^ B) -> A & B
    // (B | A) & (B ^ ~A) -> A & B
    if (match(Op1, m_c_Xor(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op0, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);

    // (~A ^ B) & (A | B) -> A & B
    // (~A ^ B) & (B | A) -> A & B
    // (B ^ ~A) & (A | B) -> A & B
    // (B ^ ~A) & (B | A) -> A & B
    if (match(Op0, m_c_Xor(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op1, m_c_Or(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(A, B);

    // (~A | B) & (A ^ B) -> ~A & B
    // (~A | B) & (B ^ A) -> ~A & B
    // (B | ~A) & (A ^ B) -> ~A & B
    // (B | ~A) & (B ^ A) -> ~A & B
    if (match(Op0, m_c_Or(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op1, m_c_Xor(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(Builder.CreateNot(A), B);

    // (A ^ B) & (~A | B) -> ~A & B
    // (B ^ A) & (~A | B) -> ~A & B
    // (A ^ B) & (B | ~A) -> ~A & B
    // (B ^ A) & (B | ~A) -> ~A & B
    if (match(Op1, m_c_Or(m_Not(m_Value(A)), m_Value(B))) &&
        match(Op0, m_c_Xor(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateAnd(Builder.CreateNot(A), B);
  }

  {
    ICmpInst *LHS = dyn_cast<ICmpInst>(Op0);
    ICmpInst *RHS = dyn_cast<ICmpInst>(Op1);
    if (LHS && RHS)
      if (Value *Res = foldAndOrOfICmps(LHS, RHS, I, /* IsAnd */ true))
        return replaceInstUsesWith(I, Res);

    // TODO: Make this recursive; it's a little tricky because an arbitrary
    // number of 'and' instructions might have to be created.
    if (LHS && match(Op1, m_OneUse(m_LogicalAnd(m_Value(X), m_Value(Y))))) {
      bool IsLogical = isa<SelectInst>(Op1);
      // LHS & (X && Y) --> (LHS && X) && Y
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res =
                foldAndOrOfICmps(LHS, Cmp, I, /* IsAnd */ true, IsLogical))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalAnd(Res, Y)
                                            : Builder.CreateAnd(Res, Y));
      // LHS & (X && Y) --> X && (LHS & Y)
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOrOfICmps(LHS, Cmp, I, /* IsAnd */ true,
                                          /* IsLogical */ false))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalAnd(X, Res)
                                            : Builder.CreateAnd(X, Res));
    }
    if (RHS && match(Op0, m_OneUse(m_LogicalAnd(m_Value(X), m_Value(Y))))) {
      bool IsLogical = isa<SelectInst>(Op0);
      // (X && Y) & RHS --> (X && RHS) && Y
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res =
                foldAndOrOfICmps(Cmp, RHS, I, /* IsAnd */ true, IsLogical))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalAnd(Res, Y)
                                            : Builder.CreateAnd(Res, Y));
      // (X && Y) & RHS --> X && (Y & RHS)
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOrOfICmps(Cmp, RHS, I, /* IsAnd */ true,
                                          /* IsLogical */ false))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalAnd(X, Res)
                                            : Builder.CreateAnd(X, Res));
    }
  }

  if (FCmpInst *LHS = dyn_cast<FCmpInst>(I.getOperand(0)))
    if (FCmpInst *RHS = dyn_cast<FCmpInst>(I.getOperand(1)))
      if (Value *Res = foldLogicOfFCmps(LHS, RHS, /*IsAnd*/ true))
        return replaceInstUsesWith(I, Res);

  if (Instruction *FoldedFCmps = reassociateFCmps(I, Builder))
    return FoldedFCmps;

  if (Instruction *CastedAnd = foldCastedBitwiseLogic(I))
    return CastedAnd;

  if (Instruction *Sel = foldBinopOfSextBoolToSelect(I))
    return Sel;

  // and(sext(A), B) / and(B, sext(A)) --> A ? B : 0, where A is i1 or <N x i1>.
  // TODO: Move this into foldBinopOfSextBoolToSelect as a more generalized fold
  //       with binop identity constant. But creating a select with non-constant
  //       arm may not be reversible due to poison semantics. Is that a good
  //       canonicalization?
  Value *A, *B;
  if (match(&I, m_c_And(m_SExt(m_Value(A)), m_Value(B))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, B, Constant::getNullValue(Ty));

  // Similarly, a 'not' of the bool translates to a swap of the select arms:
  // ~sext(A) & B / B & ~sext(A) --> A ? 0 : B
  if (match(&I, m_c_And(m_Not(m_SExt(m_Value(A))), m_Value(B))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, Constant::getNullValue(Ty), B);

  // and(zext(A), B) -> A ? (B & 1) : 0
  if (match(&I, m_c_And(m_OneUse(m_ZExt(m_Value(A))), m_Value(B))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, Builder.CreateAnd(B, ConstantInt::get(Ty, 1)),
                              Constant::getNullValue(Ty));

  // (-1 + A) & B --> A ? 0 : B where A is 0/1.
  if (match(&I, m_c_And(m_OneUse(m_Add(m_ZExtOrSelf(m_Value(A)), m_AllOnes())),
                        m_Value(B)))) {
    if (A->getType()->isIntOrIntVectorTy(1))
      return SelectInst::Create(A, Constant::getNullValue(Ty), B);
    if (computeKnownBits(A, /* Depth */ 0, &I).countMaxActiveBits() <= 1) {
      return SelectInst::Create(
          Builder.CreateICmpEQ(A, Constant::getNullValue(A->getType())), B,
          Constant::getNullValue(Ty));
    }
  }

  // (iN X s>> (N-1)) & Y --> (X s< 0) ? Y : 0 -- with optional sext
  if (match(&I, m_c_And(m_OneUse(m_SExtOrSelf(
                            m_AShr(m_Value(X), m_APIntAllowPoison(C)))),
                        m_Value(Y))) &&
      *C == X->getType()->getScalarSizeInBits() - 1) {
    Value *IsNeg = Builder.CreateIsNeg(X, "isneg");
    return SelectInst::Create(IsNeg, Y, ConstantInt::getNullValue(Ty));
  }
  // If there's a 'not' of the shifted value, swap the select operands:
  // ~(iN X s>> (N-1)) & Y --> (X s< 0) ? 0 : Y -- with optional sext
  if (match(&I, m_c_And(m_OneUse(m_SExtOrSelf(
                            m_Not(m_AShr(m_Value(X), m_APIntAllowPoison(C))))),
                        m_Value(Y))) &&
      *C == X->getType()->getScalarSizeInBits() - 1) {
    Value *IsNeg = Builder.CreateIsNeg(X, "isneg");
    return SelectInst::Create(IsNeg, ConstantInt::getNullValue(Ty), Y);
  }

  // (~x) & y  -->  ~(x | (~y))  iff that gets rid of inversions
  if (sinkNotIntoOtherHandOfLogicalOp(I))
    return &I;

  // An and recurrence w/loop invariant step is equivelent to (and start, step)
  PHINode *PN = nullptr;
  Value *Start = nullptr, *Step = nullptr;
  if (matchSimpleRecurrence(&I, PN, Start, Step) && DT.dominates(Step, PN))
    return replaceInstUsesWith(I, Builder.CreateAnd(Start, Step));

  if (Instruction *R = reassociateForUses(I, Builder))
    return R;

  if (Instruction *Canonicalized = canonicalizeLogicFirst(I, Builder))
    return Canonicalized;

  if (Instruction *Folded = foldLogicOfIsFPClass(I, Op0, Op1))
    return Folded;

  if (Instruction *Res = foldBinOpOfDisplacedShifts(I))
    return Res;

  if (Instruction *Res = foldBitwiseLogicWithIntrinsics(I, Builder))
    return Res;

  if (Value *V =
          simplifyAndOrWithOpReplaced(Op0, Op1, Constant::getAllOnesValue(Ty),
                                      /*SimplifyOnly*/ false, *this))
    return BinaryOperator::CreateAnd(V, Op1);
  if (Value *V =
          simplifyAndOrWithOpReplaced(Op1, Op0, Constant::getAllOnesValue(Ty),
                                      /*SimplifyOnly*/ false, *this))
    return BinaryOperator::CreateAnd(Op0, V);

  return nullptr;
}

Instruction *InstCombinerImpl::matchBSwapOrBitReverse(Instruction &I,
                                                      bool MatchBSwaps,
                                                      bool MatchBitReversals) {
  SmallVector<Instruction *, 4> Insts;
  if (!recognizeBSwapOrBitReverseIdiom(&I, MatchBSwaps, MatchBitReversals,
                                       Insts))
    return nullptr;
  Instruction *LastInst = Insts.pop_back_val();
  LastInst->removeFromParent();

  for (auto *Inst : Insts)
    Worklist.push(Inst);
  return LastInst;
}

std::optional<std::pair<Intrinsic::ID, SmallVector<Value *, 3>>>
InstCombinerImpl::convertOrOfShiftsToFunnelShift(Instruction &Or) {
  // TODO: Can we reduce the code duplication between this and the related
  // rotate matching code under visitSelect and visitTrunc?
  assert(Or.getOpcode() == BinaryOperator::Or && "Expecting or instruction");

  unsigned Width = Or.getType()->getScalarSizeInBits();

  Instruction *Or0, *Or1;
  if (!match(Or.getOperand(0), m_Instruction(Or0)) ||
      !match(Or.getOperand(1), m_Instruction(Or1)))
    return std::nullopt;

  bool IsFshl = true; // Sub on LSHR.
  SmallVector<Value *, 3> FShiftArgs;

  // First, find an or'd pair of opposite shifts:
  // or (lshr ShVal0, ShAmt0), (shl ShVal1, ShAmt1)
  if (isa<BinaryOperator>(Or0) && isa<BinaryOperator>(Or1)) {
    Value *ShVal0, *ShVal1, *ShAmt0, *ShAmt1;
    if (!match(Or0,
               m_OneUse(m_LogicalShift(m_Value(ShVal0), m_Value(ShAmt0)))) ||
        !match(Or1,
               m_OneUse(m_LogicalShift(m_Value(ShVal1), m_Value(ShAmt1)))) ||
        Or0->getOpcode() == Or1->getOpcode())
      return std::nullopt;

    // Canonicalize to or(shl(ShVal0, ShAmt0), lshr(ShVal1, ShAmt1)).
    if (Or0->getOpcode() == BinaryOperator::LShr) {
      std::swap(Or0, Or1);
      std::swap(ShVal0, ShVal1);
      std::swap(ShAmt0, ShAmt1);
    }
    assert(Or0->getOpcode() == BinaryOperator::Shl &&
           Or1->getOpcode() == BinaryOperator::LShr &&
           "Illegal or(shift,shift) pair");

    // Match the shift amount operands for a funnel shift pattern. This always
    // matches a subtraction on the R operand.
    auto matchShiftAmount = [&](Value *L, Value *R, unsigned Width) -> Value * {
      // Check for constant shift amounts that sum to the bitwidth.
      const APInt *LI, *RI;
      if (match(L, m_APIntAllowPoison(LI)) && match(R, m_APIntAllowPoison(RI)))
        if (LI->ult(Width) && RI->ult(Width) && (*LI + *RI) == Width)
          return ConstantInt::get(L->getType(), *LI);

      Constant *LC, *RC;
      if (match(L, m_Constant(LC)) && match(R, m_Constant(RC)) &&
          match(L,
                m_SpecificInt_ICMP(ICmpInst::ICMP_ULT, APInt(Width, Width))) &&
          match(R,
                m_SpecificInt_ICMP(ICmpInst::ICMP_ULT, APInt(Width, Width))) &&
          match(ConstantExpr::getAdd(LC, RC), m_SpecificIntAllowPoison(Width)))
        return ConstantExpr::mergeUndefsWith(LC, RC);

      // (shl ShVal, X) | (lshr ShVal, (Width - x)) iff X < Width.
      // We limit this to X < Width in case the backend re-expands the
      // intrinsic, and has to reintroduce a shift modulo operation (InstCombine
      // might remove it after this fold). This still doesn't guarantee that the
      // final codegen will match this original pattern.
      if (match(R, m_OneUse(m_Sub(m_SpecificInt(Width), m_Specific(L))))) {
        KnownBits KnownL = computeKnownBits(L, /*Depth*/ 0, &Or);
        return KnownL.getMaxValue().ult(Width) ? L : nullptr;
      }

      // For non-constant cases, the following patterns currently only work for
      // rotation patterns.
      // TODO: Add general funnel-shift compatible patterns.
      if (ShVal0 != ShVal1)
        return nullptr;

      // For non-constant cases we don't support non-pow2 shift masks.
      // TODO: Is it worth matching urem as well?
      if (!isPowerOf2_32(Width))
        return nullptr;

      // The shift amount may be masked with negation:
      // (shl ShVal, (X & (Width - 1))) | (lshr ShVal, ((-X) & (Width - 1)))
      Value *X;
      unsigned Mask = Width - 1;
      if (match(L, m_And(m_Value(X), m_SpecificInt(Mask))) &&
          match(R, m_And(m_Neg(m_Specific(X)), m_SpecificInt(Mask))))
        return X;

      // (shl ShVal, X) | (lshr ShVal, ((-X) & (Width - 1)))
      if (match(R, m_And(m_Neg(m_Specific(L)), m_SpecificInt(Mask))))
        return L;

      // Similar to above, but the shift amount may be extended after masking,
      // so return the extended value as the parameter for the intrinsic.
      if (match(L, m_ZExt(m_And(m_Value(X), m_SpecificInt(Mask)))) &&
          match(R,
                m_And(m_Neg(m_ZExt(m_And(m_Specific(X), m_SpecificInt(Mask)))),
                      m_SpecificInt(Mask))))
        return L;

      if (match(L, m_ZExt(m_And(m_Value(X), m_SpecificInt(Mask)))) &&
          match(R, m_ZExt(m_And(m_Neg(m_Specific(X)), m_SpecificInt(Mask)))))
        return L;

      return nullptr;
    };

    Value *ShAmt = matchShiftAmount(ShAmt0, ShAmt1, Width);
    if (!ShAmt) {
      ShAmt = matchShiftAmount(ShAmt1, ShAmt0, Width);
      IsFshl = false; // Sub on SHL.
    }
    if (!ShAmt)
      return std::nullopt;

    FShiftArgs = {ShVal0, ShVal1, ShAmt};
  } else if (isa<ZExtInst>(Or0) || isa<ZExtInst>(Or1)) {
    // If there are two 'or' instructions concat variables in opposite order:
    //
    // Slot1 and Slot2 are all zero bits.
    // | Slot1 | Low | Slot2 | High |
    // LowHigh = or (shl (zext Low), ZextLowShlAmt), (zext High)
    // | Slot2 | High | Slot1 | Low |
    // HighLow = or (shl (zext High), ZextHighShlAmt), (zext Low)
    //
    // the latter 'or' can be safely convert to
    // -> HighLow = fshl LowHigh, LowHigh, ZextHighShlAmt
    // if ZextLowShlAmt + ZextHighShlAmt == Width.
    if (!isa<ZExtInst>(Or1))
      std::swap(Or0, Or1);

    Value *High, *ZextHigh, *Low;
    const APInt *ZextHighShlAmt;
    if (!match(Or0,
               m_OneUse(m_Shl(m_Value(ZextHigh), m_APInt(ZextHighShlAmt)))))
      return std::nullopt;

    if (!match(Or1, m_ZExt(m_Value(Low))) ||
        !match(ZextHigh, m_ZExt(m_Value(High))))
      return std::nullopt;

    unsigned HighSize = High->getType()->getScalarSizeInBits();
    unsigned LowSize = Low->getType()->getScalarSizeInBits();
    // Make sure High does not overlap with Low and most significant bits of
    // High aren't shifted out.
    if (ZextHighShlAmt->ult(LowSize) || ZextHighShlAmt->ugt(Width - HighSize))
      return std::nullopt;

    for (User *U : ZextHigh->users()) {
      Value *X, *Y;
      if (!match(U, m_Or(m_Value(X), m_Value(Y))))
        continue;

      if (!isa<ZExtInst>(Y))
        std::swap(X, Y);

      const APInt *ZextLowShlAmt;
      if (!match(X, m_Shl(m_Specific(Or1), m_APInt(ZextLowShlAmt))) ||
          !match(Y, m_Specific(ZextHigh)) || !DT.dominates(U, &Or))
        continue;

      // HighLow is good concat. If sum of two shifts amount equals to Width,
      // LowHigh must also be a good concat.
      if (*ZextLowShlAmt + *ZextHighShlAmt != Width)
        continue;

      // Low must not overlap with High and most significant bits of Low must
      // not be shifted out.
      assert(ZextLowShlAmt->uge(HighSize) &&
             ZextLowShlAmt->ule(Width - LowSize) && "Invalid concat");

      FShiftArgs = {U, U, ConstantInt::get(Or0->getType(), *ZextHighShlAmt)};
      break;
    }
  }

  if (FShiftArgs.empty())
    return std::nullopt;

  Intrinsic::ID IID = IsFshl ? Intrinsic::fshl : Intrinsic::fshr;
  return std::make_pair(IID, FShiftArgs);
}

/// Match UB-safe variants of the funnel shift intrinsic.
static Instruction *matchFunnelShift(Instruction &Or, InstCombinerImpl &IC) {
  if (auto Opt = IC.convertOrOfShiftsToFunnelShift(Or)) {
    auto [IID, FShiftArgs] = *Opt;
    Function *F = Intrinsic::getDeclaration(Or.getModule(), IID, Or.getType());
    return CallInst::Create(F, FShiftArgs);
  }

  return nullptr;
}

/// Attempt to combine or(zext(x),shl(zext(y),bw/2) concat packing patterns.
static Instruction *matchOrConcat(Instruction &Or,
                                  InstCombiner::BuilderTy &Builder) {
  assert(Or.getOpcode() == Instruction::Or && "bswap requires an 'or'");
  Value *Op0 = Or.getOperand(0), *Op1 = Or.getOperand(1);
  Type *Ty = Or.getType();

  unsigned Width = Ty->getScalarSizeInBits();
  if ((Width & 1) != 0)
    return nullptr;
  unsigned HalfWidth = Width / 2;

  // Canonicalize zext (lower half) to LHS.
  if (!isa<ZExtInst>(Op0))
    std::swap(Op0, Op1);

  // Find lower/upper half.
  Value *LowerSrc, *ShlVal, *UpperSrc;
  const APInt *C;
  if (!match(Op0, m_OneUse(m_ZExt(m_Value(LowerSrc)))) ||
      !match(Op1, m_OneUse(m_Shl(m_Value(ShlVal), m_APInt(C)))) ||
      !match(ShlVal, m_OneUse(m_ZExt(m_Value(UpperSrc)))))
    return nullptr;
  if (*C != HalfWidth || LowerSrc->getType() != UpperSrc->getType() ||
      LowerSrc->getType()->getScalarSizeInBits() != HalfWidth)
    return nullptr;

  auto ConcatIntrinsicCalls = [&](Intrinsic::ID id, Value *Lo, Value *Hi) {
    Value *NewLower = Builder.CreateZExt(Lo, Ty);
    Value *NewUpper = Builder.CreateZExt(Hi, Ty);
    NewUpper = Builder.CreateShl(NewUpper, HalfWidth);
    Value *BinOp = Builder.CreateOr(NewLower, NewUpper);
    Function *F = Intrinsic::getDeclaration(Or.getModule(), id, Ty);
    return Builder.CreateCall(F, BinOp);
  };

  // BSWAP: Push the concat down, swapping the lower/upper sources.
  // concat(bswap(x),bswap(y)) -> bswap(concat(x,y))
  Value *LowerBSwap, *UpperBSwap;
  if (match(LowerSrc, m_BSwap(m_Value(LowerBSwap))) &&
      match(UpperSrc, m_BSwap(m_Value(UpperBSwap))))
    return ConcatIntrinsicCalls(Intrinsic::bswap, UpperBSwap, LowerBSwap);

  // BITREVERSE: Push the concat down, swapping the lower/upper sources.
  // concat(bitreverse(x),bitreverse(y)) -> bitreverse(concat(x,y))
  Value *LowerBRev, *UpperBRev;
  if (match(LowerSrc, m_BitReverse(m_Value(LowerBRev))) &&
      match(UpperSrc, m_BitReverse(m_Value(UpperBRev))))
    return ConcatIntrinsicCalls(Intrinsic::bitreverse, UpperBRev, LowerBRev);

  return nullptr;
}

/// If all elements of two constant vectors are 0/-1 and inverses, return true.
static bool areInverseVectorBitmasks(Constant *C1, Constant *C2) {
  unsigned NumElts = cast<FixedVectorType>(C1->getType())->getNumElements();
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *EltC1 = C1->getAggregateElement(i);
    Constant *EltC2 = C2->getAggregateElement(i);
    if (!EltC1 || !EltC2)
      return false;

    // One element must be all ones, and the other must be all zeros.
    if (!((match(EltC1, m_Zero()) && match(EltC2, m_AllOnes())) ||
          (match(EltC2, m_Zero()) && match(EltC1, m_AllOnes()))))
      return false;
  }
  return true;
}

/// We have an expression of the form (A & C) | (B & D). If A is a scalar or
/// vector composed of all-zeros or all-ones values and is the bitwise 'not' of
/// B, it can be used as the condition operand of a select instruction.
/// We will detect (A & C) | ~(B | D) when the flag ABIsTheSame enabled.
Value *InstCombinerImpl::getSelectCondition(Value *A, Value *B,
                                            bool ABIsTheSame) {
  // We may have peeked through bitcasts in the caller.
  // Exit immediately if we don't have (vector) integer types.
  Type *Ty = A->getType();
  if (!Ty->isIntOrIntVectorTy() || !B->getType()->isIntOrIntVectorTy())
    return nullptr;

  // If A is the 'not' operand of B and has enough signbits, we have our answer.
  if (ABIsTheSame ? (A == B) : match(B, m_Not(m_Specific(A)))) {
    // If these are scalars or vectors of i1, A can be used directly.
    if (Ty->isIntOrIntVectorTy(1))
      return A;

    // If we look through a vector bitcast, the caller will bitcast the operands
    // to match the condition's number of bits (N x i1).
    // To make this poison-safe, disallow bitcast from wide element to narrow
    // element. That could allow poison in lanes where it was not present in the
    // original code.
    A = peekThroughBitcast(A);
    if (A->getType()->isIntOrIntVectorTy()) {
      unsigned NumSignBits = ComputeNumSignBits(A);
      if (NumSignBits == A->getType()->getScalarSizeInBits() &&
          NumSignBits <= Ty->getScalarSizeInBits())
        return Builder.CreateTrunc(A, CmpInst::makeCmpResultType(A->getType()));
    }
    return nullptr;
  }

  // TODO: add support for sext and constant case
  if (ABIsTheSame)
    return nullptr;

  // If both operands are constants, see if the constants are inverse bitmasks.
  Constant *AConst, *BConst;
  if (match(A, m_Constant(AConst)) && match(B, m_Constant(BConst)))
    if (AConst == ConstantExpr::getNot(BConst) &&
        ComputeNumSignBits(A) == Ty->getScalarSizeInBits())
      return Builder.CreateZExtOrTrunc(A, CmpInst::makeCmpResultType(Ty));

  // Look for more complex patterns. The 'not' op may be hidden behind various
  // casts. Look through sexts and bitcasts to find the booleans.
  Value *Cond;
  Value *NotB;
  if (match(A, m_SExt(m_Value(Cond))) &&
      Cond->getType()->isIntOrIntVectorTy(1)) {
    // A = sext i1 Cond; B = sext (not (i1 Cond))
    if (match(B, m_SExt(m_Not(m_Specific(Cond)))))
      return Cond;

    // A = sext i1 Cond; B = not ({bitcast} (sext (i1 Cond)))
    // TODO: The one-use checks are unnecessary or misplaced. If the caller
    //       checked for uses on logic ops/casts, that should be enough to
    //       make this transform worthwhile.
    if (match(B, m_OneUse(m_Not(m_Value(NotB))))) {
      NotB = peekThroughBitcast(NotB, true);
      if (match(NotB, m_SExt(m_Specific(Cond))))
        return Cond;
    }
  }

  // All scalar (and most vector) possibilities should be handled now.
  // Try more matches that only apply to non-splat constant vectors.
  if (!Ty->isVectorTy())
    return nullptr;

  // If both operands are xor'd with constants using the same sexted boolean
  // operand, see if the constants are inverse bitmasks.
  // TODO: Use ConstantExpr::getNot()?
  if (match(A, (m_Xor(m_SExt(m_Value(Cond)), m_Constant(AConst)))) &&
      match(B, (m_Xor(m_SExt(m_Specific(Cond)), m_Constant(BConst)))) &&
      Cond->getType()->isIntOrIntVectorTy(1) &&
      areInverseVectorBitmasks(AConst, BConst)) {
    AConst = ConstantExpr::getTrunc(AConst, CmpInst::makeCmpResultType(Ty));
    return Builder.CreateXor(Cond, AConst);
  }
  return nullptr;
}

/// We have an expression of the form (A & B) | (C & D). Try to simplify this
/// to "A' ? B : D", where A' is a boolean or vector of booleans.
/// When InvertFalseVal is set to true, we try to match the pattern
/// where we have peeked through a 'not' op and A and C are the same:
/// (A & B) | ~(A | D) --> (A & B) | (~A & ~D) --> A' ? B : ~D
Value *InstCombinerImpl::matchSelectFromAndOr(Value *A, Value *B, Value *C,
                                              Value *D, bool InvertFalseVal) {
  // The potential condition of the select may be bitcasted. In that case, look
  // through its bitcast and the corresponding bitcast of the 'not' condition.
  Type *OrigType = A->getType();
  A = peekThroughBitcast(A, true);
  C = peekThroughBitcast(C, true);
  if (Value *Cond = getSelectCondition(A, C, InvertFalseVal)) {
    // ((bc Cond) & B) | ((bc ~Cond) & D) --> bc (select Cond, (bc B), (bc D))
    // If this is a vector, we may need to cast to match the condition's length.
    // The bitcasts will either all exist or all not exist. The builder will
    // not create unnecessary casts if the types already match.
    Type *SelTy = A->getType();
    if (auto *VecTy = dyn_cast<VectorType>(Cond->getType())) {
      // For a fixed or scalable vector get N from <{vscale x} N x iM>
      unsigned Elts = VecTy->getElementCount().getKnownMinValue();
      // For a fixed or scalable vector, get the size in bits of N x iM; for a
      // scalar this is just M.
      unsigned SelEltSize = SelTy->getPrimitiveSizeInBits().getKnownMinValue();
      Type *EltTy = Builder.getIntNTy(SelEltSize / Elts);
      SelTy = VectorType::get(EltTy, VecTy->getElementCount());
    }
    Value *BitcastB = Builder.CreateBitCast(B, SelTy);
    if (InvertFalseVal)
      D = Builder.CreateNot(D);
    Value *BitcastD = Builder.CreateBitCast(D, SelTy);
    Value *Select = Builder.CreateSelect(Cond, BitcastB, BitcastD);
    return Builder.CreateBitCast(Select, OrigType);
  }

  return nullptr;
}

// (icmp eq X, C) | (icmp ult Other, (X - C)) -> (icmp ule Other, (X - (C + 1)))
// (icmp ne X, C) & (icmp uge Other, (X - C)) -> (icmp ugt Other, (X - (C + 1)))
static Value *foldAndOrOfICmpEqConstantAndICmp(ICmpInst *LHS, ICmpInst *RHS,
                                               bool IsAnd, bool IsLogical,
                                               IRBuilderBase &Builder) {
  Value *LHS0 = LHS->getOperand(0);
  Value *RHS0 = RHS->getOperand(0);
  Value *RHS1 = RHS->getOperand(1);

  ICmpInst::Predicate LPred =
      IsAnd ? LHS->getInversePredicate() : LHS->getPredicate();
  ICmpInst::Predicate RPred =
      IsAnd ? RHS->getInversePredicate() : RHS->getPredicate();

  const APInt *CInt;
  if (LPred != ICmpInst::ICMP_EQ ||
      !match(LHS->getOperand(1), m_APIntAllowPoison(CInt)) ||
      !LHS0->getType()->isIntOrIntVectorTy() ||
      !(LHS->hasOneUse() || RHS->hasOneUse()))
    return nullptr;

  auto MatchRHSOp = [LHS0, CInt](const Value *RHSOp) {
    return match(RHSOp,
                 m_Add(m_Specific(LHS0), m_SpecificIntAllowPoison(-*CInt))) ||
           (CInt->isZero() && RHSOp == LHS0);
  };

  Value *Other;
  if (RPred == ICmpInst::ICMP_ULT && MatchRHSOp(RHS1))
    Other = RHS0;
  else if (RPred == ICmpInst::ICMP_UGT && MatchRHSOp(RHS0))
    Other = RHS1;
  else
    return nullptr;

  if (IsLogical)
    Other = Builder.CreateFreeze(Other);

  return Builder.CreateICmp(
      IsAnd ? ICmpInst::ICMP_ULT : ICmpInst::ICMP_UGE,
      Builder.CreateSub(LHS0, ConstantInt::get(LHS0->getType(), *CInt + 1)),
      Other);
}

/// Fold (icmp)&(icmp) or (icmp)|(icmp) if possible.
/// If IsLogical is true, then the and/or is in select form and the transform
/// must be poison-safe.
Value *InstCombinerImpl::foldAndOrOfICmps(ICmpInst *LHS, ICmpInst *RHS,
                                          Instruction &I, bool IsAnd,
                                          bool IsLogical) {
  const SimplifyQuery Q = SQ.getWithInstruction(&I);

  // Fold (iszero(A & K1) | iszero(A & K2)) ->  (A & (K1 | K2)) != (K1 | K2)
  // Fold (!iszero(A & K1) & !iszero(A & K2)) ->  (A & (K1 | K2)) == (K1 | K2)
  // if K1 and K2 are a one-bit mask.
  if (Value *V = foldAndOrOfICmpsOfAndWithPow2(LHS, RHS, &I, IsAnd, IsLogical))
    return V;

  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  Value *LHS0 = LHS->getOperand(0), *RHS0 = RHS->getOperand(0);
  Value *LHS1 = LHS->getOperand(1), *RHS1 = RHS->getOperand(1);

  const APInt *LHSC = nullptr, *RHSC = nullptr;
  match(LHS1, m_APInt(LHSC));
  match(RHS1, m_APInt(RHSC));

  // (icmp1 A, B) | (icmp2 A, B) --> (icmp3 A, B)
  // (icmp1 A, B) & (icmp2 A, B) --> (icmp3 A, B)
  if (predicatesFoldable(PredL, PredR)) {
    if (LHS0 == RHS1 && LHS1 == RHS0) {
      PredL = ICmpInst::getSwappedPredicate(PredL);
      std::swap(LHS0, LHS1);
    }
    if (LHS0 == RHS0 && LHS1 == RHS1) {
      unsigned Code = IsAnd ? getICmpCode(PredL) & getICmpCode(PredR)
                            : getICmpCode(PredL) | getICmpCode(PredR);
      bool IsSigned = LHS->isSigned() || RHS->isSigned();
      return getNewICmpValue(Code, IsSigned, LHS0, LHS1, Builder);
    }
  }

  // handle (roughly):
  // (icmp ne (A & B), C) | (icmp ne (A & D), E)
  // (icmp eq (A & B), C) & (icmp eq (A & D), E)
  if (Value *V = foldLogOpOfMaskedICmps(LHS, RHS, IsAnd, IsLogical, Builder))
    return V;

  if (Value *V =
          foldAndOrOfICmpEqConstantAndICmp(LHS, RHS, IsAnd, IsLogical, Builder))
    return V;
  // We can treat logical like bitwise here, because both operands are used on
  // the LHS, and as such poison from both will propagate.
  if (Value *V = foldAndOrOfICmpEqConstantAndICmp(RHS, LHS, IsAnd,
                                                  /*IsLogical*/ false, Builder))
    return V;

  if (Value *V =
          foldAndOrOfICmpsWithConstEq(LHS, RHS, IsAnd, IsLogical, Builder, Q))
    return V;
  // We can convert this case to bitwise and, because both operands are used
  // on the LHS, and as such poison from both will propagate.
  if (Value *V = foldAndOrOfICmpsWithConstEq(RHS, LHS, IsAnd,
                                             /*IsLogical*/ false, Builder, Q))
    return V;

  if (Value *V = foldIsPowerOf2OrZero(LHS, RHS, IsAnd, Builder))
    return V;
  if (Value *V = foldIsPowerOf2OrZero(RHS, LHS, IsAnd, Builder))
    return V;

  // TODO: One of these directions is fine with logical and/or, the other could
  // be supported by inserting freeze.
  if (!IsLogical) {
    // E.g. (icmp slt x, 0) | (icmp sgt x, n) --> icmp ugt x, n
    // E.g. (icmp sge x, 0) & (icmp slt x, n) --> icmp ult x, n
    if (Value *V = simplifyRangeCheck(LHS, RHS, /*Inverted=*/!IsAnd))
      return V;

    // E.g. (icmp sgt x, n) | (icmp slt x, 0) --> icmp ugt x, n
    // E.g. (icmp slt x, n) & (icmp sge x, 0) --> icmp ult x, n
    if (Value *V = simplifyRangeCheck(RHS, LHS, /*Inverted=*/!IsAnd))
      return V;
  }

  // TODO: Add conjugated or fold, check whether it is safe for logical and/or.
  if (IsAnd && !IsLogical)
    if (Value *V = foldSignedTruncationCheck(LHS, RHS, I, Builder))
      return V;

  if (Value *V = foldIsPowerOf2(LHS, RHS, IsAnd, Builder, *this))
    return V;

  if (Value *V = foldPowerOf2AndShiftedMask(LHS, RHS, IsAnd, Builder))
    return V;

  // TODO: Verify whether this is safe for logical and/or.
  if (!IsLogical) {
    if (Value *X = foldUnsignedUnderflowCheck(LHS, RHS, IsAnd, Q, Builder))
      return X;
    if (Value *X = foldUnsignedUnderflowCheck(RHS, LHS, IsAnd, Q, Builder))
      return X;
  }

  if (Value *X = foldEqOfParts(LHS, RHS, IsAnd))
    return X;

  // (icmp ne A, 0) | (icmp ne B, 0) --> (icmp ne (A|B), 0)
  // (icmp eq A, 0) & (icmp eq B, 0) --> (icmp eq (A|B), 0)
  // TODO: Remove this and below when foldLogOpOfMaskedICmps can handle undefs.
  if (!IsLogical && PredL == (IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE) &&
      PredL == PredR && match(LHS1, m_ZeroInt()) && match(RHS1, m_ZeroInt()) &&
      LHS0->getType() == RHS0->getType()) {
    Value *NewOr = Builder.CreateOr(LHS0, RHS0);
    return Builder.CreateICmp(PredL, NewOr,
                              Constant::getNullValue(NewOr->getType()));
  }

  // (icmp ne A, -1) | (icmp ne B, -1) --> (icmp ne (A&B), -1)
  // (icmp eq A, -1) & (icmp eq B, -1) --> (icmp eq (A&B), -1)
  if (!IsLogical && PredL == (IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE) &&
      PredL == PredR && match(LHS1, m_AllOnes()) && match(RHS1, m_AllOnes()) &&
      LHS0->getType() == RHS0->getType()) {
    Value *NewAnd = Builder.CreateAnd(LHS0, RHS0);
    return Builder.CreateICmp(PredL, NewAnd,
                              Constant::getAllOnesValue(LHS0->getType()));
  }

  if (!IsLogical)
    if (Value *V =
            foldAndOrOfICmpsWithPow2AndWithZero(Builder, LHS, RHS, IsAnd, Q))
      return V;

  // This only handles icmp of constants: (icmp1 A, C1) | (icmp2 B, C2).
  if (!LHSC || !RHSC)
    return nullptr;

  // (trunc x) == C1 & (and x, CA) == C2 -> (and x, CA|CMAX) == C1|C2
  // (trunc x) != C1 | (and x, CA) != C2 -> (and x, CA|CMAX) != C1|C2
  // where CMAX is the all ones value for the truncated type,
  // iff the lower bits of C2 and CA are zero.
  if (PredL == (IsAnd ? ICmpInst::ICMP_EQ : ICmpInst::ICMP_NE) &&
      PredL == PredR && LHS->hasOneUse() && RHS->hasOneUse()) {
    Value *V;
    const APInt *AndC, *SmallC = nullptr, *BigC = nullptr;

    // (trunc x) == C1 & (and x, CA) == C2
    // (and x, CA) == C2 & (trunc x) == C1
    if (match(RHS0, m_Trunc(m_Value(V))) &&
        match(LHS0, m_And(m_Specific(V), m_APInt(AndC)))) {
      SmallC = RHSC;
      BigC = LHSC;
    } else if (match(LHS0, m_Trunc(m_Value(V))) &&
               match(RHS0, m_And(m_Specific(V), m_APInt(AndC)))) {
      SmallC = LHSC;
      BigC = RHSC;
    }

    if (SmallC && BigC) {
      unsigned BigBitSize = BigC->getBitWidth();
      unsigned SmallBitSize = SmallC->getBitWidth();

      // Check that the low bits are zero.
      APInt Low = APInt::getLowBitsSet(BigBitSize, SmallBitSize);
      if ((Low & *AndC).isZero() && (Low & *BigC).isZero()) {
        Value *NewAnd = Builder.CreateAnd(V, Low | *AndC);
        APInt N = SmallC->zext(BigBitSize) | *BigC;
        Value *NewVal = ConstantInt::get(NewAnd->getType(), N);
        return Builder.CreateICmp(PredL, NewAnd, NewVal);
      }
    }
  }

  // Match naive pattern (and its inverted form) for checking if two values
  // share same sign. An example of the pattern:
  // (icmp slt (X & Y), 0) | (icmp sgt (X | Y), -1) -> (icmp sgt (X ^ Y), -1)
  // Inverted form (example):
  // (icmp slt (X | Y), 0) & (icmp sgt (X & Y), -1) -> (icmp slt (X ^ Y), 0)
  bool TrueIfSignedL, TrueIfSignedR;
  if (isSignBitCheck(PredL, *LHSC, TrueIfSignedL) &&
      isSignBitCheck(PredR, *RHSC, TrueIfSignedR) &&
      (RHS->hasOneUse() || LHS->hasOneUse())) {
    Value *X, *Y;
    if (IsAnd) {
      if ((TrueIfSignedL && !TrueIfSignedR &&
           match(LHS0, m_Or(m_Value(X), m_Value(Y))) &&
           match(RHS0, m_c_And(m_Specific(X), m_Specific(Y)))) ||
          (!TrueIfSignedL && TrueIfSignedR &&
           match(LHS0, m_And(m_Value(X), m_Value(Y))) &&
           match(RHS0, m_c_Or(m_Specific(X), m_Specific(Y))))) {
        Value *NewXor = Builder.CreateXor(X, Y);
        return Builder.CreateIsNeg(NewXor);
      }
    } else {
      if ((TrueIfSignedL && !TrueIfSignedR &&
            match(LHS0, m_And(m_Value(X), m_Value(Y))) &&
            match(RHS0, m_c_Or(m_Specific(X), m_Specific(Y)))) ||
          (!TrueIfSignedL && TrueIfSignedR &&
           match(LHS0, m_Or(m_Value(X), m_Value(Y))) &&
           match(RHS0, m_c_And(m_Specific(X), m_Specific(Y))))) {
        Value *NewXor = Builder.CreateXor(X, Y);
        return Builder.CreateIsNotNeg(NewXor);
      }
    }
  }

  return foldAndOrOfICmpsUsingRanges(LHS, RHS, IsAnd);
}

static Value *foldOrOfInversions(BinaryOperator &I,
                                 InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::Or &&
         "Simplification only supports or at the moment.");

  Value *Cmp1, *Cmp2, *Cmp3, *Cmp4;
  if (!match(I.getOperand(0), m_And(m_Value(Cmp1), m_Value(Cmp2))) ||
      !match(I.getOperand(1), m_And(m_Value(Cmp3), m_Value(Cmp4))))
    return nullptr;

  // Check if any two pairs of the and operations are inversions of each other.
  if (isKnownInversion(Cmp1, Cmp3) && isKnownInversion(Cmp2, Cmp4))
    return Builder.CreateXor(Cmp1, Cmp4);
  if (isKnownInversion(Cmp1, Cmp4) && isKnownInversion(Cmp2, Cmp3))
    return Builder.CreateXor(Cmp1, Cmp3);

  return nullptr;
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombinerImpl::visitOr(BinaryOperator &I) {
  if (Value *V = simplifyOrInst(I.getOperand(0), I.getOperand(1),
                                SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  // Do this before using distributive laws to catch simple and/or/not patterns.
  if (Instruction *Xor = foldOrToXor(I, Builder))
    return Xor;

  if (Instruction *X = foldComplexAndOrPatterns(I, Builder))
    return X;

  // (A & B) | (C & D) -> A ^ D where A == ~C && B == ~D
  // (A & B) | (C & D) -> A ^ C where A == ~D && B == ~C
  if (Value *V = foldOrOfInversions(I, Builder))
    return replaceInstUsesWith(I, V);

  // (A&B)|(A&C) -> A&(B|C) etc
  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Type *Ty = I.getType();
  if (Ty->isIntOrIntVectorTy(1)) {
    if (auto *SI0 = dyn_cast<SelectInst>(Op0)) {
      if (auto *R =
              foldAndOrOfSelectUsingImpliedCond(Op1, *SI0, /* IsAnd */ false))
        return R;
    }
    if (auto *SI1 = dyn_cast<SelectInst>(Op1)) {
      if (auto *R =
              foldAndOrOfSelectUsingImpliedCond(Op0, *SI1, /* IsAnd */ false))
        return R;
    }
  }

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  if (Instruction *BitOp = matchBSwapOrBitReverse(I, /*MatchBSwaps*/ true,
                                                  /*MatchBitReversals*/ true))
    return BitOp;

  if (Instruction *Funnel = matchFunnelShift(I, *this))
    return Funnel;

  if (Instruction *Concat = matchOrConcat(I, Builder))
    return replaceInstUsesWith(I, Concat);

  if (Instruction *R = foldBinOpShiftWithShift(I))
    return R;

  if (Instruction *R = tryFoldInstWithCtpopWithNot(&I))
    return R;

  Value *X, *Y;
  const APInt *CV;
  if (match(&I, m_c_Or(m_OneUse(m_Xor(m_Value(X), m_APInt(CV))), m_Value(Y))) &&
      !CV->isAllOnes() && MaskedValueIsZero(Y, *CV, 0, &I)) {
    // (X ^ C) | Y -> (X | Y) ^ C iff Y & C == 0
    // The check for a 'not' op is for efficiency (if Y is known zero --> ~X).
    Value *Or = Builder.CreateOr(X, Y);
    return BinaryOperator::CreateXor(Or, ConstantInt::get(Ty, *CV));
  }

  // If the operands have no common bits set:
  // or (mul X, Y), X --> add (mul X, Y), X --> mul X, (Y + 1)
  if (match(&I, m_c_DisjointOr(m_OneUse(m_Mul(m_Value(X), m_Value(Y))),
                               m_Deferred(X)))) {
    Value *IncrementY = Builder.CreateAdd(Y, ConstantInt::get(Ty, 1));
    return BinaryOperator::CreateMul(X, IncrementY);
  }

  // (A & C) | (B & D)
  Value *A, *B, *C, *D;
  if (match(Op0, m_And(m_Value(A), m_Value(C))) &&
      match(Op1, m_And(m_Value(B), m_Value(D)))) {

    // (A & C0) | (B & C1)
    const APInt *C0, *C1;
    if (match(C, m_APInt(C0)) && match(D, m_APInt(C1))) {
      Value *X;
      if (*C0 == ~*C1) {
        // ((X | B) & MaskC) | (B & ~MaskC) -> (X & MaskC) | B
        if (match(A, m_c_Or(m_Value(X), m_Specific(B))))
          return BinaryOperator::CreateOr(Builder.CreateAnd(X, *C0), B);
        // (A & MaskC) | ((X | A) & ~MaskC) -> (X & ~MaskC) | A
        if (match(B, m_c_Or(m_Specific(A), m_Value(X))))
          return BinaryOperator::CreateOr(Builder.CreateAnd(X, *C1), A);

        // ((X ^ B) & MaskC) | (B & ~MaskC) -> (X & MaskC) ^ B
        if (match(A, m_c_Xor(m_Value(X), m_Specific(B))))
          return BinaryOperator::CreateXor(Builder.CreateAnd(X, *C0), B);
        // (A & MaskC) | ((X ^ A) & ~MaskC) -> (X & ~MaskC) ^ A
        if (match(B, m_c_Xor(m_Specific(A), m_Value(X))))
          return BinaryOperator::CreateXor(Builder.CreateAnd(X, *C1), A);
      }

      if ((*C0 & *C1).isZero()) {
        // ((X | B) & C0) | (B & C1) --> (X | B) & (C0 | C1)
        // iff (C0 & C1) == 0 and (X & ~C0) == 0
        if (match(A, m_c_Or(m_Value(X), m_Specific(B))) &&
            MaskedValueIsZero(X, ~*C0, 0, &I)) {
          Constant *C01 = ConstantInt::get(Ty, *C0 | *C1);
          return BinaryOperator::CreateAnd(A, C01);
        }
        // (A & C0) | ((X | A) & C1) --> (X | A) & (C0 | C1)
        // iff (C0 & C1) == 0 and (X & ~C1) == 0
        if (match(B, m_c_Or(m_Value(X), m_Specific(A))) &&
            MaskedValueIsZero(X, ~*C1, 0, &I)) {
          Constant *C01 = ConstantInt::get(Ty, *C0 | *C1);
          return BinaryOperator::CreateAnd(B, C01);
        }
        // ((X | C2) & C0) | ((X | C3) & C1) --> (X | C2 | C3) & (C0 | C1)
        // iff (C0 & C1) == 0 and (C2 & ~C0) == 0 and (C3 & ~C1) == 0.
        const APInt *C2, *C3;
        if (match(A, m_Or(m_Value(X), m_APInt(C2))) &&
            match(B, m_Or(m_Specific(X), m_APInt(C3))) &&
            (*C2 & ~*C0).isZero() && (*C3 & ~*C1).isZero()) {
          Value *Or = Builder.CreateOr(X, *C2 | *C3, "bitfield");
          Constant *C01 = ConstantInt::get(Ty, *C0 | *C1);
          return BinaryOperator::CreateAnd(Or, C01);
        }
      }
    }

    // Don't try to form a select if it's unlikely that we'll get rid of at
    // least one of the operands. A select is generally more expensive than the
    // 'or' that it is replacing.
    if (Op0->hasOneUse() || Op1->hasOneUse()) {
      // (Cond & C) | (~Cond & D) -> Cond ? C : D, and commuted variants.
      if (Value *V = matchSelectFromAndOr(A, C, B, D))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(A, C, D, B))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(C, A, B, D))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(C, A, D, B))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(B, D, A, C))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(B, D, C, A))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(D, B, A, C))
        return replaceInstUsesWith(I, V);
      if (Value *V = matchSelectFromAndOr(D, B, C, A))
        return replaceInstUsesWith(I, V);
    }
  }

  if (match(Op0, m_And(m_Value(A), m_Value(C))) &&
      match(Op1, m_Not(m_Or(m_Value(B), m_Value(D)))) &&
      (Op0->hasOneUse() || Op1->hasOneUse())) {
    // (Cond & C) | ~(Cond | D) -> Cond ? C : ~D
    if (Value *V = matchSelectFromAndOr(A, C, B, D, true))
      return replaceInstUsesWith(I, V);
    if (Value *V = matchSelectFromAndOr(A, C, D, B, true))
      return replaceInstUsesWith(I, V);
    if (Value *V = matchSelectFromAndOr(C, A, B, D, true))
      return replaceInstUsesWith(I, V);
    if (Value *V = matchSelectFromAndOr(C, A, D, B, true))
      return replaceInstUsesWith(I, V);
  }

  // (A ^ B) | ((B ^ C) ^ A) -> (A ^ B) | C
  if (match(Op0, m_Xor(m_Value(A), m_Value(B))))
    if (match(Op1,
              m_c_Xor(m_c_Xor(m_Specific(B), m_Value(C)), m_Specific(A))) ||
        match(Op1, m_c_Xor(m_c_Xor(m_Specific(A), m_Value(C)), m_Specific(B))))
      return BinaryOperator::CreateOr(Op0, C);

  // ((B ^ C) ^ A) | (A ^ B) -> (A ^ B) | C
  if (match(Op1, m_Xor(m_Value(A), m_Value(B))))
    if (match(Op0,
              m_c_Xor(m_c_Xor(m_Specific(B), m_Value(C)), m_Specific(A))) ||
        match(Op0, m_c_Xor(m_c_Xor(m_Specific(A), m_Value(C)), m_Specific(B))))
      return BinaryOperator::CreateOr(Op1, C);

  if (Instruction *DeMorgan = matchDeMorgansLaws(I, *this))
    return DeMorgan;

  // Canonicalize xor to the RHS.
  bool SwappedForXor = false;
  if (match(Op0, m_Xor(m_Value(), m_Value()))) {
    std::swap(Op0, Op1);
    SwappedForXor = true;
  }

  if (match(Op1, m_Xor(m_Value(A), m_Value(B)))) {
    // (A | ?) | (A ^ B) --> (A | ?) | B
    // (B | ?) | (A ^ B) --> (B | ?) | A
    if (match(Op0, m_c_Or(m_Specific(A), m_Value())))
      return BinaryOperator::CreateOr(Op0, B);
    if (match(Op0, m_c_Or(m_Specific(B), m_Value())))
      return BinaryOperator::CreateOr(Op0, A);

    // (A & B) | (A ^ B) --> A | B
    // (B & A) | (A ^ B) --> A | B
    if (match(Op0, m_c_And(m_Specific(A), m_Specific(B))))
      return BinaryOperator::CreateOr(A, B);

    // ~A | (A ^ B) --> ~(A & B)
    // ~B | (A ^ B) --> ~(A & B)
    // The swap above should always make Op0 the 'not'.
    if ((Op0->hasOneUse() || Op1->hasOneUse()) &&
        (match(Op0, m_Not(m_Specific(A))) || match(Op0, m_Not(m_Specific(B)))))
      return BinaryOperator::CreateNot(Builder.CreateAnd(A, B));

    // Same as above, but peek through an 'and' to the common operand:
    // ~(A & ?) | (A ^ B) --> ~((A & ?) & B)
    // ~(B & ?) | (A ^ B) --> ~((B & ?) & A)
    Instruction *And;
    if ((Op0->hasOneUse() || Op1->hasOneUse()) &&
        match(Op0, m_Not(m_CombineAnd(m_Instruction(And),
                                      m_c_And(m_Specific(A), m_Value())))))
      return BinaryOperator::CreateNot(Builder.CreateAnd(And, B));
    if ((Op0->hasOneUse() || Op1->hasOneUse()) &&
        match(Op0, m_Not(m_CombineAnd(m_Instruction(And),
                                      m_c_And(m_Specific(B), m_Value())))))
      return BinaryOperator::CreateNot(Builder.CreateAnd(And, A));

    // (~A | C) | (A ^ B) --> ~(A & B) | C
    // (~B | C) | (A ^ B) --> ~(A & B) | C
    if (Op0->hasOneUse() && Op1->hasOneUse() &&
        (match(Op0, m_c_Or(m_Not(m_Specific(A)), m_Value(C))) ||
         match(Op0, m_c_Or(m_Not(m_Specific(B)), m_Value(C))))) {
      Value *Nand = Builder.CreateNot(Builder.CreateAnd(A, B), "nand");
      return BinaryOperator::CreateOr(Nand, C);
    }
  }

  if (SwappedForXor)
    std::swap(Op0, Op1);

  {
    ICmpInst *LHS = dyn_cast<ICmpInst>(Op0);
    ICmpInst *RHS = dyn_cast<ICmpInst>(Op1);
    if (LHS && RHS)
      if (Value *Res = foldAndOrOfICmps(LHS, RHS, I, /* IsAnd */ false))
        return replaceInstUsesWith(I, Res);

    // TODO: Make this recursive; it's a little tricky because an arbitrary
    // number of 'or' instructions might have to be created.
    Value *X, *Y;
    if (LHS && match(Op1, m_OneUse(m_LogicalOr(m_Value(X), m_Value(Y))))) {
      bool IsLogical = isa<SelectInst>(Op1);
      // LHS | (X || Y) --> (LHS || X) || Y
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res =
                foldAndOrOfICmps(LHS, Cmp, I, /* IsAnd */ false, IsLogical))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalOr(Res, Y)
                                            : Builder.CreateOr(Res, Y));
      // LHS | (X || Y) --> X || (LHS | Y)
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOrOfICmps(LHS, Cmp, I, /* IsAnd */ false,
                                          /* IsLogical */ false))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalOr(X, Res)
                                            : Builder.CreateOr(X, Res));
    }
    if (RHS && match(Op0, m_OneUse(m_LogicalOr(m_Value(X), m_Value(Y))))) {
      bool IsLogical = isa<SelectInst>(Op0);
      // (X || Y) | RHS --> (X || RHS) || Y
      if (auto *Cmp = dyn_cast<ICmpInst>(X))
        if (Value *Res =
                foldAndOrOfICmps(Cmp, RHS, I, /* IsAnd */ false, IsLogical))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalOr(Res, Y)
                                            : Builder.CreateOr(Res, Y));
      // (X || Y) | RHS --> X || (Y | RHS)
      if (auto *Cmp = dyn_cast<ICmpInst>(Y))
        if (Value *Res = foldAndOrOfICmps(Cmp, RHS, I, /* IsAnd */ false,
                                          /* IsLogical */ false))
          return replaceInstUsesWith(I, IsLogical
                                            ? Builder.CreateLogicalOr(X, Res)
                                            : Builder.CreateOr(X, Res));
    }
  }

  if (FCmpInst *LHS = dyn_cast<FCmpInst>(I.getOperand(0)))
    if (FCmpInst *RHS = dyn_cast<FCmpInst>(I.getOperand(1)))
      if (Value *Res = foldLogicOfFCmps(LHS, RHS, /*IsAnd*/ false))
        return replaceInstUsesWith(I, Res);

  if (Instruction *FoldedFCmps = reassociateFCmps(I, Builder))
    return FoldedFCmps;

  if (Instruction *CastedOr = foldCastedBitwiseLogic(I))
    return CastedOr;

  if (Instruction *Sel = foldBinopOfSextBoolToSelect(I))
    return Sel;

  // or(sext(A), B) / or(B, sext(A)) --> A ? -1 : B, where A is i1 or <N x i1>.
  // TODO: Move this into foldBinopOfSextBoolToSelect as a more generalized fold
  //       with binop identity constant. But creating a select with non-constant
  //       arm may not be reversible due to poison semantics. Is that a good
  //       canonicalization?
  if (match(&I, m_c_Or(m_OneUse(m_SExt(m_Value(A))), m_Value(B))) &&
      A->getType()->isIntOrIntVectorTy(1))
    return SelectInst::Create(A, ConstantInt::getAllOnesValue(Ty), B);

  // Note: If we've gotten to the point of visiting the outer OR, then the
  // inner one couldn't be simplified.  If it was a constant, then it won't
  // be simplified by a later pass either, so we try swapping the inner/outer
  // ORs in the hopes that we'll be able to simplify it this way.
  // (X|C) | V --> (X|V) | C
  ConstantInt *CI;
  if (Op0->hasOneUse() && !match(Op1, m_ConstantInt()) &&
      match(Op0, m_Or(m_Value(A), m_ConstantInt(CI)))) {
    Value *Inner = Builder.CreateOr(A, Op1);
    Inner->takeName(Op0);
    return BinaryOperator::CreateOr(Inner, CI);
  }

  // Change (or (bool?A:B),(bool?C:D)) --> (bool?(or A,C):(or B,D))
  // Since this OR statement hasn't been optimized further yet, we hope
  // that this transformation will allow the new ORs to be optimized.
  {
    Value *X = nullptr, *Y = nullptr;
    if (Op0->hasOneUse() && Op1->hasOneUse() &&
        match(Op0, m_Select(m_Value(X), m_Value(A), m_Value(B))) &&
        match(Op1, m_Select(m_Value(Y), m_Value(C), m_Value(D))) && X == Y) {
      Value *orTrue = Builder.CreateOr(A, C);
      Value *orFalse = Builder.CreateOr(B, D);
      return SelectInst::Create(X, orTrue, orFalse);
    }
  }

  // or(ashr(subNSW(Y, X), ScalarSizeInBits(Y) - 1), X)  --> X s> Y ? -1 : X.
  {
    Value *X, *Y;
    if (match(&I, m_c_Or(m_OneUse(m_AShr(
                             m_NSWSub(m_Value(Y), m_Value(X)),
                             m_SpecificInt(Ty->getScalarSizeInBits() - 1))),
                         m_Deferred(X)))) {
      Value *NewICmpInst = Builder.CreateICmpSGT(X, Y);
      Value *AllOnes = ConstantInt::getAllOnesValue(Ty);
      return SelectInst::Create(NewICmpInst, AllOnes, X);
    }
  }

  {
    // ((A & B) ^ A) | ((A & B) ^ B) -> A ^ B
    // (A ^ (A & B)) | (B ^ (A & B)) -> A ^ B
    // ((A & B) ^ B) | ((A & B) ^ A) -> A ^ B
    // (B ^ (A & B)) | (A ^ (A & B)) -> A ^ B
    const auto TryXorOpt = [&](Value *Lhs, Value *Rhs) -> Instruction * {
      if (match(Lhs, m_c_Xor(m_And(m_Value(A), m_Value(B)), m_Deferred(A))) &&
          match(Rhs,
                m_c_Xor(m_And(m_Specific(A), m_Specific(B)), m_Specific(B)))) {
        return BinaryOperator::CreateXor(A, B);
      }
      return nullptr;
    };

    if (Instruction *Result = TryXorOpt(Op0, Op1))
      return Result;
    if (Instruction *Result = TryXorOpt(Op1, Op0))
      return Result;
  }

  if (Instruction *V =
          canonicalizeCondSignextOfHighBitExtractToSignextHighBitExtract(I))
    return V;

  CmpInst::Predicate Pred;
  Value *Mul, *Ov, *MulIsNotZero, *UMulWithOv;
  // Check if the OR weakens the overflow condition for umul.with.overflow by
  // treating any non-zero result as overflow. In that case, we overflow if both
  // umul.with.overflow operands are != 0, as in that case the result can only
  // be 0, iff the multiplication overflows.
  if (match(&I,
            m_c_Or(m_CombineAnd(m_ExtractValue<1>(m_Value(UMulWithOv)),
                                m_Value(Ov)),
                   m_CombineAnd(m_ICmp(Pred,
                                       m_CombineAnd(m_ExtractValue<0>(
                                                        m_Deferred(UMulWithOv)),
                                                    m_Value(Mul)),
                                       m_ZeroInt()),
                                m_Value(MulIsNotZero)))) &&
      (Ov->hasOneUse() || (MulIsNotZero->hasOneUse() && Mul->hasOneUse())) &&
      Pred == CmpInst::ICMP_NE) {
    Value *A, *B;
    if (match(UMulWithOv, m_Intrinsic<Intrinsic::umul_with_overflow>(
                              m_Value(A), m_Value(B)))) {
      Value *NotNullA = Builder.CreateIsNotNull(A);
      Value *NotNullB = Builder.CreateIsNotNull(B);
      return BinaryOperator::CreateAnd(NotNullA, NotNullB);
    }
  }

  /// Res, Overflow = xxx_with_overflow X, C1
  /// Try to canonicalize the pattern "Overflow | icmp pred Res, C2" into
  /// "Overflow | icmp pred X, C2 +/- C1".
  const WithOverflowInst *WO;
  const Value *WOV;
  const APInt *C1, *C2;
  if (match(&I, m_c_Or(m_CombineAnd(m_ExtractValue<1>(m_CombineAnd(
                                        m_WithOverflowInst(WO), m_Value(WOV))),
                                    m_Value(Ov)),
                       m_OneUse(m_ICmp(Pred, m_ExtractValue<0>(m_Deferred(WOV)),
                                       m_APInt(C2))))) &&
      (WO->getBinaryOp() == Instruction::Add ||
       WO->getBinaryOp() == Instruction::Sub) &&
      (ICmpInst::isEquality(Pred) ||
       WO->isSigned() == ICmpInst::isSigned(Pred)) &&
      match(WO->getRHS(), m_APInt(C1))) {
    bool Overflow;
    APInt NewC = WO->getBinaryOp() == Instruction::Add
                     ? (ICmpInst::isSigned(Pred) ? C2->ssub_ov(*C1, Overflow)
                                                 : C2->usub_ov(*C1, Overflow))
                     : (ICmpInst::isSigned(Pred) ? C2->sadd_ov(*C1, Overflow)
                                                 : C2->uadd_ov(*C1, Overflow));
    if (!Overflow || ICmpInst::isEquality(Pred)) {
      Value *NewCmp = Builder.CreateICmp(
          Pred, WO->getLHS(), ConstantInt::get(WO->getLHS()->getType(), NewC));
      return BinaryOperator::CreateOr(Ov, NewCmp);
    }
  }

  // (~x) | y  -->  ~(x & (~y))  iff that gets rid of inversions
  if (sinkNotIntoOtherHandOfLogicalOp(I))
    return &I;

  // Improve "get low bit mask up to and including bit X" pattern:
  //   (1 << X) | ((1 << X) + -1)  -->  -1 l>> (bitwidth(x) - 1 - X)
  if (match(&I, m_c_Or(m_Add(m_Shl(m_One(), m_Value(X)), m_AllOnes()),
                       m_Shl(m_One(), m_Deferred(X)))) &&
      match(&I, m_c_Or(m_OneUse(m_Value()), m_Value()))) {
    Value *Sub = Builder.CreateSub(
        ConstantInt::get(Ty, Ty->getScalarSizeInBits() - 1), X);
    return BinaryOperator::CreateLShr(Constant::getAllOnesValue(Ty), Sub);
  }

  // An or recurrence w/loop invariant step is equivelent to (or start, step)
  PHINode *PN = nullptr;
  Value *Start = nullptr, *Step = nullptr;
  if (matchSimpleRecurrence(&I, PN, Start, Step) && DT.dominates(Step, PN))
    return replaceInstUsesWith(I, Builder.CreateOr(Start, Step));

  // (A & B) | (C | D) or (C | D) | (A & B)
  // Can be combined if C or D is of type (A/B & X)
  if (match(&I, m_c_Or(m_OneUse(m_And(m_Value(A), m_Value(B))),
                       m_OneUse(m_Or(m_Value(C), m_Value(D)))))) {
    // (A & B) | (C | ?) -> C | (? | (A & B))
    // (A & B) | (C | ?) -> C | (? | (A & B))
    // (A & B) | (C | ?) -> C | (? | (A & B))
    // (A & B) | (C | ?) -> C | (? | (A & B))
    // (C | ?) | (A & B) -> C | (? | (A & B))
    // (C | ?) | (A & B) -> C | (? | (A & B))
    // (C | ?) | (A & B) -> C | (? | (A & B))
    // (C | ?) | (A & B) -> C | (? | (A & B))
    if (match(D, m_OneUse(m_c_And(m_Specific(A), m_Value()))) ||
        match(D, m_OneUse(m_c_And(m_Specific(B), m_Value()))))
      return BinaryOperator::CreateOr(
          C, Builder.CreateOr(D, Builder.CreateAnd(A, B)));
    // (A & B) | (? | D) -> (? | (A & B)) | D
    // (A & B) | (? | D) -> (? | (A & B)) | D
    // (A & B) | (? | D) -> (? | (A & B)) | D
    // (A & B) | (? | D) -> (? | (A & B)) | D
    // (? | D) | (A & B) -> (? | (A & B)) | D
    // (? | D) | (A & B) -> (? | (A & B)) | D
    // (? | D) | (A & B) -> (? | (A & B)) | D
    // (? | D) | (A & B) -> (? | (A & B)) | D
    if (match(C, m_OneUse(m_c_And(m_Specific(A), m_Value()))) ||
        match(C, m_OneUse(m_c_And(m_Specific(B), m_Value()))))
      return BinaryOperator::CreateOr(
          Builder.CreateOr(C, Builder.CreateAnd(A, B)), D);
  }

  if (Instruction *R = reassociateForUses(I, Builder))
    return R;

  if (Instruction *Canonicalized = canonicalizeLogicFirst(I, Builder))
    return Canonicalized;

  if (Instruction *Folded = foldLogicOfIsFPClass(I, Op0, Op1))
    return Folded;

  if (Instruction *Res = foldBinOpOfDisplacedShifts(I))
    return Res;

  // If we are setting the sign bit of a floating-point value, convert
  // this to fneg(fabs), then cast back to integer.
  //
  // If the result isn't immediately cast back to a float, this will increase
  // the number of instructions. This is still probably a better canonical form
  // as it enables FP value tracking.
  //
  // Assumes any IEEE-represented type has the sign bit in the high bit.
  //
  // This is generous interpretation of noimplicitfloat, this is not a true
  // floating-point operation.
  Value *CastOp;
  if (match(Op0, m_ElementWiseBitCast(m_Value(CastOp))) &&
      match(Op1, m_SignMask()) &&
      !Builder.GetInsertBlock()->getParent()->hasFnAttribute(
          Attribute::NoImplicitFloat)) {
    Type *EltTy = CastOp->getType()->getScalarType();
    if (EltTy->isFloatingPointTy() && EltTy->isIEEE()) {
      Value *FAbs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, CastOp);
      Value *FNegFAbs = Builder.CreateFNeg(FAbs);
      return new BitCastInst(FNegFAbs, I.getType());
    }
  }

  // (X & C1) | C2 -> X & (C1 | C2) iff (X & C2) == C2
  if (match(Op0, m_OneUse(m_And(m_Value(X), m_APInt(C1)))) &&
      match(Op1, m_APInt(C2))) {
    KnownBits KnownX = computeKnownBits(X, /*Depth*/ 0, &I);
    if ((KnownX.One & *C2) == *C2)
      return BinaryOperator::CreateAnd(X, ConstantInt::get(Ty, *C1 | *C2));
  }

  if (Instruction *Res = foldBitwiseLogicWithIntrinsics(I, Builder))
    return Res;

  if (Value *V =
          simplifyAndOrWithOpReplaced(Op0, Op1, Constant::getNullValue(Ty),
                                      /*SimplifyOnly*/ false, *this))
    return BinaryOperator::CreateOr(V, Op1);
  if (Value *V =
          simplifyAndOrWithOpReplaced(Op1, Op0, Constant::getNullValue(Ty),
                                      /*SimplifyOnly*/ false, *this))
    return BinaryOperator::CreateOr(Op0, V);

  if (cast<PossiblyDisjointInst>(I).isDisjoint())
    if (Value *V = SimplifyAddWithRemainder(I))
      return replaceInstUsesWith(I, V);

  return nullptr;
}

/// A ^ B can be specified using other logic ops in a variety of patterns. We
/// can fold these early and efficiently by morphing an existing instruction.
static Instruction *foldXorToXor(BinaryOperator &I,
                                 InstCombiner::BuilderTy &Builder) {
  assert(I.getOpcode() == Instruction::Xor);
  Value *Op0 = I.getOperand(0);
  Value *Op1 = I.getOperand(1);
  Value *A, *B;

  // There are 4 commuted variants for each of the basic patterns.

  // (A & B) ^ (A | B) -> A ^ B
  // (A & B) ^ (B | A) -> A ^ B
  // (A | B) ^ (A & B) -> A ^ B
  // (A | B) ^ (B & A) -> A ^ B
  if (match(&I, m_c_Xor(m_And(m_Value(A), m_Value(B)),
                        m_c_Or(m_Deferred(A), m_Deferred(B)))))
    return BinaryOperator::CreateXor(A, B);

  // (A | ~B) ^ (~A | B) -> A ^ B
  // (~B | A) ^ (~A | B) -> A ^ B
  // (~A | B) ^ (A | ~B) -> A ^ B
  // (B | ~A) ^ (A | ~B) -> A ^ B
  if (match(&I, m_Xor(m_c_Or(m_Value(A), m_Not(m_Value(B))),
                      m_c_Or(m_Not(m_Deferred(A)), m_Deferred(B)))))
    return BinaryOperator::CreateXor(A, B);

  // (A & ~B) ^ (~A & B) -> A ^ B
  // (~B & A) ^ (~A & B) -> A ^ B
  // (~A & B) ^ (A & ~B) -> A ^ B
  // (B & ~A) ^ (A & ~B) -> A ^ B
  if (match(&I, m_Xor(m_c_And(m_Value(A), m_Not(m_Value(B))),
                      m_c_And(m_Not(m_Deferred(A)), m_Deferred(B)))))
    return BinaryOperator::CreateXor(A, B);

  // For the remaining cases we need to get rid of one of the operands.
  if (!Op0->hasOneUse() && !Op1->hasOneUse())
    return nullptr;

  // (A | B) ^ ~(A & B) -> ~(A ^ B)
  // (A | B) ^ ~(B & A) -> ~(A ^ B)
  // (A & B) ^ ~(A | B) -> ~(A ^ B)
  // (A & B) ^ ~(B | A) -> ~(A ^ B)
  // Complexity sorting ensures the not will be on the right side.
  if ((match(Op0, m_Or(m_Value(A), m_Value(B))) &&
       match(Op1, m_Not(m_c_And(m_Specific(A), m_Specific(B))))) ||
      (match(Op0, m_And(m_Value(A), m_Value(B))) &&
       match(Op1, m_Not(m_c_Or(m_Specific(A), m_Specific(B))))))
    return BinaryOperator::CreateNot(Builder.CreateXor(A, B));

  return nullptr;
}

Value *InstCombinerImpl::foldXorOfICmps(ICmpInst *LHS, ICmpInst *RHS,
                                        BinaryOperator &I) {
  assert(I.getOpcode() == Instruction::Xor && I.getOperand(0) == LHS &&
         I.getOperand(1) == RHS && "Should be 'xor' with these operands");

  ICmpInst::Predicate PredL = LHS->getPredicate(), PredR = RHS->getPredicate();
  Value *LHS0 = LHS->getOperand(0), *LHS1 = LHS->getOperand(1);
  Value *RHS0 = RHS->getOperand(0), *RHS1 = RHS->getOperand(1);

  if (predicatesFoldable(PredL, PredR)) {
    if (LHS0 == RHS1 && LHS1 == RHS0) {
      std::swap(LHS0, LHS1);
      PredL = ICmpInst::getSwappedPredicate(PredL);
    }
    if (LHS0 == RHS0 && LHS1 == RHS1) {
      // (icmp1 A, B) ^ (icmp2 A, B) --> (icmp3 A, B)
      unsigned Code = getICmpCode(PredL) ^ getICmpCode(PredR);
      bool IsSigned = LHS->isSigned() || RHS->isSigned();
      return getNewICmpValue(Code, IsSigned, LHS0, LHS1, Builder);
    }
  }

  // TODO: This can be generalized to compares of non-signbits using
  // decomposeBitTestICmp(). It could be enhanced more by using (something like)
  // foldLogOpOfMaskedICmps().
  const APInt *LC, *RC;
  if (match(LHS1, m_APInt(LC)) && match(RHS1, m_APInt(RC)) &&
      LHS0->getType() == RHS0->getType() &&
      LHS0->getType()->isIntOrIntVectorTy()) {
    // Convert xor of signbit tests to signbit test of xor'd values:
    // (X > -1) ^ (Y > -1) --> (X ^ Y) < 0
    // (X <  0) ^ (Y <  0) --> (X ^ Y) < 0
    // (X > -1) ^ (Y <  0) --> (X ^ Y) > -1
    // (X <  0) ^ (Y > -1) --> (X ^ Y) > -1
    bool TrueIfSignedL, TrueIfSignedR;
    if ((LHS->hasOneUse() || RHS->hasOneUse()) &&
        isSignBitCheck(PredL, *LC, TrueIfSignedL) &&
        isSignBitCheck(PredR, *RC, TrueIfSignedR)) {
      Value *XorLR = Builder.CreateXor(LHS0, RHS0);
      return TrueIfSignedL == TrueIfSignedR ? Builder.CreateIsNeg(XorLR) :
                                              Builder.CreateIsNotNeg(XorLR);
    }

    // Fold (icmp pred1 X, C1) ^ (icmp pred2 X, C2)
    // into a single comparison using range-based reasoning.
    if (LHS0 == RHS0) {
      ConstantRange CR1 = ConstantRange::makeExactICmpRegion(PredL, *LC);
      ConstantRange CR2 = ConstantRange::makeExactICmpRegion(PredR, *RC);
      auto CRUnion = CR1.exactUnionWith(CR2);
      auto CRIntersect = CR1.exactIntersectWith(CR2);
      if (CRUnion && CRIntersect)
        if (auto CR = CRUnion->exactIntersectWith(CRIntersect->inverse())) {
          if (CR->isFullSet())
            return ConstantInt::getTrue(I.getType());
          if (CR->isEmptySet())
            return ConstantInt::getFalse(I.getType());

          CmpInst::Predicate NewPred;
          APInt NewC, Offset;
          CR->getEquivalentICmp(NewPred, NewC, Offset);

          if ((Offset.isZero() && (LHS->hasOneUse() || RHS->hasOneUse())) ||
              (LHS->hasOneUse() && RHS->hasOneUse())) {
            Value *NewV = LHS0;
            Type *Ty = LHS0->getType();
            if (!Offset.isZero())
              NewV = Builder.CreateAdd(NewV, ConstantInt::get(Ty, Offset));
            return Builder.CreateICmp(NewPred, NewV,
                                      ConstantInt::get(Ty, NewC));
          }
        }
    }
  }

  // Instead of trying to imitate the folds for and/or, decompose this 'xor'
  // into those logic ops. That is, try to turn this into an and-of-icmps
  // because we have many folds for that pattern.
  //
  // This is based on a truth table definition of xor:
  // X ^ Y --> (X | Y) & !(X & Y)
  if (Value *OrICmp = simplifyBinOp(Instruction::Or, LHS, RHS, SQ)) {
    // TODO: If OrICmp is true, then the definition of xor simplifies to !(X&Y).
    // TODO: If OrICmp is false, the whole thing is false (InstSimplify?).
    if (Value *AndICmp = simplifyBinOp(Instruction::And, LHS, RHS, SQ)) {
      // TODO: Independently handle cases where the 'and' side is a constant.
      ICmpInst *X = nullptr, *Y = nullptr;
      if (OrICmp == LHS && AndICmp == RHS) {
        // (LHS | RHS) & !(LHS & RHS) --> LHS & !RHS  --> X & !Y
        X = LHS;
        Y = RHS;
      }
      if (OrICmp == RHS && AndICmp == LHS) {
        // !(LHS & RHS) & (LHS | RHS) --> !LHS & RHS  --> !Y & X
        X = RHS;
        Y = LHS;
      }
      if (X && Y && (Y->hasOneUse() || canFreelyInvertAllUsersOf(Y, &I))) {
        // Invert the predicate of 'Y', thus inverting its output.
        Y->setPredicate(Y->getInversePredicate());
        // So, are there other uses of Y?
        if (!Y->hasOneUse()) {
          // We need to adapt other uses of Y though. Get a value that matches
          // the original value of Y before inversion. While this increases
          // immediate instruction count, we have just ensured that all the
          // users are freely-invertible, so that 'not' *will* get folded away.
          BuilderTy::InsertPointGuard Guard(Builder);
          // Set insertion point to right after the Y.
          Builder.SetInsertPoint(Y->getParent(), ++(Y->getIterator()));
          Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
          // Replace all uses of Y (excluding the one in NotY!) with NotY.
          Worklist.pushUsersToWorkList(*Y);
          Y->replaceUsesWithIf(NotY,
                               [NotY](Use &U) { return U.getUser() != NotY; });
        }
        // All done.
        return Builder.CreateAnd(LHS, RHS);
      }
    }
  }

  return nullptr;
}

/// If we have a masked merge, in the canonical form of:
/// (assuming that A only has one use.)
///   |        A  |  |B|
///   ((x ^ y) & M) ^ y
///    |  D  |
/// * If M is inverted:
///      |  D  |
///     ((x ^ y) & ~M) ^ y
///   We can canonicalize by swapping the final xor operand
///   to eliminate the 'not' of the mask.
///     ((x ^ y) & M) ^ x
/// * If M is a constant, and D has one use, we transform to 'and' / 'or' ops
///   because that shortens the dependency chain and improves analysis:
///     (x & M) | (y & ~M)
static Instruction *visitMaskedMerge(BinaryOperator &I,
                                     InstCombiner::BuilderTy &Builder) {
  Value *B, *X, *D;
  Value *M;
  if (!match(&I, m_c_Xor(m_Value(B),
                         m_OneUse(m_c_And(
                             m_CombineAnd(m_c_Xor(m_Deferred(B), m_Value(X)),
                                          m_Value(D)),
                             m_Value(M))))))
    return nullptr;

  Value *NotM;
  if (match(M, m_Not(m_Value(NotM)))) {
    // De-invert the mask and swap the value in B part.
    Value *NewA = Builder.CreateAnd(D, NotM);
    return BinaryOperator::CreateXor(NewA, X);
  }

  Constant *C;
  if (D->hasOneUse() && match(M, m_Constant(C))) {
    // Propagating undef is unsafe. Clamp undef elements to -1.
    Type *EltTy = C->getType()->getScalarType();
    C = Constant::replaceUndefsWith(C, ConstantInt::getAllOnesValue(EltTy));
    // Unfold.
    Value *LHS = Builder.CreateAnd(X, C);
    Value *NotC = Builder.CreateNot(C);
    Value *RHS = Builder.CreateAnd(B, NotC);
    return BinaryOperator::CreateOr(LHS, RHS);
  }

  return nullptr;
}

static Instruction *foldNotXor(BinaryOperator &I,
                               InstCombiner::BuilderTy &Builder) {
  Value *X, *Y;
  // FIXME: one-use check is not needed in general, but currently we are unable
  // to fold 'not' into 'icmp', if that 'icmp' has multiple uses. (D35182)
  if (!match(&I, m_Not(m_OneUse(m_Xor(m_Value(X), m_Value(Y))))))
    return nullptr;

  auto hasCommonOperand = [](Value *A, Value *B, Value *C, Value *D) {
    return A == C || A == D || B == C || B == D;
  };

  Value *A, *B, *C, *D;
  // Canonicalize ~((A & B) ^ (A | ?)) -> (A & B) | ~(A | ?)
  // 4 commuted variants
  if (match(X, m_And(m_Value(A), m_Value(B))) &&
      match(Y, m_Or(m_Value(C), m_Value(D))) && hasCommonOperand(A, B, C, D)) {
    Value *NotY = Builder.CreateNot(Y);
    return BinaryOperator::CreateOr(X, NotY);
  };

  // Canonicalize ~((A | ?) ^ (A & B)) -> (A & B) | ~(A | ?)
  // 4 commuted variants
  if (match(Y, m_And(m_Value(A), m_Value(B))) &&
      match(X, m_Or(m_Value(C), m_Value(D))) && hasCommonOperand(A, B, C, D)) {
    Value *NotX = Builder.CreateNot(X);
    return BinaryOperator::CreateOr(Y, NotX);
  };

  return nullptr;
}

/// Canonicalize a shifty way to code absolute value to the more common pattern
/// that uses negation and select.
static Instruction *canonicalizeAbs(BinaryOperator &Xor,
                                    InstCombiner::BuilderTy &Builder) {
  assert(Xor.getOpcode() == Instruction::Xor && "Expected an xor instruction.");

  // There are 4 potential commuted variants. Move the 'ashr' candidate to Op1.
  // We're relying on the fact that we only do this transform when the shift has
  // exactly 2 uses and the add has exactly 1 use (otherwise, we might increase
  // instructions).
  Value *Op0 = Xor.getOperand(0), *Op1 = Xor.getOperand(1);
  if (Op0->hasNUses(2))
    std::swap(Op0, Op1);

  Type *Ty = Xor.getType();
  Value *A;
  const APInt *ShAmt;
  if (match(Op1, m_AShr(m_Value(A), m_APInt(ShAmt))) &&
      Op1->hasNUses(2) && *ShAmt == Ty->getScalarSizeInBits() - 1 &&
      match(Op0, m_OneUse(m_c_Add(m_Specific(A), m_Specific(Op1))))) {
    // Op1 = ashr i32 A, 31   ; smear the sign bit
    // xor (add A, Op1), Op1  ; add -1 and flip bits if negative
    // --> (A < 0) ? -A : A
    Value *IsNeg = Builder.CreateIsNeg(A);
    // Copy the nsw flags from the add to the negate.
    auto *Add = cast<BinaryOperator>(Op0);
    Value *NegA = Add->hasNoUnsignedWrap()
                      ? Constant::getNullValue(A->getType())
                      : Builder.CreateNeg(A, "", Add->hasNoSignedWrap());
    return SelectInst::Create(IsNeg, NegA, A);
  }
  return nullptr;
}

static bool canFreelyInvert(InstCombiner &IC, Value *Op,
                            Instruction *IgnoredUser) {
  auto *I = dyn_cast<Instruction>(Op);
  return I && IC.isFreeToInvert(I, /*WillInvertAllUses=*/true) &&
         IC.canFreelyInvertAllUsersOf(I, IgnoredUser);
}

static Value *freelyInvert(InstCombinerImpl &IC, Value *Op,
                           Instruction *IgnoredUser) {
  auto *I = cast<Instruction>(Op);
  IC.Builder.SetInsertPoint(*I->getInsertionPointAfterDef());
  Value *NotOp = IC.Builder.CreateNot(Op, Op->getName() + ".not");
  Op->replaceUsesWithIf(NotOp,
                        [NotOp](Use &U) { return U.getUser() != NotOp; });
  IC.freelyInvertAllUsersOf(NotOp, IgnoredUser);
  return NotOp;
}

// Transform
//   z = ~(x &/| y)
// into:
//   z = ((~x) |/& (~y))
// iff both x and y are free to invert and all uses of z can be freely updated.
bool InstCombinerImpl::sinkNotIntoLogicalOp(Instruction &I) {
  Value *Op0, *Op1;
  if (!match(&I, m_LogicalOp(m_Value(Op0), m_Value(Op1))))
    return false;

  // If this logic op has not been simplified yet, just bail out and let that
  // happen first. Otherwise, the code below may wrongly invert.
  if (Op0 == Op1)
    return false;

  Instruction::BinaryOps NewOpc =
      match(&I, m_LogicalAnd()) ? Instruction::Or : Instruction::And;
  bool IsBinaryOp = isa<BinaryOperator>(I);

  // Can our users be adapted?
  if (!InstCombiner::canFreelyInvertAllUsersOf(&I, /*IgnoredUser=*/nullptr))
    return false;

  // And can the operands be adapted?
  if (!canFreelyInvert(*this, Op0, &I) || !canFreelyInvert(*this, Op1, &I))
    return false;

  Op0 = freelyInvert(*this, Op0, &I);
  Op1 = freelyInvert(*this, Op1, &I);

  Builder.SetInsertPoint(*I.getInsertionPointAfterDef());
  Value *NewLogicOp;
  if (IsBinaryOp)
    NewLogicOp = Builder.CreateBinOp(NewOpc, Op0, Op1, I.getName() + ".not");
  else
    NewLogicOp =
        Builder.CreateLogicalOp(NewOpc, Op0, Op1, I.getName() + ".not");

  replaceInstUsesWith(I, NewLogicOp);
  // We can not just create an outer `not`, it will most likely be immediately
  // folded back, reconstructing our initial pattern, and causing an
  // infinite combine loop, so immediately manually fold it away.
  freelyInvertAllUsersOf(NewLogicOp);
  return true;
}

// Transform
//   z = (~x) &/| y
// into:
//   z = ~(x |/& (~y))
// iff y is free to invert and all uses of z can be freely updated.
bool InstCombinerImpl::sinkNotIntoOtherHandOfLogicalOp(Instruction &I) {
  Value *Op0, *Op1;
  if (!match(&I, m_LogicalOp(m_Value(Op0), m_Value(Op1))))
    return false;
  Instruction::BinaryOps NewOpc =
      match(&I, m_LogicalAnd()) ? Instruction::Or : Instruction::And;
  bool IsBinaryOp = isa<BinaryOperator>(I);

  Value *NotOp0 = nullptr;
  Value *NotOp1 = nullptr;
  Value **OpToInvert = nullptr;
  if (match(Op0, m_Not(m_Value(NotOp0))) && canFreelyInvert(*this, Op1, &I)) {
    Op0 = NotOp0;
    OpToInvert = &Op1;
  } else if (match(Op1, m_Not(m_Value(NotOp1))) &&
             canFreelyInvert(*this, Op0, &I)) {
    Op1 = NotOp1;
    OpToInvert = &Op0;
  } else
    return false;

  // And can our users be adapted?
  if (!InstCombiner::canFreelyInvertAllUsersOf(&I, /*IgnoredUser=*/nullptr))
    return false;

  *OpToInvert = freelyInvert(*this, *OpToInvert, &I);

  Builder.SetInsertPoint(*I.getInsertionPointAfterDef());
  Value *NewBinOp;
  if (IsBinaryOp)
    NewBinOp = Builder.CreateBinOp(NewOpc, Op0, Op1, I.getName() + ".not");
  else
    NewBinOp = Builder.CreateLogicalOp(NewOpc, Op0, Op1, I.getName() + ".not");
  replaceInstUsesWith(I, NewBinOp);
  // We can not just create an outer `not`, it will most likely be immediately
  // folded back, reconstructing our initial pattern, and causing an
  // infinite combine loop, so immediately manually fold it away.
  freelyInvertAllUsersOf(NewBinOp);
  return true;
}

Instruction *InstCombinerImpl::foldNot(BinaryOperator &I) {
  Value *NotOp;
  if (!match(&I, m_Not(m_Value(NotOp))))
    return nullptr;

  // Apply DeMorgan's Law for 'nand' / 'nor' logic with an inverted operand.
  // We must eliminate the and/or (one-use) for these transforms to not increase
  // the instruction count.
  //
  // ~(~X & Y) --> (X | ~Y)
  // ~(Y & ~X) --> (X | ~Y)
  //
  // Note: The logical matches do not check for the commuted patterns because
  //       those are handled via SimplifySelectsFeedingBinaryOp().
  Type *Ty = I.getType();
  Value *X, *Y;
  if (match(NotOp, m_OneUse(m_c_And(m_Not(m_Value(X)), m_Value(Y))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return BinaryOperator::CreateOr(X, NotY);
  }
  if (match(NotOp, m_OneUse(m_LogicalAnd(m_Not(m_Value(X)), m_Value(Y))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return SelectInst::Create(X, ConstantInt::getTrue(Ty), NotY);
  }

  // ~(~X | Y) --> (X & ~Y)
  // ~(Y | ~X) --> (X & ~Y)
  if (match(NotOp, m_OneUse(m_c_Or(m_Not(m_Value(X)), m_Value(Y))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return BinaryOperator::CreateAnd(X, NotY);
  }
  if (match(NotOp, m_OneUse(m_LogicalOr(m_Not(m_Value(X)), m_Value(Y))))) {
    Value *NotY = Builder.CreateNot(Y, Y->getName() + ".not");
    return SelectInst::Create(X, NotY, ConstantInt::getFalse(Ty));
  }

  // Is this a 'not' (~) fed by a binary operator?
  BinaryOperator *NotVal;
  if (match(NotOp, m_BinOp(NotVal))) {
    // ~((-X) | Y) --> (X - 1) & (~Y)
    if (match(NotVal,
              m_OneUse(m_c_Or(m_OneUse(m_Neg(m_Value(X))), m_Value(Y))))) {
      Value *DecX = Builder.CreateAdd(X, ConstantInt::getAllOnesValue(Ty));
      Value *NotY = Builder.CreateNot(Y);
      return BinaryOperator::CreateAnd(DecX, NotY);
    }

    // ~(~X >>s Y) --> (X >>s Y)
    if (match(NotVal, m_AShr(m_Not(m_Value(X)), m_Value(Y))))
      return BinaryOperator::CreateAShr(X, Y);

    // Treat lshr with non-negative operand as ashr.
    // ~(~X >>u Y) --> (X >>s Y) iff X is known negative
    if (match(NotVal, m_LShr(m_Not(m_Value(X)), m_Value(Y))) &&
        isKnownNegative(X, SQ.getWithInstruction(NotVal)))
      return BinaryOperator::CreateAShr(X, Y);

    // Bit-hack form of a signbit test for iN type:
    // ~(X >>s (N - 1)) --> sext i1 (X > -1) to iN
    unsigned FullShift = Ty->getScalarSizeInBits() - 1;
    if (match(NotVal, m_OneUse(m_AShr(m_Value(X), m_SpecificInt(FullShift))))) {
      Value *IsNotNeg = Builder.CreateIsNotNeg(X, "isnotneg");
      return new SExtInst(IsNotNeg, Ty);
    }

    // If we are inverting a right-shifted constant, we may be able to eliminate
    // the 'not' by inverting the constant and using the opposite shift type.
    // Canonicalization rules ensure that only a negative constant uses 'ashr',
    // but we must check that in case that transform has not fired yet.

    // ~(C >>s Y) --> ~C >>u Y (when inverting the replicated sign bits)
    Constant *C;
    if (match(NotVal, m_AShr(m_Constant(C), m_Value(Y))) &&
        match(C, m_Negative()))
      return BinaryOperator::CreateLShr(ConstantExpr::getNot(C), Y);

    // ~(C >>u Y) --> ~C >>s Y (when inverting the replicated sign bits)
    if (match(NotVal, m_LShr(m_Constant(C), m_Value(Y))) &&
        match(C, m_NonNegative()))
      return BinaryOperator::CreateAShr(ConstantExpr::getNot(C), Y);

    // ~(X + C) --> ~C - X
    if (match(NotVal, m_Add(m_Value(X), m_ImmConstant(C))))
      return BinaryOperator::CreateSub(ConstantExpr::getNot(C), X);

    // ~(X - Y) --> ~X + Y
    // FIXME: is it really beneficial to sink the `not` here?
    if (match(NotVal, m_Sub(m_Value(X), m_Value(Y))))
      if (isa<Constant>(X) || NotVal->hasOneUse())
        return BinaryOperator::CreateAdd(Builder.CreateNot(X), Y);

    // ~(~X + Y) --> X - Y
    if (match(NotVal, m_c_Add(m_Not(m_Value(X)), m_Value(Y))))
      return BinaryOperator::CreateWithCopiedFlags(Instruction::Sub, X, Y,
                                                   NotVal);
  }

  // not (cmp A, B) = !cmp A, B
  CmpInst::Predicate Pred;
  if (match(NotOp, m_Cmp(Pred, m_Value(), m_Value())) &&
      (NotOp->hasOneUse() ||
       InstCombiner::canFreelyInvertAllUsersOf(cast<Instruction>(NotOp),
                                               /*IgnoredUser=*/nullptr))) {
    cast<CmpInst>(NotOp)->setPredicate(CmpInst::getInversePredicate(Pred));
    freelyInvertAllUsersOf(NotOp);
    return &I;
  }

  // Move a 'not' ahead of casts of a bool to enable logic reduction:
  // not (bitcast (sext i1 X)) --> bitcast (sext (not i1 X))
  if (match(NotOp, m_OneUse(m_BitCast(m_OneUse(m_SExt(m_Value(X)))))) && X->getType()->isIntOrIntVectorTy(1)) {
    Type *SextTy = cast<BitCastOperator>(NotOp)->getSrcTy();
    Value *NotX = Builder.CreateNot(X);
    Value *Sext = Builder.CreateSExt(NotX, SextTy);
    return CastInst::CreateBitOrPointerCast(Sext, Ty);
  }

  if (auto *NotOpI = dyn_cast<Instruction>(NotOp))
    if (sinkNotIntoLogicalOp(*NotOpI))
      return &I;

  // Eliminate a bitwise 'not' op of 'not' min/max by inverting the min/max:
  // ~min(~X, ~Y) --> max(X, Y)
  // ~max(~X, Y) --> min(X, ~Y)
  auto *II = dyn_cast<IntrinsicInst>(NotOp);
  if (II && II->hasOneUse()) {
    if (match(NotOp, m_c_MaxOrMin(m_Not(m_Value(X)), m_Value(Y)))) {
      Intrinsic::ID InvID = getInverseMinMaxIntrinsic(II->getIntrinsicID());
      Value *NotY = Builder.CreateNot(Y);
      Value *InvMaxMin = Builder.CreateBinaryIntrinsic(InvID, X, NotY);
      return replaceInstUsesWith(I, InvMaxMin);
    }

    if (II->getIntrinsicID() == Intrinsic::is_fpclass) {
      ConstantInt *ClassMask = cast<ConstantInt>(II->getArgOperand(1));
      II->setArgOperand(
          1, ConstantInt::get(ClassMask->getType(),
                              ~ClassMask->getZExtValue() & fcAllFlags));
      return replaceInstUsesWith(I, II);
    }
  }

  if (NotOp->hasOneUse()) {
    // Pull 'not' into operands of select if both operands are one-use compares
    // or one is one-use compare and the other one is a constant.
    // Inverting the predicates eliminates the 'not' operation.
    // Example:
    //   not (select ?, (cmp TPred, ?, ?), (cmp FPred, ?, ?) -->
    //     select ?, (cmp InvTPred, ?, ?), (cmp InvFPred, ?, ?)
    //   not (select ?, (cmp TPred, ?, ?), true -->
    //     select ?, (cmp InvTPred, ?, ?), false
    if (auto *Sel = dyn_cast<SelectInst>(NotOp)) {
      Value *TV = Sel->getTrueValue();
      Value *FV = Sel->getFalseValue();
      auto *CmpT = dyn_cast<CmpInst>(TV);
      auto *CmpF = dyn_cast<CmpInst>(FV);
      bool InvertibleT = (CmpT && CmpT->hasOneUse()) || isa<Constant>(TV);
      bool InvertibleF = (CmpF && CmpF->hasOneUse()) || isa<Constant>(FV);
      if (InvertibleT && InvertibleF) {
        if (CmpT)
          CmpT->setPredicate(CmpT->getInversePredicate());
        else
          Sel->setTrueValue(ConstantExpr::getNot(cast<Constant>(TV)));
        if (CmpF)
          CmpF->setPredicate(CmpF->getInversePredicate());
        else
          Sel->setFalseValue(ConstantExpr::getNot(cast<Constant>(FV)));
        return replaceInstUsesWith(I, Sel);
      }
    }
  }

  if (Instruction *NewXor = foldNotXor(I, Builder))
    return NewXor;

  // TODO: Could handle multi-use better by checking if all uses of NotOp (other
  // than I) can be inverted.
  if (Value *R = getFreelyInverted(NotOp, NotOp->hasOneUse(), &Builder))
    return replaceInstUsesWith(I, R);

  return nullptr;
}

// FIXME: We use commutative matchers (m_c_*) for some, but not all, matches
// here. We should standardize that construct where it is needed or choose some
// other way to ensure that commutated variants of patterns are not missed.
Instruction *InstCombinerImpl::visitXor(BinaryOperator &I) {
  if (Value *V = simplifyXorInst(I.getOperand(0), I.getOperand(1),
                                 SQ.getWithInstruction(&I)))
    return replaceInstUsesWith(I, V);

  if (SimplifyAssociativeOrCommutative(I))
    return &I;

  if (Instruction *X = foldVectorBinop(I))
    return X;

  if (Instruction *Phi = foldBinopWithPhiOperands(I))
    return Phi;

  if (Instruction *NewXor = foldXorToXor(I, Builder))
    return NewXor;

  // (A&B)^(A&C) -> A&(B^C) etc
  if (Value *V = foldUsingDistributiveLaws(I))
    return replaceInstUsesWith(I, V);

  // See if we can simplify any instructions used by the instruction whose sole
  // purpose is to compute bits we don't care about.
  if (SimplifyDemandedInstructionBits(I))
    return &I;

  if (Instruction *R = foldNot(I))
    return R;

  if (Instruction *R = foldBinOpShiftWithShift(I))
    return R;

  // Fold (X & M) ^ (Y & ~M) -> (X & M) | (Y & ~M)
  // This it a special case in haveNoCommonBitsSet, but the computeKnownBits
  // calls in there are unnecessary as SimplifyDemandedInstructionBits should
  // have already taken care of those cases.
  Value *Op0 = I.getOperand(0), *Op1 = I.getOperand(1);
  Value *M;
  if (match(&I, m_c_Xor(m_c_And(m_Not(m_Value(M)), m_Value()),
                        m_c_And(m_Deferred(M), m_Value())))) {
    if (isGuaranteedNotToBeUndef(M))
      return BinaryOperator::CreateDisjointOr(Op0, Op1);
    else
      return BinaryOperator::CreateOr(Op0, Op1);
  }

  if (Instruction *Xor = visitMaskedMerge(I, Builder))
    return Xor;

  Value *X, *Y;
  Constant *C1;
  if (match(Op1, m_Constant(C1))) {
    Constant *C2;

    if (match(Op0, m_OneUse(m_Or(m_Value(X), m_ImmConstant(C2)))) &&
        match(C1, m_ImmConstant())) {
      // (X | C2) ^ C1 --> (X & ~C2) ^ (C1^C2)
      C2 = Constant::replaceUndefsWith(
          C2, Constant::getAllOnesValue(C2->getType()->getScalarType()));
      Value *And = Builder.CreateAnd(
          X, Constant::mergeUndefsWith(ConstantExpr::getNot(C2), C1));
      return BinaryOperator::CreateXor(
          And, Constant::mergeUndefsWith(ConstantExpr::getXor(C1, C2), C1));
    }

    // Use DeMorgan and reassociation to eliminate a 'not' op.
    if (match(Op0, m_OneUse(m_Or(m_Not(m_Value(X)), m_Constant(C2))))) {
      // (~X | C2) ^ C1 --> ((X & ~C2) ^ -1) ^ C1 --> (X & ~C2) ^ ~C1
      Value *And = Builder.CreateAnd(X, ConstantExpr::getNot(C2));
      return BinaryOperator::CreateXor(And, ConstantExpr::getNot(C1));
    }
    if (match(Op0, m_OneUse(m_And(m_Not(m_Value(X)), m_Constant(C2))))) {
      // (~X & C2) ^ C1 --> ((X | ~C2) ^ -1) ^ C1 --> (X | ~C2) ^ ~C1
      Value *Or = Builder.CreateOr(X, ConstantExpr::getNot(C2));
      return BinaryOperator::CreateXor(Or, ConstantExpr::getNot(C1));
    }

    // Convert xor ([trunc] (ashr X, BW-1)), C =>
    //   select(X >s -1, C, ~C)
    // The ashr creates "AllZeroOrAllOne's", which then optionally inverses the
    // constant depending on whether this input is less than 0.
    const APInt *CA;
    if (match(Op0, m_OneUse(m_TruncOrSelf(
                       m_AShr(m_Value(X), m_APIntAllowPoison(CA))))) &&
        *CA == X->getType()->getScalarSizeInBits() - 1 &&
        !match(C1, m_AllOnes())) {
      assert(!C1->isZeroValue() && "Unexpected xor with 0");
      Value *IsNotNeg = Builder.CreateIsNotNeg(X);
      return SelectInst::Create(IsNotNeg, Op1, Builder.CreateNot(Op1));
    }
  }

  Type *Ty = I.getType();
  {
    const APInt *RHSC;
    if (match(Op1, m_APInt(RHSC))) {
      Value *X;
      const APInt *C;
      // (C - X) ^ signmaskC --> (C + signmaskC) - X
      if (RHSC->isSignMask() && match(Op0, m_Sub(m_APInt(C), m_Value(X))))
        return BinaryOperator::CreateSub(ConstantInt::get(Ty, *C + *RHSC), X);

      // (X + C) ^ signmaskC --> X + (C + signmaskC)
      if (RHSC->isSignMask() && match(Op0, m_Add(m_Value(X), m_APInt(C))))
        return BinaryOperator::CreateAdd(X, ConstantInt::get(Ty, *C + *RHSC));

      // (X | C) ^ RHSC --> X ^ (C ^ RHSC) iff X & C == 0
      if (match(Op0, m_Or(m_Value(X), m_APInt(C))) &&
          MaskedValueIsZero(X, *C, 0, &I))
        return BinaryOperator::CreateXor(X, ConstantInt::get(Ty, *C ^ *RHSC));

      // When X is a power-of-two or zero and zero input is poison:
      // ctlz(i32 X) ^ 31 --> cttz(X)
      // cttz(i32 X) ^ 31 --> ctlz(X)
      auto *II = dyn_cast<IntrinsicInst>(Op0);
      if (II && II->hasOneUse() && *RHSC == Ty->getScalarSizeInBits() - 1) {
        Intrinsic::ID IID = II->getIntrinsicID();
        if ((IID == Intrinsic::ctlz || IID == Intrinsic::cttz) &&
            match(II->getArgOperand(1), m_One()) &&
            isKnownToBeAPowerOfTwo(II->getArgOperand(0), /*OrZero */ true)) {
          IID = (IID == Intrinsic::ctlz) ? Intrinsic::cttz : Intrinsic::ctlz;
          Function *F = Intrinsic::getDeclaration(II->getModule(), IID, Ty);
          return CallInst::Create(F, {II->getArgOperand(0), Builder.getTrue()});
        }
      }

      // If RHSC is inverting the remaining bits of shifted X,
      // canonicalize to a 'not' before the shift to help SCEV and codegen:
      // (X << C) ^ RHSC --> ~X << C
      if (match(Op0, m_OneUse(m_Shl(m_Value(X), m_APInt(C)))) &&
          *RHSC == APInt::getAllOnes(Ty->getScalarSizeInBits()).shl(*C)) {
        Value *NotX = Builder.CreateNot(X);
        return BinaryOperator::CreateShl(NotX, ConstantInt::get(Ty, *C));
      }
      // (X >>u C) ^ RHSC --> ~X >>u C
      if (match(Op0, m_OneUse(m_LShr(m_Value(X), m_APInt(C)))) &&
          *RHSC == APInt::getAllOnes(Ty->getScalarSizeInBits()).lshr(*C)) {
        Value *NotX = Builder.CreateNot(X);
        return BinaryOperator::CreateLShr(NotX, ConstantInt::get(Ty, *C));
      }
      // TODO: We could handle 'ashr' here as well. That would be matching
      //       a 'not' op and moving it before the shift. Doing that requires
      //       preventing the inverse fold in canShiftBinOpWithConstantRHS().
    }

    // If we are XORing the sign bit of a floating-point value, convert
    // this to fneg, then cast back to integer.
    //
    // This is generous interpretation of noimplicitfloat, this is not a true
    // floating-point operation.
    //
    // Assumes any IEEE-represented type has the sign bit in the high bit.
    // TODO: Unify with APInt matcher. This version allows undef unlike m_APInt
    Value *CastOp;
    if (match(Op0, m_ElementWiseBitCast(m_Value(CastOp))) &&
        match(Op1, m_SignMask()) &&
        !Builder.GetInsertBlock()->getParent()->hasFnAttribute(
            Attribute::NoImplicitFloat)) {
      Type *EltTy = CastOp->getType()->getScalarType();
      if (EltTy->isFloatingPointTy() && EltTy->isIEEE()) {
        Value *FNeg = Builder.CreateFNeg(CastOp);
        return new BitCastInst(FNeg, I.getType());
      }
    }
  }

  // FIXME: This should not be limited to scalar (pull into APInt match above).
  {
    Value *X;
    ConstantInt *C1, *C2, *C3;
    // ((X^C1) >> C2) ^ C3 -> (X>>C2) ^ ((C1>>C2)^C3)
    if (match(Op1, m_ConstantInt(C3)) &&
        match(Op0, m_LShr(m_Xor(m_Value(X), m_ConstantInt(C1)),
                          m_ConstantInt(C2))) &&
        Op0->hasOneUse()) {
      // fold (C1 >> C2) ^ C3
      APInt FoldConst = C1->getValue().lshr(C2->getValue());
      FoldConst ^= C3->getValue();
      // Prepare the two operands.
      auto *Opnd0 = Builder.CreateLShr(X, C2);
      Opnd0->takeName(Op0);
      return BinaryOperator::CreateXor(Opnd0, ConstantInt::get(Ty, FoldConst));
    }
  }

  if (Instruction *FoldedLogic = foldBinOpIntoSelectOrPhi(I))
    return FoldedLogic;

  // Y ^ (X | Y) --> X & ~Y
  // Y ^ (Y | X) --> X & ~Y
  if (match(Op1, m_OneUse(m_c_Or(m_Value(X), m_Specific(Op0)))))
    return BinaryOperator::CreateAnd(X, Builder.CreateNot(Op0));
  // (X | Y) ^ Y --> X & ~Y
  // (Y | X) ^ Y --> X & ~Y
  if (match(Op0, m_OneUse(m_c_Or(m_Value(X), m_Specific(Op1)))))
    return BinaryOperator::CreateAnd(X, Builder.CreateNot(Op1));

  // Y ^ (X & Y) --> ~X & Y
  // Y ^ (Y & X) --> ~X & Y
  if (match(Op1, m_OneUse(m_c_And(m_Value(X), m_Specific(Op0)))))
    return BinaryOperator::CreateAnd(Op0, Builder.CreateNot(X));
  // (X & Y) ^ Y --> ~X & Y
  // (Y & X) ^ Y --> ~X & Y
  // Canonical form is (X & C) ^ C; don't touch that.
  // TODO: A 'not' op is better for analysis and codegen, but demanded bits must
  //       be fixed to prefer that (otherwise we get infinite looping).
  if (!match(Op1, m_Constant()) &&
      match(Op0, m_OneUse(m_c_And(m_Value(X), m_Specific(Op1)))))
    return BinaryOperator::CreateAnd(Op1, Builder.CreateNot(X));

  Value *A, *B, *C;
  // (A ^ B) ^ (A | C) --> (~A & C) ^ B -- There are 4 commuted variants.
  if (match(&I, m_c_Xor(m_OneUse(m_Xor(m_Value(A), m_Value(B))),
                        m_OneUse(m_c_Or(m_Deferred(A), m_Value(C))))))
      return BinaryOperator::CreateXor(
          Builder.CreateAnd(Builder.CreateNot(A), C), B);

  // (A ^ B) ^ (B | C) --> (~B & C) ^ A -- There are 4 commuted variants.
  if (match(&I, m_c_Xor(m_OneUse(m_Xor(m_Value(A), m_Value(B))),
                        m_OneUse(m_c_Or(m_Deferred(B), m_Value(C))))))
      return BinaryOperator::CreateXor(
          Builder.CreateAnd(Builder.CreateNot(B), C), A);

  // (A & B) ^ (A ^ B) -> (A | B)
  if (match(Op0, m_And(m_Value(A), m_Value(B))) &&
      match(Op1, m_c_Xor(m_Specific(A), m_Specific(B))))
    return BinaryOperator::CreateOr(A, B);
  // (A ^ B) ^ (A & B) -> (A | B)
  if (match(Op0, m_Xor(m_Value(A), m_Value(B))) &&
      match(Op1, m_c_And(m_Specific(A), m_Specific(B))))
    return BinaryOperator::CreateOr(A, B);

  // (A & ~B) ^ ~A -> ~(A & B)
  // (~B & A) ^ ~A -> ~(A & B)
  if (match(Op0, m_c_And(m_Value(A), m_Not(m_Value(B)))) &&
      match(Op1, m_Not(m_Specific(A))))
    return BinaryOperator::CreateNot(Builder.CreateAnd(A, B));

  // (~A & B) ^ A --> A | B -- There are 4 commuted variants.
  if (match(&I, m_c_Xor(m_c_And(m_Not(m_Value(A)), m_Value(B)), m_Deferred(A))))
    return BinaryOperator::CreateOr(A, B);

  // (~A | B) ^ A --> ~(A & B)
  if (match(Op0, m_OneUse(m_c_Or(m_Not(m_Specific(Op1)), m_Value(B)))))
    return BinaryOperator::CreateNot(Builder.CreateAnd(Op1, B));

  // A ^ (~A | B) --> ~(A & B)
  if (match(Op1, m_OneUse(m_c_Or(m_Not(m_Specific(Op0)), m_Value(B)))))
    return BinaryOperator::CreateNot(Builder.CreateAnd(Op0, B));

  // (A | B) ^ (A | C) --> (B ^ C) & ~A -- There are 4 commuted variants.
  // TODO: Loosen one-use restriction if common operand is a constant.
  Value *D;
  if (match(Op0, m_OneUse(m_Or(m_Value(A), m_Value(B)))) &&
      match(Op1, m_OneUse(m_Or(m_Value(C), m_Value(D))))) {
    if (B == C || B == D)
      std::swap(A, B);
    if (A == C)
      std::swap(C, D);
    if (A == D) {
      Value *NotA = Builder.CreateNot(A);
      return BinaryOperator::CreateAnd(Builder.CreateXor(B, C), NotA);
    }
  }

  // (A & B) ^ (A | C) --> A ? ~B : C -- There are 4 commuted variants.
  if (I.getType()->isIntOrIntVectorTy(1) &&
      match(Op0, m_OneUse(m_LogicalAnd(m_Value(A), m_Value(B)))) &&
      match(Op1, m_OneUse(m_LogicalOr(m_Value(C), m_Value(D))))) {
    bool NeedFreeze = isa<SelectInst>(Op0) && isa<SelectInst>(Op1) && B == D;
    if (B == C || B == D)
      std::swap(A, B);
    if (A == C)
      std::swap(C, D);
    if (A == D) {
      if (NeedFreeze)
        A = Builder.CreateFreeze(A);
      Value *NotB = Builder.CreateNot(B);
      return SelectInst::Create(A, NotB, C);
    }
  }

  if (auto *LHS = dyn_cast<ICmpInst>(I.getOperand(0)))
    if (auto *RHS = dyn_cast<ICmpInst>(I.getOperand(1)))
      if (Value *V = foldXorOfICmps(LHS, RHS, I))
        return replaceInstUsesWith(I, V);

  if (Instruction *CastedXor = foldCastedBitwiseLogic(I))
    return CastedXor;

  if (Instruction *Abs = canonicalizeAbs(I, Builder))
    return Abs;

  // Otherwise, if all else failed, try to hoist the xor-by-constant:
  //   (X ^ C) ^ Y --> (X ^ Y) ^ C
  // Just like we do in other places, we completely avoid the fold
  // for constantexprs, at least to avoid endless combine loop.
  if (match(&I, m_c_Xor(m_OneUse(m_Xor(m_CombineAnd(m_Value(X),
                                                    m_Unless(m_ConstantExpr())),
                                       m_ImmConstant(C1))),
                        m_Value(Y))))
    return BinaryOperator::CreateXor(Builder.CreateXor(X, Y), C1);

  if (Instruction *R = reassociateForUses(I, Builder))
    return R;

  if (Instruction *Canonicalized = canonicalizeLogicFirst(I, Builder))
    return Canonicalized;

  if (Instruction *Folded = foldLogicOfIsFPClass(I, Op0, Op1))
    return Folded;

  if (Instruction *Folded = canonicalizeConditionalNegationViaMathToSelect(I))
    return Folded;

  if (Instruction *Res = foldBinOpOfDisplacedShifts(I))
    return Res;

  if (Instruction *Res = foldBitwiseLogicWithIntrinsics(I, Builder))
    return Res;

  return nullptr;
}
