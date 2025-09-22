//===-- CallBrPrepare - Prepare callbr for code generation ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers callbrs in LLVM IR in order to to assist SelectionDAG's
// codegen.
//
// In particular, this pass assists in inserting register copies for the output
// values of a callbr along the edges leading to the indirect target blocks.
// Though the output SSA value is defined by the callbr instruction itself in
// the IR representation, the value cannot be copied to the appropriate virtual
// registers prior to jumping to an indirect label, since the jump occurs
// within the user-provided assembly blob.
//
// Instead, those copies must occur separately at the beginning of each
// indirect target. That requires that we create a separate SSA definition in
// each of them (via llvm.callbr.landingpad), and may require splitting
// critical edges so we have a location to place the intrinsic. Finally, we
// remap users of the original callbr output SSA value to instead point to the
// appropriate llvm.callbr.landingpad value.
//
// Ideally, this could be done inside SelectionDAG, or in the
// MachineInstruction representation, without the use of an IR-level intrinsic.
// But, within the current framework, it’s simpler to implement as an IR pass.
// (If support for callbr in GlobalISel is implemented, it’s worth considering
// whether this is still required.)
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CallBrPrepare.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"

using namespace llvm;

#define DEBUG_TYPE "callbr-prepare"

static bool SplitCriticalEdges(ArrayRef<CallBrInst *> CBRs, DominatorTree &DT);
static bool InsertIntrinsicCalls(ArrayRef<CallBrInst *> CBRs,
                                 DominatorTree &DT);
static void UpdateSSA(DominatorTree &DT, CallBrInst *CBR, CallInst *Intrinsic,
                      SSAUpdater &SSAUpdate);
static SmallVector<CallBrInst *, 2> FindCallBrs(Function &Fn);

namespace {

class CallBrPrepare : public FunctionPass {
public:
  CallBrPrepare() : FunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &Fn) override;
  static char ID;
};

} // end anonymous namespace

