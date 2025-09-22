//===- ScalarEvolutionNormalization.cpp - See below -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities for working with "normalized" expressions.
// See the comments at the top of ScalarEvolutionNormalization.h for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ScalarEvolutionNormalization.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
using namespace llvm;

/// TransformKind - Different types of transformations that
/// TransformForPostIncUse can do.
enum TransformKind {
  /// Normalize - Normalize according to the given loops.
  Normalize,
  /// Denormalize - Perform the inverse transform on the expression with the
  /// given loop set.
  Denormalize
};

namespace {
struct NormalizeDenormalizeRewriter
    : public SCEVRewriteVisitor<NormalizeDenormalizeRewriter> {
  const TransformKind Kind;

  // NB! Pred is a function_ref.  Storing it here is okay only because
  // we're careful about the lifetime of NormalizeDenormalizeRewriter.
  const NormalizePredTy Pred;

  NormalizeDenormalizeRewriter(TransformKind Kind, NormalizePredTy Pred,
                               ScalarEvolution &SE)
      : SCEVRewriteVisitor<NormalizeDenormalizeRewriter>(SE), Kind(Kind),
        Pred(Pred) {}
  const SCEV *visitAddRecExpr(const SCEVAddRecExpr *Expr);
};
} // namespace

const SCEV *
NormalizeDenormalizeRewriter::visitAddRecExpr(const SCEVAddRecExpr *AR) {
  SmallVector<const SCEV *, 8> Operands;

  transform(AR->operands(), std::back_inserter(Operands),
            [&](const SCEV *Op) { return visit(Op); });

  if (!Pred(AR))
    return SE.getAddRecExpr(Operands, AR->getLoop(), SCEV::FlagAnyWrap);

  // Normalization and denormalization are fancy names for decrementing and
  // incrementing a SCEV expression with respect to a set of loops.  Since
  // Pred(AR) has returned true, we know we need to normalize or denormalize AR
  // with respect to its loop.

  if (Kind == Denormalize) {
    // Denormalization / "partial increment" is essentially the same as \c
    // SCEVAddRecExpr::getPostIncExpr.  Here we use an explicit loop to make the
    // symmetry with Normalization clear.
    for (int i = 0, e = Operands.size() - 1; i < e; i++)
      Operands[i] = SE.getAddExpr(Operands[i], Operands[i + 1]);
  } else {
    assert(Kind == Normalize && "Only two possibilities!");

    // Normalization / "partial decrement" is a bit more subtle.  Since
    // incrementing a SCEV expression (in general) changes the step of the SCEV
    // expression as well, we cannot use the step of the current expression.
    // Instead, we have to use the step of the very expression we're trying to
    // compute!
    //
    // We solve the issue by recursively building up the result, starting from
    // the "least significant" operand in the add recurrence:
    //
    // Base case:
    //   Single operand add recurrence.  It's its own normalization.
    //
    // N-operand case:
    //   {S_{N-1},+,S_{N-2},+,...,+,S_0} = S
    //
    //   Since the step recurrence of S is {S_{N-2},+,...,+,S_0}, we know its
    //   normalization by induction.  We subtract the normalized step
    //   recurrence from S_{N-1} to get the normalization of S.

    for (int i = Operands.size() - 2; i >= 0; i--)
      Operands[i] = SE.getMinusSCEV(Operands[i], Operands[i + 1]);
  }

  return SE.getAddRecExpr(Operands, AR->getLoop(), SCEV::FlagAnyWrap);
}

const SCEV *llvm::normalizeForPostIncUse(const SCEV *S,
                                         const PostIncLoopSet &Loops,
                                         ScalarEvolution &SE,
                                         bool CheckInvertible) {
  if (Loops.empty())
    return S;
  auto Pred = [&](const SCEVAddRecExpr *AR) {
    return Loops.count(AR->getLoop());
  };
  const SCEV *Normalized =
      NormalizeDenormalizeRewriter(Normalize, Pred, SE).visit(S);
  const SCEV *Denormalized = denormalizeForPostIncUse(Normalized, Loops, SE);
  // If the normalized expression isn't invertible.
  if (CheckInvertible && Denormalized != S)
    return nullptr;
  return Normalized;
}

const SCEV *llvm::normalizeForPostIncUseIf(const SCEV *S, NormalizePredTy Pred,
                                           ScalarEvolution &SE) {
  return NormalizeDenormalizeRewriter(Normalize, Pred, SE).visit(S);
}

const SCEV *llvm::denormalizeForPostIncUse(const SCEV *S,
                                           const PostIncLoopSet &Loops,
                                           ScalarEvolution &SE) {
  if (Loops.empty())
    return S;
  auto Pred = [&](const SCEVAddRecExpr *AR) {
    return Loops.count(AR->getLoop());
  };
  return NormalizeDenormalizeRewriter(Denormalize, Pred, SE).visit(S);
}
