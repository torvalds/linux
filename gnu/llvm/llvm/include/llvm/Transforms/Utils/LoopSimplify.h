//===- LoopSimplify.h - Loop Canonicalization Pass --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs several transformations to transform natural loops into a
// simpler form, which makes subsequent analyses and transformations simpler and
// more effective.
//
// Loop pre-header insertion guarantees that there is a single, non-critical
// entry edge from outside of the loop to the loop header.  This simplifies a
// number of analyses and transformations, such as LICM.
//
// Loop exit-block insertion guarantees that all exit blocks from the loop
// (blocks which are outside of the loop that have predecessors inside of the
// loop) only have predecessors from inside of the loop (and are thus dominated
// by the loop header).  This simplifies transformations such as store-sinking
// that are built into LICM.
//
// This pass also guarantees that loops will have exactly one backedge.
//
// Indirectbr instructions introduce several complications. If the loop
// contains or is entered by an indirectbr instruction, it may not be possible
// to transform the loop and make these guarantees. Client code should check
// that these conditions are true before relying on them.
//
// Note that the simplifycfg pass will clean up blocks which are split out but
// end up being unnecessary, so usage of this pass should not pessimize
// generated code.
//
// This pass obviously modifies the CFG, but updates loop information and
// dominator information.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_LOOPSIMPLIFY_H
#define LLVM_TRANSFORMS_UTILS_LOOPSIMPLIFY_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class AssumptionCache;
class DominatorTree;
class Loop;
class LoopInfo;
class MemorySSAUpdater;
class ScalarEvolution;

/// This pass is responsible for loop canonicalization.
class LoopSimplifyPass : public PassInfoMixin<LoopSimplifyPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Simplify each loop in a loop nest recursively.
///
/// This takes a potentially un-simplified loop L (and its children) and turns
/// it into a simplified loop nest with preheaders and single backedges. It will
/// update \c DominatorTree, \c LoopInfo, \c ScalarEvolution and \c MemorySSA
/// analyses if they're non-null, and LCSSA if \c PreserveLCSSA is true.
bool simplifyLoop(Loop *L, DominatorTree *DT, LoopInfo *LI, ScalarEvolution *SE,
                  AssumptionCache *AC, MemorySSAUpdater *MSSAU,
                  bool PreserveLCSSA);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_LOOPSIMPLIFY_H
