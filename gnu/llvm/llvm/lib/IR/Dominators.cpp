//===- Dominators.cpp - Dominator Calculation -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements simple dominator construction algorithms for finding
// forward dominators.  Postdominators are available in libanalysis, but are not
// included in libvmcore, because it's not needed.  Forward dominators are
// needed to support the Verifier pass.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Dominators.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/GenericDomTreeConstruction.h"
#include "llvm/Support/raw_ostream.h"

#include <cassert>

namespace llvm {
class Argument;
class Constant;
class Value;
} // namespace llvm
using namespace llvm;

bool llvm::VerifyDomInfo = false;
static cl::opt<bool, true>
    VerifyDomInfoX("verify-dom-info", cl::location(VerifyDomInfo), cl::Hidden,
                   cl::desc("Verify dominator info (time consuming)"));

#ifdef EXPENSIVE_CHECKS
static constexpr bool ExpensiveChecksEnabled = true;
#else
static constexpr bool ExpensiveChecksEnabled = false;
#endif

bool BasicBlockEdge::isSingleEdge() const {
  unsigned NumEdgesToEnd = 0;
  for (const BasicBlock *Succ : successors(Start)) {
    if (Succ == End)
      ++NumEdgesToEnd;
    if (NumEdgesToEnd >= 2)
      return false;
  }
  assert(NumEdgesToEnd == 1);
  return true;
}

//===----------------------------------------------------------------------===//
//  DominatorTree Implementation
//===----------------------------------------------------------------------===//
//
// Provide public access to DominatorTree information.  Implementation details
// can be found in Dominators.h, GenericDomTree.h, and
// GenericDomTreeConstruction.h.
//
//===----------------------------------------------------------------------===//

template class llvm::DomTreeNodeBase<BasicBlock>;
template class llvm::DominatorTreeBase<BasicBlock, false>; // DomTreeBase
template class llvm::DominatorTreeBase<BasicBlock, true>; // PostDomTreeBase

template class llvm::cfg::Update<BasicBlock *>;

template void llvm::DomTreeBuilder::Calculate<DomTreeBuilder::BBDomTree>(
    DomTreeBuilder::BBDomTree &DT);
template void
llvm::DomTreeBuilder::CalculateWithUpdates<DomTreeBuilder::BBDomTree>(
    DomTreeBuilder::BBDomTree &DT, BBUpdates U);

template void llvm::DomTreeBuilder::Calculate<DomTreeBuilder::BBPostDomTree>(
    DomTreeBuilder::BBPostDomTree &DT);
// No CalculateWithUpdates<PostDomTree> instantiation, unless a usecase arises.

template void llvm::DomTreeBuilder::InsertEdge<DomTreeBuilder::BBDomTree>(
    DomTreeBuilder::BBDomTree &DT, BasicBlock *From, BasicBlock *To);
template void llvm::DomTreeBuilder::InsertEdge<DomTreeBuilder::BBPostDomTree>(
    DomTreeBuilder::BBPostDomTree &DT, BasicBlock *From, BasicBlock *To);

template void llvm::DomTreeBuilder::DeleteEdge<DomTreeBuilder::BBDomTree>(
    DomTreeBuilder::BBDomTree &DT, BasicBlock *From, BasicBlock *To);
template void llvm::DomTreeBuilder::DeleteEdge<DomTreeBuilder::BBPostDomTree>(
    DomTreeBuilder::BBPostDomTree &DT, BasicBlock *From, BasicBlock *To);

template void llvm::DomTreeBuilder::ApplyUpdates<DomTreeBuilder::BBDomTree>(
    DomTreeBuilder::BBDomTree &DT, DomTreeBuilder::BBDomTreeGraphDiff &,
    DomTreeBuilder::BBDomTreeGraphDiff *);
template void llvm::DomTreeBuilder::ApplyUpdates<DomTreeBuilder::BBPostDomTree>(
    DomTreeBuilder::BBPostDomTree &DT, DomTreeBuilder::BBPostDomTreeGraphDiff &,
    DomTreeBuilder::BBPostDomTreeGraphDiff *);