PreservedAnalyses CallBrPreparePass::run(Function &Fn,
                                         FunctionAnalysisManager &FAM) {
  bool Changed = false;
  SmallVector<CallBrInst *, 2> CBRs = FindCallBrs(Fn);

  if (CBRs.empty())
    return PreservedAnalyses::all();

  auto &DT = FAM.getResult<DominatorTreeAnalysis>(Fn);

  Changed |= SplitCriticalEdges(CBRs, DT);
  Changed |= InsertIntrinsicCalls(CBRs, DT);

  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

char CallBrPrepare::ID = 0;
INITIALIZE_PASS_BEGIN(CallBrPrepare, "callbrprepare", "Prepare callbr", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(CallBrPrepare, "callbrprepare", "Prepare callbr", false,
                    false)

FunctionPass *llvm::createCallBrPass() { return new CallBrPrepare(); }

void CallBrPrepare::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addPreserved<DominatorTreeWrapperPass>();
}

SmallVector<CallBrInst *, 2> FindCallBrs(Function &Fn) {
  SmallVector<CallBrInst *, 2> CBRs;
  for (BasicBlock &BB : Fn)
    if (auto *CBR = dyn_cast<CallBrInst>(BB.getTerminator()))
      if (!CBR->getType()->isVoidTy() && !CBR->use_empty())
        CBRs.push_back(CBR);
  return CBRs;
}

bool SplitCriticalEdges(ArrayRef<CallBrInst *> CBRs, DominatorTree &DT) {
  bool Changed = false;
  CriticalEdgeSplittingOptions Options(&DT);
  Options.setMergeIdenticalEdges();

  // The indirect destination might be duplicated between another parameter...
  //   %0 = callbr ... [label %x, label %x]
  // ...hence MergeIdenticalEdges and AllowIndentical edges, but we don't need
  // to split the default destination if it's duplicated between an indirect
  // destination...
  //   %1 = callbr ... to label %x [label %x]
  // ...hence starting at 1 and checking against successor 0 (aka the default
  // destination).
  for (CallBrInst *CBR : CBRs)
    for (unsigned i = 1, e = CBR->getNumSuccessors(); i != e; ++i)
      if (CBR->getSuccessor(i) == CBR->getSuccessor(0) ||
          isCriticalEdge(CBR, i, /*AllowIdenticalEdges*/ true))
        if (SplitKnownCriticalEdge(CBR, i, Options))
          Changed = true;
  return Changed;
}

bool InsertIntrinsicCalls(ArrayRef<CallBrInst *> CBRs, DominatorTree &DT) {
  bool Changed = false;
  SmallPtrSet<const BasicBlock *, 4> Visited;
  IRBuilder<> Builder(CBRs[0]->getContext());
  for (CallBrInst *CBR : CBRs) {
    if (!CBR->getNumIndirectDests())
      continue;

    SSAUpdater SSAUpdate;
    SSAUpdate.Initialize(CBR->getType(), CBR->getName());
    SSAUpdate.AddAvailableValue(CBR->getParent(), CBR);
    SSAUpdate.AddAvailableValue(CBR->getDefaultDest(), CBR);

    for (BasicBlock *IndDest : CBR->getIndirectDests()) {
      if (!Visited.insert(IndDest).second)
        continue;
      Builder.SetInsertPoint(&*IndDest->begin());
      CallInst *Intrinsic = Builder.CreateIntrinsic(
          CBR->getType(), Intrinsic::callbr_landingpad, {CBR});
      SSAUpdate.AddAvailableValue(IndDest, Intrinsic);
      UpdateSSA(DT, CBR, Intrinsic, SSAUpdate);
      Changed = true;
    }
  }
  return Changed;
}

static bool IsInSameBasicBlock(const Use &U, const BasicBlock *BB) {
  const auto *I = dyn_cast<Instruction>(U.getUser());
  return I && I->getParent() == BB;
}

#ifndef NDEBUG
static void PrintDebugDomInfo(const DominatorTree &DT, const Use &U,
                              const BasicBlock *BB, bool IsDefaultDest) {
  if (!isa<Instruction>(U.getUser()))
    return;
  LLVM_DEBUG(dbgs() << "Use: " << *U.getUser() << ", in block "
                    << cast<Instruction>(U.getUser())->getParent()->getName()
                    << ", is " << (DT.dominates(BB, U) ? "" : "NOT ")
                    << "dominated by " << BB->getName() << " ("
                    << (IsDefaultDest ? "in" : "") << "direct)\n");
}
#endif

void UpdateSSA(DominatorTree &DT, CallBrInst *CBR, CallInst *Intrinsic,
               SSAUpdater &SSAUpdate) {

  SmallPtrSet<Use *, 4> Visited;
  BasicBlock *DefaultDest = CBR->getDefaultDest();
  BasicBlock *LandingPad = Intrinsic->getParent();

  SmallVector<Use *, 4> Uses(make_pointer_range(CBR->uses()));
  for (Use *U : Uses) {
    if (!Visited.insert(U).second)
      continue;

#ifndef NDEBUG
    PrintDebugDomInfo(DT, *U, LandingPad, /*IsDefaultDest*/ false);
    PrintDebugDomInfo(DT, *U, DefaultDest, /*IsDefaultDest*/ true);
#endif

    // Don't rewrite the use in the newly inserted intrinsic.
    if (const auto *II = dyn_cast<IntrinsicInst>(U->getUser()))
      if (II->getIntrinsicID() == Intrinsic::callbr_landingpad)
        continue;

    // If the Use is in the same BasicBlock as the Intrinsic call, replace
    // the Use with the value of the Intrinsic call.
    if (IsInSameBasicBlock(*U, LandingPad)) {
      U->set(Intrinsic);
      continue;
    }

    // If the Use is dominated by the default dest, do not touch it.
    if (DT.dominates(DefaultDest, *U))
      continue;

    SSAUpdate.RewriteUse(*U);
  }
}

bool CallBrPrepare::runOnFunction(Function &Fn) {
  bool Changed = false;
  SmallVector<CallBrInst *, 2> CBRs = FindCallBrs(Fn);

  if (CBRs.empty())
    return Changed;

  // It's highly likely that most programs do not contain CallBrInsts. Follow a
  // similar pattern from SafeStackLegacyPass::runOnFunction to reuse previous
  // domtree analysis if available, otherwise compute it lazily. This avoids
  // forcing Dominator Tree Construction at -O0 for programs that likely do not
  // contain CallBrInsts. It does pessimize programs with callbr at higher
  // optimization levels, as the DominatorTree created here is not reused by
  // subsequent passes.
  DominatorTree *DT;
  std::optional<DominatorTree> LazilyComputedDomTree;
  if (auto *DTWP = getAnalysisIfAvailable<DominatorTreeWrapperPass>())
    DT = &DTWP->getDomTree();
  else {
    LazilyComputedDomTree.emplace(Fn);
    DT = &*LazilyComputedDomTree;
  }

  if (SplitCriticalEdges(CBRs, *DT))
    Changed = true;

  if (InsertIntrinsicCalls(CBRs, *DT))
    Changed = true;

  return Changed;
}
