//===-- UnrollLoop.cpp - Loop unrolling utilities -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements some loop unrolling utilities. It does not define any
// actual pass or policy, but provides a single function to perform loop
// unrolling.
//
// The process of unrolling can produce extraneous basic blocks linked with
// unconditional branches.  This will be corrected in the future.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/ilist_iterator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopIterator.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueMap.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GenericDomTree.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include "llvm/Transforms/Utils/SimplifyIndVar.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include <algorithm>
#include <assert.h>
#include <numeric>
#include <type_traits>
#include <vector>

namespace llvm {
class DataLayout;
class Value;
} // namespace llvm

using namespace llvm;

#define DEBUG_TYPE "loop-unroll"

// TODO: Should these be here or in LoopUnroll?
STATISTIC(NumCompletelyUnrolled, "Number of loops completely unrolled");
STATISTIC(NumUnrolled, "Number of loops unrolled (completely or otherwise)");
STATISTIC(NumUnrolledNotLatch, "Number of loops unrolled without a conditional "
                               "latch (completely or otherwise)");

static cl::opt<bool>
UnrollRuntimeEpilog("unroll-runtime-epilog", cl::init(false), cl::Hidden,
                    cl::desc("Allow runtime unrolled loops to be unrolled "
                             "with epilog instead of prolog."));

static cl::opt<bool>
UnrollVerifyDomtree("unroll-verify-domtree", cl::Hidden,
                    cl::desc("Verify domtree after unrolling"),
#ifdef EXPENSIVE_CHECKS
    cl::init(true)
#else
    cl::init(false)
#endif
                    );

static cl::opt<bool>
UnrollVerifyLoopInfo("unroll-verify-loopinfo", cl::Hidden,
                    cl::desc("Verify loopinfo after unrolling"),
#ifdef EXPENSIVE_CHECKS
    cl::init(true)
#else
    cl::init(false)
#endif
                    );


/// Check if unrolling created a situation where we need to insert phi nodes to
/// preserve LCSSA form.
/// \param Blocks is a vector of basic blocks representing unrolled loop.
/// \param L is the outer loop.
/// It's possible that some of the blocks are in L, and some are not. In this
/// case, if there is a use is outside L, and definition is inside L, we need to
/// insert a phi-node, otherwise LCSSA will be broken.
/// The function is just a helper function for llvm::UnrollLoop that returns
/// true if this situation occurs, indicating that LCSSA needs to be fixed.
static bool needToInsertPhisForLCSSA(Loop *L,
                                     const std::vector<BasicBlock *> &Blocks,
                                     LoopInfo *LI) {
  for (BasicBlock *BB : Blocks) {
    if (LI->getLoopFor(BB) == L)
      continue;
    for (Instruction &I : *BB) {
      for (Use &U : I.operands()) {
        if (const auto *Def = dyn_cast<Instruction>(U)) {
          Loop *DefLoop = LI->getLoopFor(Def->getParent());
          if (!DefLoop)
            continue;
          if (DefLoop->contains(L))
            return true;
        }
      }
    }
  }
  return false;
}

/// Adds ClonedBB to LoopInfo, creates a new loop for ClonedBB if necessary
/// and adds a mapping from the original loop to the new loop to NewLoops.
/// Returns nullptr if no new loop was created and a pointer to the
/// original loop OriginalBB was part of otherwise.
const Loop* llvm::addClonedBlockToLoopInfo(BasicBlock *OriginalBB,
                                           BasicBlock *ClonedBB, LoopInfo *LI,
                                           NewLoopsMap &NewLoops) {
  // Figure out which loop New is in.
  const Loop *OldLoop = LI->getLoopFor(OriginalBB);
  assert(OldLoop && "Should (at least) be in the loop being unrolled!");

  Loop *&NewLoop = NewLoops[OldLoop];
  if (!NewLoop) {
    // Found a new sub-loop.
    assert(OriginalBB == OldLoop->getHeader() &&
           "Header should be first in RPO");

    NewLoop = LI->AllocateLoop();
    Loop *NewLoopParent = NewLoops.lookup(OldLoop->getParentLoop());

    if (NewLoopParent)
      NewLoopParent->addChildLoop(NewLoop);
    else
      LI->addTopLevelLoop(NewLoop);

    NewLoop->addBasicBlockToLoop(ClonedBB, *LI);
    return OldLoop;
  } else {
    NewLoop->addBasicBlockToLoop(ClonedBB, *LI);
    return nullptr;
  }
}

/// The function chooses which type of unroll (epilog or prolog) is more
/// profitabale.
/// Epilog unroll is more profitable when there is PHI that starts from
/// constant.  In this case epilog will leave PHI start from constant,
/// but prolog will convert it to non-constant.
///
/// loop:
///   PN = PHI [I, Latch], [CI, PreHeader]
///   I = foo(PN)
///   ...
///
/// Epilog unroll case.
/// loop:
///   PN = PHI [I2, Latch], [CI, PreHeader]
///   I1 = foo(PN)
///   I2 = foo(I1)
///   ...
/// Prolog unroll case.
///   NewPN = PHI [PrologI, Prolog], [CI, PreHeader]
/// loop:
///   PN = PHI [I2, Latch], [NewPN, PreHeader]
///   I1 = foo(PN)
///   I2 = foo(I1)
///   ...
///
static bool isEpilogProfitable(Loop *L) {
  BasicBlock *PreHeader = L->getLoopPreheader();
  BasicBlock *Header = L->getHeader();
  assert(PreHeader && Header);
  for (const PHINode &PN : Header->phis()) {
    if (isa<ConstantInt>(PN.getIncomingValueForBlock(PreHeader)))
      return true;
  }
  return false;
}