template bool llvm::DomTreeBuilder::Verify<DomTreeBuilder::BBDomTree>(
    const DomTreeBuilder::BBDomTree &DT,
    DomTreeBuilder::BBDomTree::VerificationLevel VL);
template bool llvm::DomTreeBuilder::Verify<DomTreeBuilder::BBPostDomTree>(
    const DomTreeBuilder::BBPostDomTree &DT,
    DomTreeBuilder::BBPostDomTree::VerificationLevel VL);

bool DominatorTree::invalidate(Function &F, const PreservedAnalyses &PA,
                               FunctionAnalysisManager::Invalidator &) {
  // Check whether the analysis, all analyses on functions, or the function's
  // CFG have been preserved.
  auto PAC = PA.getChecker<DominatorTreeAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>() ||
           PAC.preservedSet<CFGAnalyses>());
}

bool DominatorTree::dominates(const BasicBlock *BB, const Use &U) const {
  Instruction *UserInst = cast<Instruction>(U.getUser());
  if (auto *PN = dyn_cast<PHINode>(UserInst))
    // A phi use using a value from a block is dominated by the end of that
    // block.  Note that the phi's parent block may not be.
    return dominates(BB, PN->getIncomingBlock(U));
  else
    return properlyDominates(BB, UserInst->getParent());
}

// dominates - Return true if Def dominates a use in User. This performs
// the special checks necessary if Def and User are in the same basic block.
// Note that Def doesn't dominate a use in Def itself!
bool DominatorTree::dominates(const Value *DefV,
                              const Instruction *User) const {
  const Instruction *Def = dyn_cast<Instruction>(DefV);
  if (!Def) {
    assert((isa<Argument>(DefV) || isa<Constant>(DefV)) &&
           "Should be called with an instruction, argument or constant");
    return true; // Arguments and constants dominate everything.
  }

  const BasicBlock *UseBB = User->getParent();
  const BasicBlock *DefBB = Def->getParent();

  // Any unreachable use is dominated, even if Def == User.
  if (!isReachableFromEntry(UseBB))
    return true;

  // Unreachable definitions don't dominate anything.
  if (!isReachableFromEntry(DefBB))
    return false;

  // An instruction doesn't dominate a use in itself.
  if (Def == User)
    return false;

  // The value defined by an invoke dominates an instruction only if it
  // dominates every instruction in UseBB.
  // A PHI is dominated only if the instruction dominates every possible use in
  // the UseBB.
  if (isa<InvokeInst>(Def) || isa<CallBrInst>(Def) || isa<PHINode>(User))
    return dominates(Def, UseBB);

  if (DefBB != UseBB)
    return dominates(DefBB, UseBB);

  return Def->comesBefore(User);
}

// true if Def would dominate a use in any instruction in UseBB.
// note that dominates(Def, Def->getParent()) is false.
bool DominatorTree::dominates(const Instruction *Def,
                              const BasicBlock *UseBB) const {
  const BasicBlock *DefBB = Def->getParent();

  // Any unreachable use is dominated, even if DefBB == UseBB.
  if (!isReachableFromEntry(UseBB))
    return true;

  // Unreachable definitions don't dominate anything.
  if (!isReachableFromEntry(DefBB))
    return false;

  if (DefBB == UseBB)
    return false;

  // Invoke results are only usable in the normal destination, not in the
  // exceptional destination.
  if (const auto *II = dyn_cast<InvokeInst>(Def)) {
    BasicBlock *NormalDest = II->getNormalDest();
    BasicBlockEdge E(DefBB, NormalDest);
    return dominates(E, UseBB);
  }

  return dominates(DefBB, UseBB);
}

