//===- AMDGPUUnifyDivergentExitNodes.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a variant of the UnifyFunctionExitNodes pass. Rather than ensuring
// there is at most one ret and one unreachable instruction, it ensures there is
// at most one divergent exiting block.
//
// StructurizeCFG can't deal with multi-exit regions formed by branches to
// multiple return nodes. It is not desirable to structurize regions with
// uniform branches, so unifying those to the same return block as divergent
// branches inhibits use of scalar branching. It still can't deal with the case
// where one branch goes to return, and one unreachable. Replace unreachable in
// this case with a return.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUUnifyDivergentExitNodes.h"
#include "AMDGPU.h"
#include "SIDefines.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/Type.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-unify-divergent-exit-nodes"

namespace {

class AMDGPUUnifyDivergentExitNodesImpl {
private:
  const TargetTransformInfo *TTI = nullptr;

public:
  AMDGPUUnifyDivergentExitNodesImpl() = delete;
  AMDGPUUnifyDivergentExitNodesImpl(const TargetTransformInfo *TTI)
      : TTI(TTI) {}

  // We can preserve non-critical-edgeness when we unify function exit nodes
  BasicBlock *unifyReturnBlockSet(Function &F, DomTreeUpdater &DTU,
                                  ArrayRef<BasicBlock *> ReturningBlocks,
                                  StringRef Name);
  bool run(Function &F, DominatorTree *DT, const PostDominatorTree &PDT,
           const UniformityInfo &UA);
};

class AMDGPUUnifyDivergentExitNodes : public FunctionPass {
public:
  static char ID;
  AMDGPUUnifyDivergentExitNodes() : FunctionPass(ID) {
    initializeAMDGPUUnifyDivergentExitNodesPass(
        *PassRegistry::getPassRegistry());
  }
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  bool runOnFunction(Function &F) override;
};
} // end anonymous namespace

char AMDGPUUnifyDivergentExitNodes::ID = 0;

char &llvm::AMDGPUUnifyDivergentExitNodesID = AMDGPUUnifyDivergentExitNodes::ID;

