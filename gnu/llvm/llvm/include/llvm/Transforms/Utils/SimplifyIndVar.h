//===-- llvm/Transforms/Utils/SimplifyIndVar.h - Indvar Utils ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines in interface for induction variable simplification. It does
// not define any actual pass or policy, but provides a single function to
// simplify a loop's induction variables based on ScalarEvolution.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H

#include <utility>

namespace llvm {

class Type;
class WeakTrackingVH;
template <typename T> class SmallVectorImpl;
class CastInst;
class DominatorTree;
class Loop;
class LoopInfo;
class PHINode;
class ScalarEvolution;
class SCEVExpander;
class TargetTransformInfo;

/// Interface for visiting interesting IV users that are recognized but not
/// simplified by this utility.
class IVVisitor {
protected:
  const DominatorTree *DT = nullptr;

  virtual void anchor();

public:
  IVVisitor() = default;
  virtual ~IVVisitor() = default;

  const DominatorTree *getDomTree() const { return DT; }
  virtual void visitCast(CastInst *Cast) = 0;
};

/// simplifyUsersOfIV - Simplify instructions that use this induction variable
/// by using ScalarEvolution to analyze the IV's recurrence. Returns a pair
/// where the first entry indicates that the function makes changes and the
/// second entry indicates that it introduced new opportunities for loop
/// unswitching.
std::pair<bool, bool> simplifyUsersOfIV(PHINode *CurrIV, ScalarEvolution *SE,
                                        DominatorTree *DT, LoopInfo *LI,
                                        const TargetTransformInfo *TTI,
                                        SmallVectorImpl<WeakTrackingVH> &Dead,
                                        SCEVExpander &Rewriter,
                                        IVVisitor *V = nullptr);

/// SimplifyLoopIVs - Simplify users of induction variables within this
/// loop. This does not actually change or add IVs.
bool simplifyLoopIVs(Loop *L, ScalarEvolution *SE, DominatorTree *DT,
                     LoopInfo *LI, const TargetTransformInfo *TTI,
                     SmallVectorImpl<WeakTrackingVH> &Dead);

/// Collect information about induction variables that are used by sign/zero
/// extend operations. This information is recorded by CollectExtend and provides
/// the input to WidenIV.
struct WideIVInfo {
  PHINode *NarrowIV = nullptr;

  // Widest integer type created [sz]ext
  Type *WidestNativeType = nullptr;

  // Was a sext user seen before a zext?
  bool IsSigned = false;
};

/// Widen Induction Variables - Extend the width of an IV to cover its
/// widest uses.
PHINode *createWideIV(const WideIVInfo &WI,
    LoopInfo *LI, ScalarEvolution *SE, SCEVExpander &Rewriter,
    DominatorTree *DT, SmallVectorImpl<WeakTrackingVH> &DeadInsts,
    unsigned &NumElimExt, unsigned &NumWidened,
    bool HasGuards, bool UsePostIncrementRanges);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