bool DominatorTree::dominates(const BasicBlockEdge &BBE,
                              const BasicBlock *UseBB) const {
  // If the BB the edge ends in doesn't dominate the use BB, then the
  // edge also doesn't.
  const BasicBlock *Start = BBE.getStart();
  const BasicBlock *End = BBE.getEnd();
  if (!dominates(End, UseBB))
    return false;

  // Simple case: if the end BB has a single predecessor, the fact that it
  // dominates the use block implies that the edge also does.
  if (End->getSinglePredecessor())
    return true;

  // The normal edge from the invoke is critical. Conceptually, what we would
  // like to do is split it and check if the new block dominates the use.
  // With X being the new block, the graph would look like:
  //
  //        DefBB
  //          /\      .  .
  //         /  \     .  .
  //        /    \    .  .
  //       /      \   |  |
  //      A        X  B  C
  //      |         \ | /
  //      .          \|/
  //      .      NormalDest
  //      .
  //
  // Given the definition of dominance, NormalDest is dominated by X iff X
  // dominates all of NormalDest's predecessors (X, B, C in the example). X
  // trivially dominates itself, so we only have to find if it dominates the
  // other predecessors. Since the only way out of X is via NormalDest, X can
  // only properly dominate a node if NormalDest dominates that node too.
  int IsDuplicateEdge = 0;
  for (const BasicBlock *BB : predecessors(End)) {
    if (BB == Start) {
      // If there are multiple edges between Start and End, by definition they
      // can't dominate anything.
      if (IsDuplicateEdge++)
        return false;
      continue;
    }

    if (!dominates(End, BB))
      return false;
  }
  return true;
}

bool DominatorTree::dominates(const BasicBlockEdge &BBE, const Use &U) const {
  Instruction *UserInst = cast<Instruction>(U.getUser());
  // A PHI in the end of the edge is dominated by it.
  PHINode *PN = dyn_cast<PHINode>(UserInst);
  if (PN && PN->getParent() == BBE.getEnd() &&
      PN->getIncomingBlock(U) == BBE.getStart())
    return true;

  // Otherwise use the edge-dominates-block query, which
  // handles the crazy critical edge cases properly.
  const BasicBlock *UseBB;
  if (PN)
    UseBB = PN->getIncomingBlock(U);
  else
    UseBB = UserInst->getParent();
  return dominates(BBE, UseBB);
}

bool DominatorTree::dominates(const Value *DefV, const Use &U) const {
  const Instruction *Def = dyn_cast<Instruction>(DefV);
  if (!Def) {
    assert((isa<Argument>(DefV) || isa<Constant>(DefV)) &&
           "Should be called with an instruction, argument or constant");
    return true; // Arguments and constants dominate everything.
  }

  Instruction *UserInst = cast<Instruction>(U.getUser());
  const BasicBlock *DefBB = Def->getParent();

  // Determine the block in which the use happens. PHI nodes use
  // their operands on edges; simulate this by thinking of the use
  // happening at the end of the predecessor block.
  const BasicBlock *UseBB;
  if (PHINode *PN = dyn_cast<PHINode>(UserInst))
    UseBB = PN->getIncomingBlock(U);
  else
    UseBB = UserInst->getParent();

  // Any unreachable use is dominated, even if Def == User.
  if (!isReachableFromEntry(UseBB))
    return true;

  // Unreachable definitions don't dominate anything.
  if (!isReachableFromEntry(DefBB))
    return false;

  // Invoke instructions define their return values on the edges to their normal
  // successors, so we have to handle them specially.
  // Among other things, this means they don't dominate anything in
  // their own block, except possibly a phi, so we don't need to
  // walk the block in any case.
  if (const InvokeInst *II = dyn_cast<InvokeInst>(Def)) {
    BasicBlock *NormalDest = II->getNormalDest();
    BasicBlockEdge E(DefBB, NormalDest);
    return dominates(E, U);
  }

  // If the def and use are in different blocks, do a simple CFG dominator
  // tree query.
  if (DefBB != UseBB)
    return dominates(DefBB, UseBB);

  // Ok, def and use are in the same block. If the def is an invoke, it
  // doesn't dominate anything in the block. If it's a PHI, it dominates
  // everything in the block.
  if (isa<PHINode>(UserInst))
    return true;

  return Def->comesBefore(UserInst);
}

