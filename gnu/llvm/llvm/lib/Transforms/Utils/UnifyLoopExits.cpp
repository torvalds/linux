//===- UnifyLoopExits.cpp - Redirect exiting edges to one block -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each natural loop with multiple exit blocks, this pass creates a new
// block N such that all exiting blocks now branch to N, and then control flow
// is redistributed to all the original exit blocks.
//
// Limitation: This assumes that all terminators in the CFG are direct branches
//             (the "br" instruction). The presence of any other control flow
//             such as indirectbr, switch or callbr will cause an assert.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/UnifyLoopExits.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#define DEBUG_TYPE "unify-loop-exits"

using namespace llvm;

static cl::opt<unsigned> MaxBooleansInControlFlowHub(
    "max-booleans-in-control-flow-hub", cl::init(32), cl::Hidden,
    cl::desc("Set the maximum number of outgoing blocks for using a boolean "
             "value to record the exiting block in CreateControlFlowHub."));

namespace {
struct UnifyLoopExitsLegacyPass : public FunctionPass {
  static char ID;
  UnifyLoopExitsLegacyPass() : FunctionPass(ID) {
    initializeUnifyLoopExitsLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<LoopInfoWrapperPass>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }

  bool runOnFunction(Function &F) override;
};
} // namespace

char UnifyLoopExitsLegacyPass::ID = 0;

FunctionPass *llvm::createUnifyLoopExitsPass() {
  return new UnifyLoopExitsLegacyPass();
}

INITIALIZE_PASS_BEGIN(UnifyLoopExitsLegacyPass, "unify-loop-exits",
                      "Fixup each natural loop to have a single exit block",
                      false /* Only looks at CFG */, false /* Analysis Pass */)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_END(UnifyLoopExitsLegacyPass, "unify-loop-exits",
                    "Fixup each natural loop to have a single exit block",
                    false /* Only looks at CFG */, false /* Analysis Pass */)

// The current transform introduces new control flow paths which may break the
// SSA requirement that every def must dominate all its uses. For example,
// consider a value D defined inside the loop that is used by some instruction
// U outside the loop. It follows that D dominates U, since the original
// program has valid SSA form. After merging the exits, all paths from D to U
// now flow through the unified exit block. In addition, there may be other
// paths that do not pass through D, but now reach the unified exit
// block. Thus, D no longer dominates U.
//
// Restore the dominance by creating a phi for each such D at the new unified
// loop exit. But when doing this, ignore any uses U that are in the new unified
// loop exit, since those were introduced specially when the block was created.
//
// The use of SSAUpdater seems like overkill for this operation. The location
// for creating the new PHI is well-known, and also the set of incoming blocks
// to the new PHI.
static void restoreSSA(const DominatorTree &DT, const Loop *L,
                       const SetVector<BasicBlock *> &Incoming,
                       BasicBlock *LoopExitBlock) {
  using InstVector = SmallVector<Instruction *, 8>;
  using IIMap = MapVector<Instruction *, InstVector>;
  IIMap ExternalUsers;
  for (auto *BB : L->blocks()) {
    for (auto &I : *BB) {
      for (auto &U : I.uses()) {
        auto UserInst = cast<Instruction>(U.getUser());
        auto UserBlock = UserInst->getParent();
        if (UserBlock == LoopExitBlock)
          continue;
        if (L->contains(UserBlock))
          continue;
        LLVM_DEBUG(dbgs() << "added ext use for " << I.getName() << "("
                          << BB->getName() << ")"
                          << ": " << UserInst->getName() << "("
                          << UserBlock->getName() << ")"
                          << "\n");
        ExternalUsers[&I].push_back(UserInst);
      }
    }
  }

  for (const auto &II : ExternalUsers) {
    // For each Def used outside the loop, create NewPhi in
    // LoopExitBlock. NewPhi receives Def only along exiting blocks that
    // dominate it, while the remaining values are undefined since those paths
    // didn't exist in the original CFG.
    auto Def = II.first;
    LLVM_DEBUG(dbgs() << "externally used: " << Def->getName() << "\n");
    auto NewPhi =
        PHINode::Create(Def->getType(), Incoming.size(),
                        Def->getName() + ".moved", LoopExitBlock->begin());
    for (auto *In : Incoming) {
      LLVM_DEBUG(dbgs() << "predecessor " << In->getName() << ": ");
      if (Def->getParent() == In || DT.dominates(Def, In)) {
        LLVM_DEBUG(dbgs() << "dominated\n");
        NewPhi->addIncoming(Def, In);
      } else {
        LLVM_DEBUG(dbgs() << "not dominated\n");
        NewPhi->addIncoming(PoisonValue::get(Def->getType()), In);
      }
    }

    LLVM_DEBUG(dbgs() << "external users:");
    for (auto *U : II.second) {
      LLVM_DEBUG(dbgs() << " " << U->getName());
      U->replaceUsesOfWith(Def, NewPhi);
    }
    LLVM_DEBUG(dbgs() << "\n");
  }
}