struct LoadValue {
  Instruction *DefI = nullptr;
  unsigned Generation = 0;
  LoadValue() = default;
  LoadValue(Instruction *Inst, unsigned Generation)
      : DefI(Inst), Generation(Generation) {}
};

class StackNode {
  ScopedHashTable<const SCEV *, LoadValue>::ScopeTy LoadScope;
  unsigned CurrentGeneration;
  unsigned ChildGeneration;
  DomTreeNode *Node;
  DomTreeNode::const_iterator ChildIter;
  DomTreeNode::const_iterator EndIter;
  bool Processed = false;

public:
  StackNode(ScopedHashTable<const SCEV *, LoadValue> &AvailableLoads,
            unsigned cg, DomTreeNode *N, DomTreeNode::const_iterator Child,
            DomTreeNode::const_iterator End)
      : LoadScope(AvailableLoads), CurrentGeneration(cg), ChildGeneration(cg),
        Node(N), ChildIter(Child), EndIter(End) {}
  // Accessors.
  unsigned currentGeneration() const { return CurrentGeneration; }
  unsigned childGeneration() const { return ChildGeneration; }
  void childGeneration(unsigned generation) { ChildGeneration = generation; }
  DomTreeNode *node() { return Node; }
  DomTreeNode::const_iterator childIter() const { return ChildIter; }

  DomTreeNode *nextChild() {
    DomTreeNode *Child = *ChildIter;
    ++ChildIter;
    return Child;
  }

  DomTreeNode::const_iterator end() const { return EndIter; }
  bool isProcessed() const { return Processed; }
  void process() { Processed = true; }
};

Value *getMatchingValue(LoadValue LV, LoadInst *LI, unsigned CurrentGeneration,
                        BatchAAResults &BAA,
                        function_ref<MemorySSA *()> GetMSSA) {
  if (!LV.DefI)
    return nullptr;
  if (LV.DefI->getType() != LI->getType())
    return nullptr;
  if (LV.Generation != CurrentGeneration) {
    MemorySSA *MSSA = GetMSSA();
    if (!MSSA)
      return nullptr;
    auto *EarlierMA = MSSA->getMemoryAccess(LV.DefI);
    MemoryAccess *LaterDef =
        MSSA->getWalker()->getClobberingMemoryAccess(LI, BAA);
    if (!MSSA->dominates(LaterDef, EarlierMA))
      return nullptr;
  }
  return LV.DefI;
}

void loadCSE(Loop *L, DominatorTree &DT, ScalarEvolution &SE, LoopInfo &LI,
             BatchAAResults &BAA, function_ref<MemorySSA *()> GetMSSA) {
  ScopedHashTable<const SCEV *, LoadValue> AvailableLoads;
  SmallVector<std::unique_ptr<StackNode>> NodesToProcess;
  DomTreeNode *HeaderD = DT.getNode(L->getHeader());
  NodesToProcess.emplace_back(new StackNode(AvailableLoads, 0, HeaderD,
                                            HeaderD->begin(), HeaderD->end()));

  unsigned CurrentGeneration = 0;
  while (!NodesToProcess.empty()) {
    StackNode *NodeToProcess = &*NodesToProcess.back();

    CurrentGeneration = NodeToProcess->currentGeneration();

    if (!NodeToProcess->isProcessed()) {
      // Process the node.

      // If this block has a single predecessor, then the predecessor is the
      // parent
      // of the domtree node and all of the live out memory values are still
      // current in this block.  If this block has multiple predecessors, then
      // they could have invalidated the live-out memory values of our parent
      // value.  For now, just be conservative and invalidate memory if this
      // block has multiple predecessors.
      if (!NodeToProcess->node()->getBlock()->getSinglePredecessor())
        ++CurrentGeneration;
      for (auto &I : make_early_inc_range(*NodeToProcess->node()->getBlock())) {

        auto *Load = dyn_cast<LoadInst>(&I);
        if (!Load || !Load->isSimple()) {
          if (I.mayWriteToMemory())
            CurrentGeneration++;
          continue;
        }

        const SCEV *PtrSCEV = SE.getSCEV(Load->getPointerOperand());
        LoadValue LV = AvailableLoads.lookup(PtrSCEV);
        if (Value *M =
                getMatchingValue(LV, Load, CurrentGeneration, BAA, GetMSSA)) {
          if (LI.replacementPreservesLCSSAForm(Load, M)) {
            Load->replaceAllUsesWith(M);
            Load->eraseFromParent();
          }
        } else {
          AvailableLoads.insert(PtrSCEV, LoadValue(Load, CurrentGeneration));
        }
      }
      NodeToProcess->childGeneration(CurrentGeneration);
      NodeToProcess->process();
    } else if (NodeToProcess->childIter() != NodeToProcess->end()) {
      // Push the next child onto the stack.
      DomTreeNode *Child = NodeToProcess->nextChild();
      if (!L->contains(Child->getBlock()))
        continue;
      NodesToProcess.emplace_back(
          new StackNode(AvailableLoads, NodeToProcess->childGeneration(), Child,
                        Child->begin(), Child->end()));
    } else {
      // It has been processed, and there are no more children to process,
      // so delete it and pop it off the stack.
      NodesToProcess.pop_back();
    }
  }
}

