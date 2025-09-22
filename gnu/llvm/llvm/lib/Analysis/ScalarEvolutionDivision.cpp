//===- ScalarEvolutionDivision.h - See below --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the class that knows how to divide SCEV's.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolutionDivision.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>

namespace llvm {
class Type;
} // namespace llvm

using namespace llvm;

namespace {

static inline int sizeOfSCEV(const SCEV *S) {
  struct FindSCEVSize {
    int Size = 0;

    FindSCEVSize() = default;

    bool follow(const SCEV *S) {
      ++Size;
      // Keep looking at all operands of S.
      return true;
    }

    bool isDone() const { return false; }
  };

  FindSCEVSize F;
  SCEVTraversal<FindSCEVSize> ST(F);
  ST.visitAll(S);
  return F.Size;
}

} // namespace

// Computes the Quotient and Remainder of the division of Numerator by
// Denominator.
void SCEVDivision::divide(ScalarEvolution &SE, const SCEV *Numerator,
                          const SCEV *Denominator, const SCEV **Quotient,
                          const SCEV **Remainder) {
  assert(Numerator && Denominator && "Uninitialized SCEV");

  SCEVDivision D(SE, Numerator, Denominator);

  // Check for the trivial case here to avoid having to check for it in the
  // rest of the code.
  if (Numerator == Denominator) {
    *Quotient = D.One;
    *Remainder = D.Zero;
    return;
  }

  if (Numerator->isZero()) {
    *Quotient = D.Zero;
    *Remainder = D.Zero;
    return;
  }

  // A simple case when N/1. The quotient is N.
  if (Denominator->isOne()) {
    *Quotient = Numerator;
    *Remainder = D.Zero;
    return;
  }

  // Split the Denominator when it is a product.
  if (const SCEVMulExpr *T = dyn_cast<SCEVMulExpr>(Denominator)) {
    const SCEV *Q, *R;
    *Quotient = Numerator;
    for (const SCEV *Op : T->operands()) {
      divide(SE, *Quotient, Op, &Q, &R);
      *Quotient = Q;

      // Bail out when the Numerator is not divisible by one of the terms of
      // the Denominator.
      if (!R->isZero()) {
        *Quotient = D.Zero;
        *Remainder = Numerator;
        return;
      }
    }
    *Remainder = D.Zero;
    return;
  }

  D.visit(Numerator);
  *Quotient = D.Quotient;
  *Remainder = D.Remainder;
}

void SCEVDivision::visitConstant(const SCEVConstant *Numerator) {
  if (const SCEVConstant *D = dyn_cast<SCEVConstant>(Denominator)) {
    APInt NumeratorVal = Numerator->getAPInt();
    APInt DenominatorVal = D->getAPInt();
    uint32_t NumeratorBW = NumeratorVal.getBitWidth();
    uint32_t DenominatorBW = DenominatorVal.getBitWidth();

    if (NumeratorBW > DenominatorBW)
      DenominatorVal = DenominatorVal.sext(NumeratorBW);
    else if (NumeratorBW < DenominatorBW)
      NumeratorVal = NumeratorVal.sext(DenominatorBW);

    APInt QuotientVal(NumeratorVal.getBitWidth(), 0);
    APInt RemainderVal(NumeratorVal.getBitWidth(), 0);
    APInt::sdivrem(NumeratorVal, DenominatorVal, QuotientVal, RemainderVal);
    Quotient = SE.getConstant(QuotientVal);
    Remainder = SE.getConstant(RemainderVal);
    return;
  }
}

void SCEVDivision::visitVScale(const SCEVVScale *Numerator) {
  return cannotDivide(Numerator);
}

void SCEVDivision::visitAddRecExpr(const SCEVAddRecExpr *Numerator) {
  const SCEV *StartQ, *StartR, *StepQ, *StepR;
  if (!Numerator->isAffine())
    return cannotDivide(Numerator);
  divide(SE, Numerator->getStart(), Denominator, &StartQ, &StartR);
  divide(SE, Numerator->getStepRecurrence(SE), Denominator, &StepQ, &StepR);
  // Bail out if the types do not match.
  Type *Ty = Denominator->getType();
  if (Ty != StartQ->getType() || Ty != StartR->getType() ||
      Ty != StepQ->getType() || Ty != StepR->getType())
    return cannotDivide(Numerator);
  Quotient = SE.getAddRecExpr(StartQ, StepQ, Numerator->getLoop(),
                              Numerator->getNoWrapFlags());
  Remainder = SE.getAddRecExpr(StartR, StepR, Numerator->getLoop(),
                               Numerator->getNoWrapFlags());
}