INITIALIZE_PASS_BEGIN(AMDGPUUnifyDivergentExitNodes, DEBUG_TYPE,
                      "Unify divergent function exit nodes", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(PostDominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(UniformityInfoWrapperPass)
INITIALIZE_PASS_END(AMDGPUUnifyDivergentExitNodes, DEBUG_TYPE,
                    "Unify divergent function exit nodes", false, false)

void AMDGPUUnifyDivergentExitNodes::getAnalysisUsage(AnalysisUsage &AU) const {
  if (RequireAndPreserveDomTree)
    AU.addRequired<DominatorTreeWrapperPass>();

  AU.addRequired<PostDominatorTreeWrapperPass>();

  AU.addRequired<UniformityInfoWrapperPass>();

  if (RequireAndPreserveDomTree) {
    AU.addPreserved<DominatorTreeWrapperPass>();
    // FIXME: preserve PostDominatorTreeWrapperPass
  }

  // We preserve the non-critical-edgeness property
  AU.addPreservedID(BreakCriticalEdgesID);

  FunctionPass::getAnalysisUsage(AU);

  AU.addRequired<TargetTransformInfoWrapperPass>();
}

/// \returns true if \p BB is reachable through only uniform branches.
/// XXX - Is there a more efficient way to find this?
static bool isUniformlyReached(const UniformityInfo &UA, BasicBlock &BB) {
  SmallVector<BasicBlock *, 8> Stack(predecessors(&BB));
  SmallPtrSet<BasicBlock *, 8> Visited;

  while (!Stack.empty()) {
    BasicBlock *Top = Stack.pop_back_val();
    if (!UA.isUniform(Top->getTerminator()))
      return false;

    for (BasicBlock *Pred : predecessors(Top)) {
      if (Visited.insert(Pred).second)
        Stack.push_back(Pred);
    }
  }

  return true;
}

BasicBlock *AMDGPUUnifyDivergentExitNodesImpl::unifyReturnBlockSet(
    Function &F, DomTreeUpdater &DTU, ArrayRef<BasicBlock *> ReturningBlocks,
    StringRef Name) {
  // Otherwise, we need to insert a new basic block into the function, add a PHI
  // nodes (if the function returns values), and convert all of the return
  // instructions into unconditional branches.
  BasicBlock *NewRetBlock = BasicBlock::Create(F.getContext(), Name, &F);
  IRBuilder<> B(NewRetBlock);

  PHINode *PN = nullptr;
  if (F.getReturnType()->isVoidTy()) {
    B.CreateRetVoid();
  } else {
    // If the function doesn't return void... add a PHI node to the block...
    PN = B.CreatePHI(F.getReturnType(), ReturningBlocks.size(),
                     "UnifiedRetVal");
    B.CreateRet(PN);
  }

  // Loop over all of the blocks, replacing the return instruction with an
  // unconditional branch.
  std::vector<DominatorTree::UpdateType> Updates;
  Updates.reserve(ReturningBlocks.size());
  for (BasicBlock *BB : ReturningBlocks) {
    // Add an incoming element to the PHI node for every return instruction that
    // is merging into this new block...
    if (PN)
      PN->addIncoming(BB->getTerminator()->getOperand(0), BB);

    // Remove and delete the return inst.
    BB->getTerminator()->eraseFromParent();
    BranchInst::Create(NewRetBlock, BB);
    Updates.emplace_back(DominatorTree::Insert, BB, NewRetBlock);
  }

  if (RequireAndPreserveDomTree)
    DTU.applyUpdates(Updates);
  Updates.clear();

  for (BasicBlock *BB : ReturningBlocks) {
    // Cleanup possible branch to unconditional branch to the return.
    simplifyCFG(BB, *TTI, RequireAndPreserveDomTree ? &DTU : nullptr,
                SimplifyCFGOptions().bonusInstThreshold(2));
  }

  return NewRetBlock;
}

bool AMDGPUUnifyDivergentExitNodesImpl::run(Function &F, DominatorTree *DT,
                                            const PostDominatorTree &PDT,
                                            const UniformityInfo &UA) {
  assert(hasOnlySimpleTerminator(F) && "Unsupported block terminator.");

  if (PDT.root_size() == 0 ||
      (PDT.root_size() == 1 &&
       !isa<BranchInst>(PDT.getRoot()->getTerminator())))
    return false;

  // Loop over all of the blocks in a function, tracking all of the blocks that
  // return.
  SmallVector<BasicBlock *, 4> ReturningBlocks;
  SmallVector<BasicBlock *, 4> UnreachableBlocks;

  // Dummy return block for infinite loop.
  BasicBlock *DummyReturnBB = nullptr;

  bool Changed = false;
  std::vector<DominatorTree::UpdateType> Updates;

  // TODO: For now we unify all exit blocks, even though they are uniformly
  // reachable, if there are any exits not uniformly reached. This is to
  // workaround the limitation of structurizer, which can not handle multiple
  // function exits. After structurizer is able to handle multiple function
  // exits, we should only unify UnreachableBlocks that are not uniformly
  // reachable.
  bool HasDivergentExitBlock = llvm::any_of(
      PDT.roots(), [&](auto BB) { return !isUniformlyReached(UA, *BB); });

  for (BasicBlock *BB : PDT.roots()) {
    if (isa<ReturnInst>(BB->getTerminator())) {
      if (HasDivergentExitBlock)
        ReturningBlocks.push_back(BB);
    } else if (isa<UnreachableInst>(BB->getTerminator())) {
      if (HasDivergentExitBlock)
        UnreachableBlocks.push_back(BB);
    } else if (BranchInst *BI = dyn_cast<BranchInst>(BB->getTerminator())) {

      ConstantInt *BoolTrue = ConstantInt::getTrue(F.getContext());
      if (DummyReturnBB == nullptr) {
        DummyReturnBB = BasicBlock::Create(F.getContext(),
                                           "DummyReturnBlock", &F);
        Type *RetTy = F.getReturnType();
        Value *RetVal = RetTy->isVoidTy() ? nullptr : PoisonValue::get(RetTy);
        ReturnInst::Create(F.getContext(), RetVal, DummyReturnBB);
        ReturningBlocks.push_back(DummyReturnBB);
      }

      if (BI->isUnconditional()) {
        BasicBlock *LoopHeaderBB = BI->getSuccessor(0);
        BI->eraseFromParent(); // Delete the unconditional branch.
        // Add a new conditional branch with a dummy edge to the return block.
        BranchInst::Create(LoopHeaderBB, DummyReturnBB, BoolTrue, BB);
        Updates.emplace_back(DominatorTree::Insert, BB, DummyReturnBB);
      } else { // Conditional branch.
        SmallVector<BasicBlock *, 2> Successors(successors(BB));

        // Create a new transition block to hold the conditional branch.
        BasicBlock *TransitionBB = BB->splitBasicBlock(BI, "TransitionBlock");

        Updates.reserve(Updates.size() + 2 * Successors.size() + 2);

        // 'Successors' become successors of TransitionBB instead of BB,
        // and TransitionBB becomes a single successor of BB.
        Updates.emplace_back(DominatorTree::Insert, BB, TransitionBB);
        for (BasicBlock *Successor : Successors) {
          Updates.emplace_back(DominatorTree::Insert, TransitionBB, Successor);
          Updates.emplace_back(DominatorTree::Delete, BB, Successor);
        }

        // Create a branch that will always branch to the transition block and
        // references DummyReturnBB.
        BB->getTerminator()->eraseFromParent();
        BranchInst::Create(TransitionBB, DummyReturnBB, BoolTrue, BB);
        Updates.emplace_back(DominatorTree::Insert, BB, DummyReturnBB);
      }
      Changed = true;
    }
  }

  if (!UnreachableBlocks.empty()) {
    BasicBlock *UnreachableBlock = nullptr;

    if (UnreachableBlocks.size() == 1) {
      UnreachableBlock = UnreachableBlocks.front();
    } else {
      UnreachableBlock = BasicBlock::Create(F.getContext(),
                                            "UnifiedUnreachableBlock", &F);
      new UnreachableInst(F.getContext(), UnreachableBlock);

      Updates.reserve(Updates.size() + UnreachableBlocks.size());
      for (BasicBlock *BB : UnreachableBlocks) {
        // Remove and delete the unreachable inst.
        BB->getTerminator()->eraseFromParent();
        BranchInst::Create(UnreachableBlock, BB);
        Updates.emplace_back(DominatorTree::Insert, BB, UnreachableBlock);
      }
      Changed = true;
    }

    if (!ReturningBlocks.empty()) {
      // Don't create a new unreachable inst if we have a return. The
      // structurizer/annotator can't handle the multiple exits

      Type *RetTy = F.getReturnType();
      Value *RetVal = RetTy->isVoidTy() ? nullptr : PoisonValue::get(RetTy);
      // Remove and delete the unreachable inst.
      UnreachableBlock->getTerminator()->eraseFromParent();

      Function *UnreachableIntrin =
        Intrinsic::getDeclaration(F.getParent(), Intrinsic::amdgcn_unreachable);

      // Insert a call to an intrinsic tracking that this is an unreachable
      // point, in case we want to kill the active lanes or something later.
      CallInst::Create(UnreachableIntrin, {}, "", UnreachableBlock);

      // Don't create a scalar trap. We would only want to trap if this code was
      // really reached, but a scalar trap would happen even if no lanes
      // actually reached here.
      ReturnInst::Create(F.getContext(), RetVal, UnreachableBlock);
      ReturningBlocks.push_back(UnreachableBlock);
      Changed = true;
    }
  }

  // FIXME: add PDT here once simplifycfg is ready.
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Eager);
  if (RequireAndPreserveDomTree)
    DTU.applyUpdates(Updates);
  Updates.clear();

  // Now handle return blocks.
  if (ReturningBlocks.empty())
    return Changed; // No blocks return

  if (ReturningBlocks.size() == 1)
    return Changed; // Already has a single return block

  unifyReturnBlockSet(F, DTU, ReturningBlocks, "UnifiedReturnBlock");
  return true;
}

bool AMDGPUUnifyDivergentExitNodes::runOnFunction(Function &F) {
  DominatorTree *DT = nullptr;
  if (RequireAndPreserveDomTree)
    DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  const auto &PDT =
      getAnalysis<PostDominatorTreeWrapperPass>().getPostDomTree();
  const auto &UA = getAnalysis<UniformityInfoWrapperPass>().getUniformityInfo();
  const auto *TranformInfo =
      &getAnalysis<TargetTransformInfoWrapperPass>().getTTI(F);
  return AMDGPUUnifyDivergentExitNodesImpl(TranformInfo).run(F, DT, PDT, UA);
}

PreservedAnalyses
AMDGPUUnifyDivergentExitNodesPass::run(Function &F,
                                       FunctionAnalysisManager &AM) {
  DominatorTree *DT = nullptr;
  if (RequireAndPreserveDomTree)
    DT = &AM.getResult<DominatorTreeAnalysis>(F);

  const auto &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  const auto &UA = AM.getResult<UniformityInfoAnalysis>(F);
  const auto *TransformInfo = &AM.getResult<TargetIRAnalysis>(F);
  return AMDGPUUnifyDivergentExitNodesImpl(TransformInfo).run(F, DT, PDT, UA)
             ? PreservedAnalyses::none()
             : PreservedAnalyses::all();
}