/// Perform some cleanup and simplifications on loops after unrolling. It is
/// useful to simplify the IV's in the new loop, as well as do a quick
/// simplify/dce pass of the instructions.
void llvm::simplifyLoopAfterUnroll(Loop *L, bool SimplifyIVs, LoopInfo *LI,
                                   ScalarEvolution *SE, DominatorTree *DT,
                                   AssumptionCache *AC,
                                   const TargetTransformInfo *TTI,
                                   AAResults *AA) {
  using namespace llvm::PatternMatch;

  // Simplify any new induction variables in the partially unrolled loop.
  if (SE && SimplifyIVs) {
    SmallVector<WeakTrackingVH, 16> DeadInsts;
    simplifyLoopIVs(L, SE, DT, LI, TTI, DeadInsts);

    // Aggressively clean up dead instructions that simplifyLoopIVs already
    // identified. Any remaining should be cleaned up below.
    while (!DeadInsts.empty()) {
      Value *V = DeadInsts.pop_back_val();
      if (Instruction *Inst = dyn_cast_or_null<Instruction>(V))
        RecursivelyDeleteTriviallyDeadInstructions(Inst);
    }

    if (AA) {
      std::unique_ptr<MemorySSA> MSSA = nullptr;
      BatchAAResults BAA(*AA);
      loadCSE(L, *DT, *SE, *LI, BAA, [L, AA, DT, &MSSA]() -> MemorySSA * {
        if (!MSSA)
          MSSA.reset(new MemorySSA(*L, AA, DT));
        return &*MSSA;
      });
    }
  }

  // At this point, the code is well formed.  Perform constprop, instsimplify,
  // and dce.
  const DataLayout &DL = L->getHeader()->getDataLayout();
  SmallVector<WeakTrackingVH, 16> DeadInsts;
  for (BasicBlock *BB : L->getBlocks()) {
    // Remove repeated debug instructions after loop unrolling.
    if (BB->getParent()->getSubprogram())
      RemoveRedundantDbgInstrs(BB);

    for (Instruction &Inst : llvm::make_early_inc_range(*BB)) {
      if (Value *V = simplifyInstruction(&Inst, {DL, nullptr, DT, AC}))
        if (LI->replacementPreservesLCSSAForm(&Inst, V))
          Inst.replaceAllUsesWith(V);
      if (isInstructionTriviallyDead(&Inst))
        DeadInsts.emplace_back(&Inst);

      // Fold ((add X, C1), C2) to (add X, C1+C2). This is very common in
      // unrolled loops, and handling this early allows following code to
      // identify the IV as a "simple recurrence" without first folding away
      // a long chain of adds.
      {
        Value *X;
        const APInt *C1, *C2;
        if (match(&Inst, m_Add(m_Add(m_Value(X), m_APInt(C1)), m_APInt(C2)))) {
          auto *InnerI = dyn_cast<Instruction>(Inst.getOperand(0));
          auto *InnerOBO = cast<OverflowingBinaryOperator>(Inst.getOperand(0));
          bool SignedOverflow;
          APInt NewC = C1->sadd_ov(*C2, SignedOverflow);
          Inst.setOperand(0, X);
          Inst.setOperand(1, ConstantInt::get(Inst.getType(), NewC));
          Inst.setHasNoUnsignedWrap(Inst.hasNoUnsignedWrap() &&
                                    InnerOBO->hasNoUnsignedWrap());
          Inst.setHasNoSignedWrap(Inst.hasNoSignedWrap() &&
                                  InnerOBO->hasNoSignedWrap() &&
                                  !SignedOverflow);
          if (InnerI && isInstructionTriviallyDead(InnerI))
            DeadInsts.emplace_back(InnerI);
        }
      }
    }
    // We can't do recursive deletion until we're done iterating, as we might
    // have a phi which (potentially indirectly) uses instructions later in
    // the block we're iterating through.
    RecursivelyDeleteTriviallyDeadInstructions(DeadInsts);
  }
}

// Loops containing convergent instructions that are uncontrolled or controlled
// from outside the loop must have a count that divides their TripMultiple.
LLVM_ATTRIBUTE_USED
static bool canHaveUnrollRemainder(const Loop *L) {
  if (getLoopConvergenceHeart(L))
    return false;

  // Check for uncontrolled convergent operations.
  for (auto &BB : L->blocks()) {
    for (auto &I : *BB) {
      if (isa<ConvergenceControlInst>(I))
        return true;
      if (auto *CB = dyn_cast<CallBase>(&I))
        if (CB->isConvergent())
          return CB->getConvergenceControlToken();
    }
  }
  return true;
}