bool DominatorTree::isReachableFromEntry(const Use &U) const {
  Instruction *I = dyn_cast<Instruction>(U.getUser());

  // ConstantExprs aren't really reachable from the entry block, but they
  // don't need to be treated like unreachable code either.
  if (!I) return true;

  // PHI nodes use their operands on their incoming edges.
  if (PHINode *PN = dyn_cast<PHINode>(I))
    return isReachableFromEntry(PN->getIncomingBlock(U));

  // Everything else uses their operands in their own block.
  return isReachableFromEntry(I->getParent());
}

// Edge BBE1 dominates edge BBE2 if they match or BBE1 dominates start of BBE2.
bool DominatorTree::dominates(const BasicBlockEdge &BBE1,
                              const BasicBlockEdge &BBE2) const {
  if (BBE1.getStart() == BBE2.getStart() && BBE1.getEnd() == BBE2.getEnd())
    return true;
  return dominates(BBE1, BBE2.getStart());
}

Instruction *DominatorTree::findNearestCommonDominator(Instruction *I1,
                                                       Instruction *I2) const {
  BasicBlock *BB1 = I1->getParent();
  BasicBlock *BB2 = I2->getParent();
  if (BB1 == BB2)
    return I1->comesBefore(I2) ? I1 : I2;
  if (!isReachableFromEntry(BB2))
    return I1;
  if (!isReachableFromEntry(BB1))
    return I2;
  BasicBlock *DomBB = findNearestCommonDominator(BB1, BB2);
  if (BB1 == DomBB)
    return I1;
  if (BB2 == DomBB)
    return I2;
  return DomBB->getTerminator();
}

//===----------------------------------------------------------------------===//
//  DominatorTreeAnalysis and related pass implementations
//===----------------------------------------------------------------------===//
//
// This implements the DominatorTreeAnalysis which is used with the new pass
// manager. It also implements some methods from utility passes.
//
//===----------------------------------------------------------------------===//

DominatorTree DominatorTreeAnalysis::run(Function &F,
                                         FunctionAnalysisManager &) {
  DominatorTree DT;
  DT.recalculate(F);
  return DT;
}

AnalysisKey DominatorTreeAnalysis::Key;

DominatorTreePrinterPass::DominatorTreePrinterPass(raw_ostream &OS) : OS(OS) {}

PreservedAnalyses DominatorTreePrinterPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  OS << "DominatorTree for function: " << F.getName() << "\n";
  AM.getResult<DominatorTreeAnalysis>(F).print(OS);

  return PreservedAnalyses::all();
}

PreservedAnalyses DominatorTreeVerifierPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  assert(DT.verify());
  (void)DT;
  return PreservedAnalyses::all();
}

//===----------------------------------------------------------------------===//
//  DominatorTreeWrapperPass Implementation
//===----------------------------------------------------------------------===//
//
// The implementation details of the wrapper pass that holds a DominatorTree
// suitable for use with the legacy pass manager.
//
//===----------------------------------------------------------------------===//

char DominatorTreeWrapperPass::ID = 0;

DominatorTreeWrapperPass::DominatorTreeWrapperPass() : FunctionPass(ID) {
  initializeDominatorTreeWrapperPassPass(*PassRegistry::getPassRegistry());
}

INITIALIZE_PASS(DominatorTreeWrapperPass, "domtree",
                "Dominator Tree Construction", true, true)

bool DominatorTreeWrapperPass::runOnFunction(Function &F) {
  DT.recalculate(F);
  return false;
}

void DominatorTreeWrapperPass::verifyAnalysis() const {
  if (VerifyDomInfo)
    assert(DT.verify(DominatorTree::VerificationLevel::Full));
  else if (ExpensiveChecksEnabled)
    assert(DT.verify(DominatorTree::VerificationLevel::Basic));
}

void DominatorTreeWrapperPass::print(raw_ostream &OS, const Module *) const {
  DT.print(OS);
}