void SCEVDivision::visitAddExpr(const SCEVAddExpr *Numerator) {
  SmallVector<const SCEV *, 2> Qs, Rs;
  Type *Ty = Denominator->getType();

  for (const SCEV *Op : Numerator->operands()) {
    const SCEV *Q, *R;
    divide(SE, Op, Denominator, &Q, &R);

    // Bail out if types do not match.
    if (Ty != Q->getType() || Ty != R->getType())
      return cannotDivide(Numerator);

    Qs.push_back(Q);
    Rs.push_back(R);
  }

  if (Qs.size() == 1) {
    Quotient = Qs[0];
    Remainder = Rs[0];
    return;
  }

  Quotient = SE.getAddExpr(Qs);
  Remainder = SE.getAddExpr(Rs);
}

void SCEVDivision::visitMulExpr(const SCEVMulExpr *Numerator) {
  SmallVector<const SCEV *, 2> Qs;
  Type *Ty = Denominator->getType();

  bool FoundDenominatorTerm = false;
  for (const SCEV *Op : Numerator->operands()) {
    // Bail out if types do not match.
    if (Ty != Op->getType())
      return cannotDivide(Numerator);

    if (FoundDenominatorTerm) {
      Qs.push_back(Op);
      continue;
    }

    // Check whether Denominator divides one of the product operands.
    const SCEV *Q, *R;
    divide(SE, Op, Denominator, &Q, &R);
    if (!R->isZero()) {
      Qs.push_back(Op);
      continue;
    }

    // Bail out if types do not match.
    if (Ty != Q->getType())
      return cannotDivide(Numerator);

    FoundDenominatorTerm = true;
    Qs.push_back(Q);
  }

  if (FoundDenominatorTerm) {
    Remainder = Zero;
    if (Qs.size() == 1)
      Quotient = Qs[0];
    else
      Quotient = SE.getMulExpr(Qs);
    return;
  }

  if (!isa<SCEVUnknown>(Denominator))
    return cannotDivide(Numerator);

  // The Remainder is obtained by replacing Denominator by 0 in Numerator.
  ValueToSCEVMapTy RewriteMap;
  RewriteMap[cast<SCEVUnknown>(Denominator)->getValue()] = Zero;
  Remainder = SCEVParameterRewriter::rewrite(Numerator, SE, RewriteMap);

  if (Remainder->isZero()) {
    // The Quotient is obtained by replacing Denominator by 1 in Numerator.
    RewriteMap[cast<SCEVUnknown>(Denominator)->getValue()] = One;
    Quotient = SCEVParameterRewriter::rewrite(Numerator, SE, RewriteMap);
    return;
  }

  // Quotient is (Numerator - Remainder) divided by Denominator.
  const SCEV *Q, *R;
  const SCEV *Diff = SE.getMinusSCEV(Numerator, Remainder);
  // This SCEV does not seem to simplify: fail the division here.
  if (sizeOfSCEV(Diff) > sizeOfSCEV(Numerator))
    return cannotDivide(Numerator);
  divide(SE, Diff, Denominator, &Q, &R);
  if (R != Zero)
    return cannotDivide(Numerator);
  Quotient = Q;
}

SCEVDivision::SCEVDivision(ScalarEvolution &S, const SCEV *Numerator,
                           const SCEV *Denominator)
    : SE(S), Denominator(Denominator) {
  Zero = SE.getZero(Denominator->getType());
  One = SE.getOne(Denominator->getType());

  // We generally do not know how to divide Expr by Denominator. We initialize
  // the division to a "cannot divide" state to simplify the rest of the code.
  cannotDivide(Numerator);
}

// Convenience function for giving up on the division. We set the quotient to
// be equal to zero and the remainder to be equal to the numerator.
void SCEVDivision::cannotDivide(const SCEV *Numerator) {
  Quotient = Zero;
  Remainder = Numerator;
}