/// Unroll the given loop by Count. The loop must be in LCSSA form.  Unrolling
/// can only fail when the loop's latch block is not terminated by a conditional
/// branch instruction. However, if the trip count (and multiple) are not known,
/// loop unrolling will mostly produce more code that is no faster.
///
/// If Runtime is true then UnrollLoop will try to insert a prologue or
/// epilogue that ensures the latch has a trip multiple of Count. UnrollLoop
/// will not runtime-unroll the loop if computing the run-time trip count will
/// be expensive and AllowExpensiveTripCount is false.
///
/// The LoopInfo Analysis that is passed will be kept consistent.
///
/// This utility preserves LoopInfo. It will also preserve ScalarEvolution and
/// DominatorTree if they are non-null.
///
/// If RemainderLoop is non-null, it will receive the remainder loop (if
/// required and not fully unrolled).
LoopUnrollResult
llvm::UnrollLoop(Loop *L, UnrollLoopOptions ULO, LoopInfo *LI,
                 ScalarEvolution *SE, DominatorTree *DT, AssumptionCache *AC,
                 const TargetTransformInfo *TTI, OptimizationRemarkEmitter *ORE,
                 bool PreserveLCSSA, Loop **RemainderLoop, AAResults *AA) {
  assert(DT && "DomTree is required");

  if (!L->getLoopPreheader()) {
    LLVM_DEBUG(dbgs() << "  Can't unroll; loop preheader-insertion failed.\n");
    return LoopUnrollResult::Unmodified;
  }

  if (!L->getLoopLatch()) {
    LLVM_DEBUG(dbgs() << "  Can't unroll; loop exit-block-insertion failed.\n");
    return LoopUnrollResult::Unmodified;
  }

  // Loops with indirectbr cannot be cloned.
  if (!L->isSafeToClone()) {
    LLVM_DEBUG(dbgs() << "  Can't unroll; Loop body cannot be cloned.\n");
    return LoopUnrollResult::Unmodified;
  }

  if (L->getHeader()->hasAddressTaken()) {
    // The loop-rotate pass can be helpful to avoid this in many cases.
    LLVM_DEBUG(
        dbgs() << "  Won't unroll loop: address of header block is taken.\n");
    return LoopUnrollResult::Unmodified;
  }

  assert(ULO.Count > 0);

  // All these values should be taken only after peeling because they might have
  // changed.
  BasicBlock *Preheader = L->getLoopPreheader();
  BasicBlock *Header = L->getHeader();
  BasicBlock *LatchBlock = L->getLoopLatch();
  SmallVector<BasicBlock *, 4> ExitBlocks;
  L->getExitBlocks(ExitBlocks);
  std::vector<BasicBlock *> OriginalLoopBlocks = L->getBlocks();

  const unsigned MaxTripCount = SE->getSmallConstantMaxTripCount(L);
  const bool MaxOrZero = SE->isBackedgeTakenCountMaxOrZero(L);
  unsigned EstimatedLoopInvocationWeight = 0;
  std::optional<unsigned> OriginalTripCount =
      llvm::getLoopEstimatedTripCount(L, &EstimatedLoopInvocationWeight);

  // Effectively "DCE" unrolled iterations that are beyond the max tripcount
  // and will never be executed.
  if (MaxTripCount && ULO.Count > MaxTripCount)
    ULO.Count = MaxTripCount;

  struct ExitInfo {
    unsigned TripCount;
    unsigned TripMultiple;
    unsigned BreakoutTrip;
    bool ExitOnTrue;
    BasicBlock *FirstExitingBlock = nullptr;
    SmallVector<BasicBlock *> ExitingBlocks;
  };
  DenseMap<BasicBlock *, ExitInfo> ExitInfos;
  SmallVector<BasicBlock *, 4> ExitingBlocks;
  L->getExitingBlocks(ExitingBlocks);
  for (auto *ExitingBlock : ExitingBlocks) {
    // The folding code is not prepared to deal with non-branch instructions
    // right now.
    auto *BI = dyn_cast<BranchInst>(ExitingBlock->getTerminator());
    if (!BI)
      continue;

    ExitInfo &Info = ExitInfos.try_emplace(ExitingBlock).first->second;
    Info.TripCount = SE->getSmallConstantTripCount(L, ExitingBlock);
    Info.TripMultiple = SE->getSmallConstantTripMultiple(L, ExitingBlock);
    if (Info.TripCount != 0) {
      Info.BreakoutTrip = Info.TripCount % ULO.Count;
      Info.TripMultiple = 0;
    } else {
      Info.BreakoutTrip = Info.TripMultiple =
          (unsigned)std::gcd(ULO.Count, Info.TripMultiple);
    }
    Info.ExitOnTrue = !L->contains(BI->getSuccessor(0));
    Info.ExitingBlocks.push_back(ExitingBlock);
    LLVM_DEBUG(dbgs() << "  Exiting block %" << ExitingBlock->getName()
                      << ": TripCount=" << Info.TripCount
                      << ", TripMultiple=" << Info.TripMultiple
                      << ", BreakoutTrip=" << Info.BreakoutTrip << "\n");
  }

  // Are we eliminating the loop control altogether?  Note that we can know
  // we're eliminating the backedge without knowing exactly which iteration
  // of the unrolled body exits.
  const bool CompletelyUnroll = ULO.Count == MaxTripCount;

  const bool PreserveOnlyFirst = CompletelyUnroll && MaxOrZero;

  // There's no point in performing runtime unrolling if this unroll count
  // results in a full unroll.
  if (CompletelyUnroll)
    ULO.Runtime = false;

  // Go through all exits of L and see if there are any phi-nodes there. We just
  // conservatively assume that they're inserted to preserve LCSSA form, which
  // means that complete unrolling might break this form. We need to either fix
  // it in-place after the transformation, or entirely rebuild LCSSA. TODO: For
  // now we just recompute LCSSA for the outer loop, but it should be possible
  // to fix it in-place.
  bool NeedToFixLCSSA =
      PreserveLCSSA && CompletelyUnroll &&
      any_of(ExitBlocks,
             [](const BasicBlock *BB) { return isa<PHINode>(BB->begin()); });

  // The current loop unroll pass can unroll loops that have
  // (1) single latch; and
  // (2a) latch is unconditional; or
  // (2b) latch is conditional and is an exiting block
  // FIXME: The implementation can be extended to work with more complicated
  // cases, e.g. loops with multiple latches.
  BranchInst *LatchBI = dyn_cast<BranchInst>(LatchBlock->getTerminator());

  // A conditional branch which exits the loop, which can be optimized to an
  // unconditional branch in the unrolled loop in some cases.
  bool LatchIsExiting = L->isLoopExiting(LatchBlock);
  if (!LatchBI || (LatchBI->isConditional() && !LatchIsExiting)) {
    LLVM_DEBUG(
        dbgs() << "Can't unroll; a conditional latch must exit the loop");
    return LoopUnrollResult::Unmodified;
  }

  assert((!ULO.Runtime || canHaveUnrollRemainder(L)) &&
         "Can't runtime unroll if loop contains a convergent operation.");

  bool EpilogProfitability =
      UnrollRuntimeEpilog.getNumOccurrences() ? UnrollRuntimeEpilog
                                              : isEpilogProfitable(L);

  if (ULO.Runtime &&
      !UnrollRuntimeLoopRemainder(L, ULO.Count, ULO.AllowExpensiveTripCount,
                                  EpilogProfitability, ULO.UnrollRemainder,
                                  ULO.ForgetAllSCEV, LI, SE, DT, AC, TTI,
                                  PreserveLCSSA, RemainderLoop)) {
    if (ULO.Force)
      ULO.Runtime = false;
    else {
      LLVM_DEBUG(dbgs() << "Won't unroll; remainder loop could not be "
                           "generated when assuming runtime trip count\n");
      return LoopUnrollResult::Unmodified;
    }
  }

  using namespace ore;
  // Report the unrolling decision.
  if (CompletelyUnroll) {
    LLVM_DEBUG(dbgs() << "COMPLETELY UNROLLING loop %" << Header->getName()
                      << " with trip count " << ULO.Count << "!\n");
    if (ORE)
      ORE->emit([&]() {
        return OptimizationRemark(DEBUG_TYPE, "FullyUnrolled", L->getStartLoc(),
                                  L->getHeader())
               << "completely unrolled loop with "
               << NV("UnrollCount", ULO.Count) << " iterations";
      });
  } else {
    LLVM_DEBUG(dbgs() << "UNROLLING loop %" << Header->getName() << " by "
                      << ULO.Count);
    if (ULO.Runtime)
      LLVM_DEBUG(dbgs() << " with run-time trip count");
    LLVM_DEBUG(dbgs() << "!\n");

    if (ORE)
      ORE->emit([&]() {
        OptimizationRemark Diag(DEBUG_TYPE, "PartialUnrolled", L->getStartLoc(),
                                L->getHeader());
        Diag << "unrolled loop by a factor of " << NV("UnrollCount", ULO.Count);
        if (ULO.Runtime)
          Diag << " with run-time trip count";
        return Diag;
      });
  }

  // We are going to make changes to this loop. SCEV may be keeping cached info
  // about it, in particular about backedge taken count. The changes we make
  // are guaranteed to invalidate this information for our loop. It is tempting
  // to only invalidate the loop being unrolled, but it is incorrect as long as
  // all exiting branches from all inner loops have impact on the outer loops,
  // and if something changes inside them then any of outer loops may also
  // change. When we forget outermost loop, we also forget all contained loops
  // and this is what we need here.
  if (SE) {
    if (ULO.ForgetAllSCEV)
      SE->forgetAllLoops();
    else {
      SE->forgetTopmostLoop(L);
      SE->forgetBlockAndLoopDispositions();
    }
  }

  if (!LatchIsExiting)
    ++NumUnrolledNotLatch;

  // For the first iteration of the loop, we should use the precloned values for
  // PHI nodes.  Insert associations now.
  ValueToValueMapTy LastValueMap;
  std::vector<PHINode*> OrigPHINode;
  for (BasicBlock::iterator I = Header->begin(); isa<PHINode>(I); ++I) {
    OrigPHINode.push_back(cast<PHINode>(I));
  }

  std::vector<BasicBlock *> Headers;
  std::vector<BasicBlock *> Latches;
  Headers.push_back(Header);
  Latches.push_back(LatchBlock);

  // The current on-the-fly SSA update requires blocks to be processed in
  // reverse postorder so that LastValueMap contains the correct value at each
  // exit.
  LoopBlocksDFS DFS(L);
  DFS.perform(LI);

  // Stash the DFS iterators before adding blocks to the loop.
  LoopBlocksDFS::RPOIterator BlockBegin = DFS.beginRPO();
  LoopBlocksDFS::RPOIterator BlockEnd = DFS.endRPO();

  std::vector<BasicBlock*> UnrolledLoopBlocks = L->getBlocks();

  // Loop Unrolling might create new loops. While we do preserve LoopInfo, we
  // might break loop-simplified form for these loops (as they, e.g., would
  // share the same exit blocks). We'll keep track of loops for which we can
  // break this so that later we can re-simplify them.
  SmallSetVector<Loop *, 4> LoopsToSimplify;
  for (Loop *SubLoop : *L)
    LoopsToSimplify.insert(SubLoop);

  // When a FSDiscriminator is enabled, we don't need to add the multiply
  // factors to the discriminators.
  if (Header->getParent()->shouldEmitDebugInfoForProfiling() &&
      !EnableFSDiscriminator)
    for (BasicBlock *BB : L->getBlocks())
      for (Instruction &I : *BB)
        if (!I.isDebugOrPseudoInst())
          if (const DILocation *DIL = I.getDebugLoc()) {
            auto NewDIL = DIL->cloneByMultiplyingDuplicationFactor(ULO.Count);
            if (NewDIL)
              I.setDebugLoc(*NewDIL);
            else
              LLVM_DEBUG(dbgs()
                         << "Failed to create new discriminator: "
                         << DIL->getFilename() << " Line: " << DIL->getLine());
          }

  // Identify what noalias metadata is inside the loop: if it is inside the
  // loop, the associated metadata must be cloned for each iteration.
  SmallVector<MDNode *, 6> LoopLocalNoAliasDeclScopes;
  identifyNoAliasScopesToClone(L->getBlocks(), LoopLocalNoAliasDeclScopes);

  // We place the unrolled iterations immediately after the original loop
  // latch.  This is a reasonable default placement if we don't have block
  // frequencies, and if we do, well the layout will be adjusted later.
  auto BlockInsertPt = std::next(LatchBlock->getIterator());
  for (unsigned It = 1; It != ULO.Count; ++It) {
    SmallVector<BasicBlock *, 8> NewBlocks;
    SmallDenseMap<const Loop *, Loop *, 4> NewLoops;
    NewLoops[L] = L;

    for (LoopBlocksDFS::RPOIterator BB = BlockBegin; BB != BlockEnd; ++BB) {
      ValueToValueMapTy VMap;
      BasicBlock *New = CloneBasicBlock(*BB, VMap, "." + Twine(It));
      Header->getParent()->insert(BlockInsertPt, New);

      assert((*BB != Header || LI->getLoopFor(*BB) == L) &&
             "Header should not be in a sub-loop");
      // Tell LI about New.
      const Loop *OldLoop = addClonedBlockToLoopInfo(*BB, New, LI, NewLoops);
      if (OldLoop)
        LoopsToSimplify.insert(NewLoops[OldLoop]);

      if (*BB == Header) {
        // Loop over all of the PHI nodes in the block, changing them to use
        // the incoming values from the previous block.
        for (PHINode *OrigPHI : OrigPHINode) {
          PHINode *NewPHI = cast<PHINode>(VMap[OrigPHI]);
          Value *InVal = NewPHI->getIncomingValueForBlock(LatchBlock);
          if (Instruction *InValI = dyn_cast<Instruction>(InVal))
            if (It > 1 && L->contains(InValI))
              InVal = LastValueMap[InValI];
          VMap[OrigPHI] = InVal;
          NewPHI->eraseFromParent();
        }

        // Eliminate copies of the loop heart intrinsic, if any.
        if (ULO.Heart) {
          auto it = VMap.find(ULO.Heart);
          assert(it != VMap.end());
          Instruction *heartCopy = cast<Instruction>(it->second);
          heartCopy->eraseFromParent();
          VMap.erase(it);
        }
      }

      // Update our running map of newest clones
      LastValueMap[*BB] = New;
      for (ValueToValueMapTy::iterator VI = VMap.begin(), VE = VMap.end();
           VI != VE; ++VI)
        LastValueMap[VI->first] = VI->second;

      // Add phi entries for newly created values to all exit blocks.
      for (BasicBlock *Succ : successors(*BB)) {
        if (L->contains(Succ))
          continue;
        for (PHINode &PHI : Succ->phis()) {
          Value *Incoming = PHI.getIncomingValueForBlock(*BB);
          ValueToValueMapTy::iterator It = LastValueMap.find(Incoming);
          if (It != LastValueMap.end())
            Incoming = It->second;
          PHI.addIncoming(Incoming, New);
          SE->forgetValue(&PHI);
        }
      }
      // Keep track of new headers and latches as we create them, so that
      // we can insert the proper branches later.
      if (*BB == Header)
        Headers.push_back(New);
      if (*BB == LatchBlock)
        Latches.push_back(New);

      // Keep track of the exiting block and its successor block contained in
      // the loop for the current iteration.
      auto ExitInfoIt = ExitInfos.find(*BB);
      if (ExitInfoIt != ExitInfos.end())
        ExitInfoIt->second.ExitingBlocks.push_back(New);

      NewBlocks.push_back(New);
      UnrolledLoopBlocks.push_back(New);

      // Update DomTree: since we just copy the loop body, and each copy has a
      // dedicated entry block (copy of the header block), this header's copy
      // dominates all copied blocks. That means, dominance relations in the
      // copied body are the same as in the original body.
      if (*BB == Header)
        DT->addNewBlock(New, Latches[It - 1]);
      else {
        auto BBDomNode = DT->getNode(*BB);
        auto BBIDom = BBDomNode->getIDom();
        BasicBlock *OriginalBBIDom = BBIDom->getBlock();
        DT->addNewBlock(
            New, cast<BasicBlock>(LastValueMap[cast<Value>(OriginalBBIDom)]));
      }
    }

    // Remap all instructions in the most recent iteration
    remapInstructionsInBlocks(NewBlocks, LastValueMap);
    for (BasicBlock *NewBlock : NewBlocks)
      for (Instruction &I : *NewBlock)
        if (auto *II = dyn_cast<AssumeInst>(&I))
          AC->registerAssumption(II);

    {
      // Identify what other metadata depends on the cloned version. After
      // cloning, replace the metadata with the corrected version for both
      // memory instructions and noalias intrinsics.
      std::string ext = (Twine("It") + Twine(It)).str();
      cloneAndAdaptNoAliasScopes(LoopLocalNoAliasDeclScopes, NewBlocks,
                                 Header->getContext(), ext);
    }
  }

  // Loop over the PHI nodes in the original block, setting incoming values.
  for (PHINode *PN : OrigPHINode) {
    if (CompletelyUnroll) {
      PN->replaceAllUsesWith(PN->getIncomingValueForBlock(Preheader));
      PN->eraseFromParent();
    } else if (ULO.Count > 1) {
      Value *InVal = PN->removeIncomingValue(LatchBlock, false);
      // If this value was defined in the loop, take the value defined by the
      // last iteration of the loop.
      if (Instruction *InValI = dyn_cast<Instruction>(InVal)) {
        if (L->contains(InValI))
          InVal = LastValueMap[InVal];
      }
      assert(Latches.back() == LastValueMap[LatchBlock] && "bad last latch");
      PN->addIncoming(InVal, Latches.back());
    }
  }

  // Connect latches of the unrolled iterations to the headers of the next
  // iteration. Currently they point to the header of the same iteration.
  for (unsigned i = 0, e = Latches.size(); i != e; ++i) {
    unsigned j = (i + 1) % e;
    Latches[i]->getTerminator()->replaceSuccessorWith(Headers[i], Headers[j]);
  }

  // Update dominators of blocks we might reach through exits.
  // Immediate dominator of such block might change, because we add more
  // routes which can lead to the exit: we can now reach it from the copied
  // iterations too.
  if (ULO.Count > 1) {
    for (auto *BB : OriginalLoopBlocks) {
      auto *BBDomNode = DT->getNode(BB);
      SmallVector<BasicBlock *, 16> ChildrenToUpdate;
      for (auto *ChildDomNode : BBDomNode->children()) {
        auto *ChildBB = ChildDomNode->getBlock();
        if (!L->contains(ChildBB))
          ChildrenToUpdate.push_back(ChildBB);
      }
      // The new idom of the block will be the nearest common dominator
      // of all copies of the previous idom. This is equivalent to the
      // nearest common dominator of the previous idom and the first latch,
      // which dominates all copies of the previous idom.
      BasicBlock *NewIDom = DT->findNearestCommonDominator(BB, LatchBlock);
      for (auto *ChildBB : ChildrenToUpdate)
        DT->changeImmediateDominator(ChildBB, NewIDom);
    }
  }

  assert(!UnrollVerifyDomtree ||
         DT->verify(DominatorTree::VerificationLevel::Fast));

  SmallVector<DominatorTree::UpdateType> DTUpdates;
  auto SetDest = [&](BasicBlock *Src, bool WillExit, bool ExitOnTrue) {
    auto *Term = cast<BranchInst>(Src->getTerminator());
    const unsigned Idx = ExitOnTrue ^ WillExit;
    BasicBlock *Dest = Term->getSuccessor(Idx);
    BasicBlock *DeadSucc = Term->getSuccessor(1-Idx);

    // Remove predecessors from all non-Dest successors.
    DeadSucc->removePredecessor(Src, /* KeepOneInputPHIs */ true);

    // Replace the conditional branch with an unconditional one.
    BranchInst::Create(Dest, Term->getIterator());
    Term->eraseFromParent();

    DTUpdates.emplace_back(DominatorTree::Delete, Src, DeadSucc);
  };

  auto WillExit = [&](const ExitInfo &Info, unsigned i, unsigned j,
                      bool IsLatch) -> std::optional<bool> {
    if (CompletelyUnroll) {
      if (PreserveOnlyFirst) {
        if (i == 0)
          return std::nullopt;
        return j == 0;
      }
      // Complete (but possibly inexact) unrolling
      if (j == 0)
        return true;
      if (Info.TripCount && j != Info.TripCount)
        return false;
      return std::nullopt;
    }

    if (ULO.Runtime) {
      // If runtime unrolling inserts a prologue, information about non-latch
      // exits may be stale.
      if (IsLatch && j != 0)
        return false;
      return std::nullopt;
    }

    if (j != Info.BreakoutTrip &&
        (Info.TripMultiple == 0 || j % Info.TripMultiple != 0)) {
      // If we know the trip count or a multiple of it, we can safely use an
      // unconditional branch for some iterations.
      return false;
    }
    return std::nullopt;
  };

  // Fold branches for iterations where we know that they will exit or not
  // exit.
  for (auto &Pair : ExitInfos) {
    ExitInfo &Info = Pair.second;
    for (unsigned i = 0, e = Info.ExitingBlocks.size(); i != e; ++i) {
      // The branch destination.
      unsigned j = (i + 1) % e;
      bool IsLatch = Pair.first == LatchBlock;
      std::optional<bool> KnownWillExit = WillExit(Info, i, j, IsLatch);
      if (!KnownWillExit) {
        if (!Info.FirstExitingBlock)
          Info.FirstExitingBlock = Info.ExitingBlocks[i];
        continue;
      }

      // We don't fold known-exiting branches for non-latch exits here,
      // because this ensures that both all loop blocks and all exit blocks
      // remain reachable in the CFG.
      // TODO: We could fold these branches, but it would require much more
      // sophisticated updates to LoopInfo.
      if (*KnownWillExit && !IsLatch) {
        if (!Info.FirstExitingBlock)
          Info.FirstExitingBlock = Info.ExitingBlocks[i];
        continue;
      }

      SetDest(Info.ExitingBlocks[i], *KnownWillExit, Info.ExitOnTrue);
    }
  }

  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  DomTreeUpdater *DTUToUse = &DTU;
  if (ExitingBlocks.size() == 1 && ExitInfos.size() == 1) {
    // Manually update the DT if there's a single exiting node. In that case
    // there's a single exit node and it is sufficient to update the nodes
    // immediately dominated by the original exiting block. They will become
    // dominated by the first exiting block that leaves the loop after
    // unrolling. Note that the CFG inside the loop does not change, so there's
    // no need to update the DT inside the unrolled loop.
    DTUToUse = nullptr;
    auto &[OriginalExit, Info] = *ExitInfos.begin();
    if (!Info.FirstExitingBlock)
      Info.FirstExitingBlock = Info.ExitingBlocks.back();
    for (auto *C : to_vector(DT->getNode(OriginalExit)->children())) {
      if (L->contains(C->getBlock()))
        continue;
      C->setIDom(DT->getNode(Info.FirstExitingBlock));
    }
  } else {
    DTU.applyUpdates(DTUpdates);
  }

  // When completely unrolling, the last latch becomes unreachable.
  if (!LatchIsExiting && CompletelyUnroll) {
    // There is no need to update the DT here, because there must be a unique
    // latch. Hence if the latch is not exiting it must directly branch back to
    // the original loop header and does not dominate any nodes.
    assert(LatchBlock->getSingleSuccessor() && "Loop with multiple latches?");
    changeToUnreachable(Latches.back()->getTerminator(), PreserveLCSSA);
  }

  // Merge adjacent basic blocks, if possible.
  for (BasicBlock *Latch : Latches) {
    BranchInst *Term = dyn_cast<BranchInst>(Latch->getTerminator());
    assert((Term ||
            (CompletelyUnroll && !LatchIsExiting && Latch == Latches.back())) &&
           "Need a branch as terminator, except when fully unrolling with "
           "unconditional latch");
    if (Term && Term->isUnconditional()) {
      BasicBlock *Dest = Term->getSuccessor(0);
      BasicBlock *Fold = Dest->getUniquePredecessor();
      if (MergeBlockIntoPredecessor(Dest, /*DTU=*/DTUToUse, LI,
                                    /*MSSAU=*/nullptr, /*MemDep=*/nullptr,
                                    /*PredecessorWithTwoSuccessors=*/false,
                                    DTUToUse ? nullptr : DT)) {
        // Dest has been folded into Fold. Update our worklists accordingly.
        std::replace(Latches.begin(), Latches.end(), Dest, Fold);
        llvm::erase(UnrolledLoopBlocks, Dest);
      }
    }
  }

  if (DTUToUse) {
    // Apply updates to the DomTree.
    DT = &DTU.getDomTree();
  }
  assert(!UnrollVerifyDomtree ||
         DT->verify(DominatorTree::VerificationLevel::Fast));

  // At this point, the code is well formed.  We now simplify the unrolled loop,
  // doing constant propagation and dead code elimination as we go.
  simplifyLoopAfterUnroll(L, !CompletelyUnroll && ULO.Count > 1, LI, SE, DT, AC,
                          TTI, AA);

  NumCompletelyUnrolled += CompletelyUnroll;
  ++NumUnrolled;

  Loop *OuterL = L->getParentLoop();
  // Update LoopInfo if the loop is completely removed.
  if (CompletelyUnroll) {
    LI->erase(L);
    // We shouldn't try to use `L` anymore.
    L = nullptr;
  } else if (OriginalTripCount) {
    // Update the trip count. Note that the remainder has already logic
    // computing it in `UnrollRuntimeLoopRemainder`.
    setLoopEstimatedTripCount(L, *OriginalTripCount / ULO.Count,
                              EstimatedLoopInvocationWeight);
  }

  // LoopInfo should not be valid, confirm that.
  if (UnrollVerifyLoopInfo)
    LI->verify(*DT);

  // After complete unrolling most of the blocks should be contained in OuterL.
  // However, some of them might happen to be out of OuterL (e.g. if they
  // precede a loop exit). In this case we might need to insert PHI nodes in
  // order to preserve LCSSA form.
  // We don't need to check this if we already know that we need to fix LCSSA
  // form.
  // TODO: For now we just recompute LCSSA for the outer loop in this case, but
  // it should be possible to fix it in-place.
  if (PreserveLCSSA && OuterL && CompletelyUnroll && !NeedToFixLCSSA)
    NeedToFixLCSSA |= ::needToInsertPhisForLCSSA(OuterL, UnrolledLoopBlocks, LI);

  // Make sure that loop-simplify form is preserved. We want to simplify
  // at least one layer outside of the loop that was unrolled so that any
  // changes to the parent loop exposed by the unrolling are considered.
  if (OuterL) {
    // OuterL includes all loops for which we can break loop-simplify, so
    // it's sufficient to simplify only it (it'll recursively simplify inner
    // loops too).
    if (NeedToFixLCSSA) {
      // LCSSA must be performed on the outermost affected loop. The unrolled
      // loop's last loop latch is guaranteed to be in the outermost loop
      // after LoopInfo's been updated by LoopInfo::erase.
      Loop *LatchLoop = LI->getLoopFor(Latches.back());
      Loop *FixLCSSALoop = OuterL;
      if (!FixLCSSALoop->contains(LatchLoop))
        while (FixLCSSALoop->getParentLoop() != LatchLoop)
          FixLCSSALoop = FixLCSSALoop->getParentLoop();

      formLCSSARecursively(*FixLCSSALoop, *DT, LI, SE);
    } else if (PreserveLCSSA) {
      assert(OuterL->isLCSSAForm(*DT) &&
             "Loops should be in LCSSA form after loop-unroll.");
    }

    // TODO: That potentially might be compile-time expensive. We should try
    // to fix the loop-simplified form incrementally.
    simplifyLoop(OuterL, DT, LI, SE, AC, nullptr, PreserveLCSSA);
  } else {
    // Simplify loops for which we might've broken loop-simplify form.
    for (Loop *SubLoop : LoopsToSimplify)
      simplifyLoop(SubLoop, DT, LI, SE, AC, nullptr, PreserveLCSSA);
  }

  return CompletelyUnroll ? LoopUnrollResult::FullyUnrolled
                          : LoopUnrollResult::PartiallyUnrolled;
}

/// Given an llvm.loop loop id metadata node, returns the loop hint metadata
/// node with the given name (for example, "llvm.loop.unroll.count"). If no
/// such metadata node exists, then nullptr is returned.
MDNode *llvm::GetUnrollMetadata(MDNode *LoopID, StringRef Name) {
  // First operand should refer to the loop id itself.
  assert(LoopID->getNumOperands() > 0 && "requires at least one operand");
  assert(LoopID->getOperand(0) == LoopID && "invalid loop id");

  for (const MDOperand &MDO : llvm::drop_begin(LoopID->operands())) {
    MDNode *MD = dyn_cast<MDNode>(MDO);
    if (!MD)
      continue;

    MDString *S = dyn_cast<MDString>(MD->getOperand(0));
    if (!S)
      continue;

    if (Name == S->getString())
      return MD;
  }
  return nullptr;
}