static bool unifyLoopExits(DominatorTree &DT, LoopInfo &LI, Loop *L) {
  // To unify the loop exits, we need a list of the exiting blocks as
  // well as exit blocks. The functions for locating these lists both
  // traverse the entire loop body. It is more efficient to first
  // locate the exiting blocks and then examine their successors to
  // locate the exit blocks.
  SetVector<BasicBlock *> ExitingBlocks;
  SetVector<BasicBlock *> Exits;

  // We need SetVectors, but the Loop API takes a vector, so we use a temporary.
  SmallVector<BasicBlock *, 8> Temp;
  L->getExitingBlocks(Temp);
  for (auto *BB : Temp) {
    ExitingBlocks.insert(BB);
    for (auto *S : successors(BB)) {
      auto SL = LI.getLoopFor(S);
      // A successor is not an exit if it is directly or indirectly in the
      // current loop.
      if (SL == L || L->contains(SL))
        continue;
      Exits.insert(S);
    }
  }

  LLVM_DEBUG(
      dbgs() << "Found exit blocks:";
      for (auto Exit : Exits) {
        dbgs() << " " << Exit->getName();
      }
      dbgs() << "\n";

      dbgs() << "Found exiting blocks:";
      for (auto EB : ExitingBlocks) {
        dbgs() << " " << EB->getName();
      }
      dbgs() << "\n";);

  if (Exits.size() <= 1) {
    LLVM_DEBUG(dbgs() << "loop does not have multiple exits; nothing to do\n");
    return false;
  }

  SmallVector<BasicBlock *, 8> GuardBlocks;
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  auto LoopExitBlock =
      CreateControlFlowHub(&DTU, GuardBlocks, ExitingBlocks, Exits, "loop.exit",
                           MaxBooleansInControlFlowHub.getValue());

  restoreSSA(DT, L, ExitingBlocks, LoopExitBlock);

#if defined(EXPENSIVE_CHECKS)
  assert(DT.verify(DominatorTree::VerificationLevel::Full));
#else
  assert(DT.verify(DominatorTree::VerificationLevel::Fast));
#endif // EXPENSIVE_CHECKS
  L->verifyLoop();

  // The guard blocks were created outside the loop, so they need to become
  // members of the parent loop.
  if (auto ParentLoop = L->getParentLoop()) {
    for (auto *G : GuardBlocks) {
      ParentLoop->addBasicBlockToLoop(G, LI);
    }
    ParentLoop->verifyLoop();
  }

#if defined(EXPENSIVE_CHECKS)
  LI.verify(DT);
#endif // EXPENSIVE_CHECKS

  return true;
}

static bool runImpl(LoopInfo &LI, DominatorTree &DT) {

  bool Changed = false;
  auto Loops = LI.getLoopsInPreorder();
  for (auto *L : Loops) {
    LLVM_DEBUG(dbgs() << "Loop: " << L->getHeader()->getName() << " (depth: "
                      << LI.getLoopDepth(L->getHeader()) << ")\n");
    Changed |= unifyLoopExits(DT, LI, L);
  }
  return Changed;
}

bool UnifyLoopExitsLegacyPass::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "===== Unifying loop exits in function " << F.getName()
                    << "\n");
  auto &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  assert(hasOnlySimpleTerminator(F) && "Unsupported block terminator.");

  return runImpl(LI, DT);
}

namespace llvm {

PreservedAnalyses UnifyLoopExitsPass::run(Function &F,
                                          FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  if (!runImpl(LI, DT))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<LoopAnalysis>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
} // namespace llvm
