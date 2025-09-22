//===- CallSiteSplitting.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a transformation that tries to split a call-site to pass
// more constrained arguments if its argument is predicated in the control flow
// so that we can expose better context to the later passes (e.g, inliner, jump
// threading, or IPA-CP based function cloning, etc.).
// As of now we support two cases :
//
// 1) Try to a split call-site with constrained arguments, if any constraints
// on any argument can be found by following the single predecessors of the
// all site's predecessors. Currently this pass only handles call-sites with 2
// predecessors. For example, in the code below, we try to split the call-site
// since we can predicate the argument(ptr) based on the OR condition.
//
// Split from :
//   if (!ptr || c)
//     callee(ptr);
// to :
//   if (!ptr)
//     callee(null)         // set the known constant value
//   else if (c)
//     callee(nonnull ptr)  // set non-null attribute in the argument
//
// 2) We can also split a call-site based on constant incoming values of a PHI
// For example,
// from :
//   Header:
//    %c = icmp eq i32 %i1, %i2
//    br i1 %c, label %Tail, label %TBB
//   TBB:
//    br label Tail%
//   Tail:
//    %p = phi i32 [ 0, %Header], [ 1, %TBB]
//    call void @bar(i32 %p)
// to
//   Header:
//    %c = icmp eq i32 %i1, %i2
//    br i1 %c, label %Tail-split0, label %TBB
//   TBB:
//    br label %Tail-split1
//   Tail-split0:
//    call void @bar(i32 0)
//    br label %Tail
//   Tail-split1:
//    call void @bar(i32 1)
//    br label %Tail
//   Tail:
//    %p = phi i32 [ 0, %Tail-split0 ], [ 1, %Tail-split1 ]
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/CallSiteSplitting.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Local.h"

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "callsite-splitting"

STATISTIC(NumCallSiteSplit, "Number of call-site split");

/// Only allow instructions before a call, if their CodeSize cost is below
/// DuplicationThreshold. Those instructions need to be duplicated in all
/// split blocks.
static cl::opt<unsigned>
    DuplicationThreshold("callsite-splitting-duplication-threshold", cl::Hidden,
                         cl::desc("Only allow instructions before a call, if "
                                  "their cost is below DuplicationThreshold"),
                         cl::init(5));

static void addNonNullAttribute(CallBase &CB, Value *Op) {
  unsigned ArgNo = 0;
  for (auto &I : CB.args()) {
    if (&*I == Op)
      CB.addParamAttr(ArgNo, Attribute::NonNull);
    ++ArgNo;
  }
}

static void setConstantInArgument(CallBase &CB, Value *Op,
                                  Constant *ConstValue) {
  unsigned ArgNo = 0;
  for (auto &I : CB.args()) {
    if (&*I == Op) {
      // It is possible we have already added the non-null attribute to the
      // parameter by using an earlier constraining condition.
      CB.removeParamAttr(ArgNo, Attribute::NonNull);
      CB.setArgOperand(ArgNo, ConstValue);
    }
    ++ArgNo;
  }
}

static bool isCondRelevantToAnyCallArgument(ICmpInst *Cmp, CallBase &CB) {
  assert(isa<Constant>(Cmp->getOperand(1)) && "Expected a constant operand.");
  Value *Op0 = Cmp->getOperand(0);
  unsigned ArgNo = 0;
  for (auto I = CB.arg_begin(), E = CB.arg_end(); I != E; ++I, ++ArgNo) {
    // Don't consider constant or arguments that are already known non-null.
    if (isa<Constant>(*I) || CB.paramHasAttr(ArgNo, Attribute::NonNull))
      continue;

    if (*I == Op0)
      return true;
  }
  return false;
}

using ConditionTy = std::pair<ICmpInst *, unsigned>;
using ConditionsTy = SmallVector<ConditionTy, 2>;

/// If From has a conditional jump to To, add the condition to Conditions,
/// if it is relevant to any argument at CB.
static void recordCondition(CallBase &CB, BasicBlock *From, BasicBlock *To,
                            ConditionsTy &Conditions) {
  auto *BI = dyn_cast<BranchInst>(From->getTerminator());
  if (!BI || !BI->isConditional())
    return;

  CmpInst::Predicate Pred;
  Value *Cond = BI->getCondition();
  if (!match(Cond, m_ICmp(Pred, m_Value(), m_Constant())))
    return;

  ICmpInst *Cmp = cast<ICmpInst>(Cond);
  if (Pred == ICmpInst::ICMP_EQ || Pred == ICmpInst::ICMP_NE)
    if (isCondRelevantToAnyCallArgument(Cmp, CB))
      Conditions.push_back({Cmp, From->getTerminator()->getSuccessor(0) == To
                                     ? Pred
                                     : Cmp->getInversePredicate()});
}

/// Record ICmp conditions relevant to any argument in CB following Pred's
/// single predecessors. If there are conflicting conditions along a path, like
/// x == 1 and x == 0, the first condition will be used. We stop once we reach
/// an edge to StopAt.
static void recordConditions(CallBase &CB, BasicBlock *Pred,
                             ConditionsTy &Conditions, BasicBlock *StopAt) {
  BasicBlock *From = Pred;
  BasicBlock *To = Pred;
  SmallPtrSet<BasicBlock *, 4> Visited;
  while (To != StopAt && !Visited.count(From->getSinglePredecessor()) &&
         (From = From->getSinglePredecessor())) {
    recordCondition(CB, From, To, Conditions);
    Visited.insert(From);
    To = From;
  }
}

static void addConditions(CallBase &CB, const ConditionsTy &Conditions) {
  for (const auto &Cond : Conditions) {
    Value *Arg = Cond.first->getOperand(0);
    Constant *ConstVal = cast<Constant>(Cond.first->getOperand(1));
    if (Cond.second == ICmpInst::ICMP_EQ)
      setConstantInArgument(CB, Arg, ConstVal);
    else if (ConstVal->getType()->isPointerTy() && ConstVal->isNullValue()) {
      assert(Cond.second == ICmpInst::ICMP_NE);
      addNonNullAttribute(CB, Arg);
    }
  }
}

static SmallVector<BasicBlock *, 2> getTwoPredecessors(BasicBlock *BB) {
  SmallVector<BasicBlock *, 2> Preds(predecessors((BB)));
  assert(Preds.size() == 2 && "Expected exactly 2 predecessors!");
  return Preds;
}

static bool canSplitCallSite(CallBase &CB, TargetTransformInfo &TTI) {
  if (CB.isConvergent() || CB.cannotDuplicate())
    return false;

  // FIXME: As of now we handle only CallInst. InvokeInst could be handled
  // without too much effort.
  if (!isa<CallInst>(CB))
    return false;

  BasicBlock *CallSiteBB = CB.getParent();
  // Need 2 predecessors and cannot split an edge from an IndirectBrInst.
  SmallVector<BasicBlock *, 2> Preds(predecessors(CallSiteBB));
  if (Preds.size() != 2 || isa<IndirectBrInst>(Preds[0]->getTerminator()) ||
      isa<IndirectBrInst>(Preds[1]->getTerminator()))
    return false;

  // BasicBlock::canSplitPredecessors is more aggressive, so checking for
  // BasicBlock::isEHPad as well.
  if (!CallSiteBB->canSplitPredecessors() || CallSiteBB->isEHPad())
    return false;

  // Allow splitting a call-site only when the CodeSize cost of the
  // instructions before the call is less then DuplicationThreshold. The
  // instructions before the call will be duplicated in the split blocks and
  // corresponding uses will be updated.
  InstructionCost Cost = 0;
  for (auto &InstBeforeCall :
       llvm::make_range(CallSiteBB->begin(), CB.getIterator())) {
    Cost += TTI.getInstructionCost(&InstBeforeCall,
                                   TargetTransformInfo::TCK_CodeSize);
    if (Cost >= DuplicationThreshold)
      return false;
  }

  return true;
}

static Instruction *cloneInstForMustTail(Instruction *I, Instruction *Before,
                                         Value *V) {
  Instruction *Copy = I->clone();
  Copy->setName(I->getName());
  Copy->insertBefore(Before);
  if (V)
    Copy->setOperand(0, V);
  return Copy;
}

/// Copy mandatory `musttail` return sequence that follows original `CI`, and
/// link it up to `NewCI` value instead:
///
///   * (optional) `bitcast NewCI to ...`
///   * `ret bitcast or NewCI`
///
/// Insert this sequence right before `SplitBB`'s terminator, which will be
/// cleaned up later in `splitCallSite` below.
static void copyMustTailReturn(BasicBlock *SplitBB, Instruction *CI,
                               Instruction *NewCI) {
  bool IsVoid = SplitBB->getParent()->getReturnType()->isVoidTy();
  auto II = std::next(CI->getIterator());

  BitCastInst* BCI = dyn_cast<BitCastInst>(&*II);
  if (BCI)
    ++II;

  ReturnInst* RI = dyn_cast<ReturnInst>(&*II);
  assert(RI && "`musttail` call must be followed by `ret` instruction");

  Instruction *TI = SplitBB->getTerminator();
  Value *V = NewCI;
  if (BCI)
    V = cloneInstForMustTail(BCI, TI, V);
  cloneInstForMustTail(RI, TI, IsVoid ? nullptr : V);

  // FIXME: remove TI here, `DuplicateInstructionsInSplitBetween` has a bug
  // that prevents doing this now.
}

/// For each (predecessor, conditions from predecessors) pair, it will split the
/// basic block containing the call site, hook it up to the predecessor and
/// replace the call instruction with new call instructions, which contain
/// constraints based on the conditions from their predecessors.
/// For example, in the IR below with an OR condition, the call-site can
/// be split. In this case, Preds for Tail is [(Header, a == null),
/// (TBB, a != null, b == null)]. Tail is replaced by 2 split blocks, containing
/// CallInst1, which has constraints based on the conditions from Head and
/// CallInst2, which has constraints based on the conditions coming from TBB.
///
/// From :
///
///   Header:
///     %c = icmp eq i32* %a, null
///     br i1 %c %Tail, %TBB
///   TBB:
///     %c2 = icmp eq i32* %b, null
///     br i1 %c %Tail, %End
///   Tail:
///     %ca = call i1  @callee (i32* %a, i32* %b)
///
///  to :
///
///   Header:                          // PredBB1 is Header
///     %c = icmp eq i32* %a, null
///     br i1 %c %Tail-split1, %TBB
///   TBB:                             // PredBB2 is TBB
///     %c2 = icmp eq i32* %b, null
///     br i1 %c %Tail-split2, %End
///   Tail-split1:
///     %ca1 = call @callee (i32* null, i32* %b)         // CallInst1
///    br %Tail
///   Tail-split2:
///     %ca2 = call @callee (i32* nonnull %a, i32* null) // CallInst2
///    br %Tail
///   Tail:
///    %p = phi i1 [%ca1, %Tail-split1],[%ca2, %Tail-split2]
///
/// Note that in case any arguments at the call-site are constrained by its
/// predecessors, new call-sites with more constrained arguments will be
/// created in createCallSitesOnPredicatedArgument().
static void splitCallSite(CallBase &CB,
                          ArrayRef<std::pair<BasicBlock *, ConditionsTy>> Preds,
                          DomTreeUpdater &DTU) {
  BasicBlock *TailBB = CB.getParent();
  bool IsMustTailCall = CB.isMustTailCall();

  PHINode *CallPN = nullptr;

  // `musttail` calls must be followed by optional `bitcast`, and `ret`. The
  // split blocks will be terminated right after that so there're no users for
  // this phi in a `TailBB`.
  if (!IsMustTailCall && !CB.use_empty()) {
    CallPN = PHINode::Create(CB.getType(), Preds.size(), "phi.call");
    CallPN->setDebugLoc(CB.getDebugLoc());
  }

  LLVM_DEBUG(dbgs() << "split call-site : " << CB << " into \n");

  assert(Preds.size() == 2 && "The ValueToValueMaps array has size 2.");
  // ValueToValueMapTy is neither copy nor moveable, so we use a simple array
  // here.
  ValueToValueMapTy ValueToValueMaps[2];
  for (unsigned i = 0; i < Preds.size(); i++) {
    BasicBlock *PredBB = Preds[i].first;
    BasicBlock *SplitBlock = DuplicateInstructionsInSplitBetween(
        TailBB, PredBB, &*std::next(CB.getIterator()), ValueToValueMaps[i],
        DTU);
    assert(SplitBlock && "Unexpected new basic block split.");

    auto *NewCI =
        cast<CallBase>(&*std::prev(SplitBlock->getTerminator()->getIterator()));
    addConditions(*NewCI, Preds[i].second);

    // Handle PHIs used as arguments in the call-site.
    for (PHINode &PN : TailBB->phis()) {
      unsigned ArgNo = 0;
      for (auto &CI : CB.args()) {
        if (&*CI == &PN) {
          NewCI->setArgOperand(ArgNo, PN.getIncomingValueForBlock(SplitBlock));
        }
        ++ArgNo;
      }
    }
    LLVM_DEBUG(dbgs() << "    " << *NewCI << " in " << SplitBlock->getName()
                      << "\n");
    if (CallPN)
      CallPN->addIncoming(NewCI, SplitBlock);

    // Clone and place bitcast and return instructions before `TI`
    if (IsMustTailCall)
      copyMustTailReturn(SplitBlock, &CB, NewCI);
  }

  NumCallSiteSplit++;

  // FIXME: remove TI in `copyMustTailReturn`
  if (IsMustTailCall) {
    // Remove superfluous `br` terminators from the end of the Split blocks
    // NOTE: Removing terminator removes the SplitBlock from the TailBB's
    // predecessors. Therefore we must get complete list of Splits before
    // attempting removal.
    SmallVector<BasicBlock *, 2> Splits(predecessors((TailBB)));
    assert(Splits.size() == 2 && "Expected exactly 2 splits!");
    for (BasicBlock *BB : Splits) {
      BB->getTerminator()->eraseFromParent();
      DTU.applyUpdatesPermissive({{DominatorTree::Delete, BB, TailBB}});
    }

    // Erase the tail block once done with musttail patching
    DTU.deleteBB(TailBB);
    return;
  }

  BasicBlock::iterator OriginalBegin = TailBB->begin();
  // Replace users of the original call with a PHI mering call-sites split.
  if (CallPN) {
    CallPN->insertBefore(*TailBB, OriginalBegin);
    CB.replaceAllUsesWith(CallPN);
  }

  // Remove instructions moved to split blocks from TailBB, from the duplicated
  // call instruction to the beginning of the basic block. If an instruction
  // has any uses, add a new PHI node to combine the values coming from the
  // split blocks. The new PHI nodes are placed before the first original
  // instruction, so we do not end up deleting them. By using reverse-order, we
  // do not introduce unnecessary PHI nodes for def-use chains from the call
  // instruction to the beginning of the block.
  auto I = CB.getReverseIterator();
  Instruction *OriginalBeginInst = &*OriginalBegin;
  while (I != TailBB->rend()) {
    Instruction *CurrentI = &*I++;
    if (!CurrentI->use_empty()) {
      // If an existing PHI has users after the call, there is no need to create
      // a new one.
      if (isa<PHINode>(CurrentI))
        continue;
      PHINode *NewPN = PHINode::Create(CurrentI->getType(), Preds.size());
      NewPN->setDebugLoc(CurrentI->getDebugLoc());
      for (auto &Mapping : ValueToValueMaps)
        NewPN->addIncoming(Mapping[CurrentI],
                           cast<Instruction>(Mapping[CurrentI])->getParent());
      NewPN->insertBefore(*TailBB, TailBB->begin());
      CurrentI->replaceAllUsesWith(NewPN);
    }
    CurrentI->dropDbgRecords();
    CurrentI->eraseFromParent();
    // We are done once we handled the first original instruction in TailBB.
    if (CurrentI == OriginalBeginInst)
      break;
  }
}

// Return true if the call-site has an argument which is a PHI with only
// constant incoming values.
static bool isPredicatedOnPHI(CallBase &CB) {
  BasicBlock *Parent = CB.getParent();
  if (&CB != Parent->getFirstNonPHIOrDbg())
    return false;

  for (auto &PN : Parent->phis()) {
    for (auto &Arg : CB.args()) {
      if (&*Arg != &PN)
        continue;
      assert(PN.getNumIncomingValues() == 2 &&
             "Unexpected number of incoming values");
      if (PN.getIncomingBlock(0) == PN.getIncomingBlock(1))
        return false;
      if (PN.getIncomingValue(0) == PN.getIncomingValue(1))
        continue;
      if (isa<Constant>(PN.getIncomingValue(0)) &&
          isa<Constant>(PN.getIncomingValue(1)))
        return true;
    }
  }
  return false;
}

using PredsWithCondsTy = SmallVector<std::pair<BasicBlock *, ConditionsTy>, 2>;

// Check if any of the arguments in CS are predicated on a PHI node and return
// the set of predecessors we should use for splitting.
static PredsWithCondsTy shouldSplitOnPHIPredicatedArgument(CallBase &CB) {
  if (!isPredicatedOnPHI(CB))
    return {};

  auto Preds = getTwoPredecessors(CB.getParent());
  return {{Preds[0], {}}, {Preds[1], {}}};
}

// Checks if any of the arguments in CS are predicated in a predecessor and
// returns a list of predecessors with the conditions that hold on their edges
// to CS.
static PredsWithCondsTy shouldSplitOnPredicatedArgument(CallBase &CB,
                                                        DomTreeUpdater &DTU) {
  auto Preds = getTwoPredecessors(CB.getParent());
  if (Preds[0] == Preds[1])
    return {};

  // We can stop recording conditions once we reached the immediate dominator
  // for the block containing the call site. Conditions in predecessors of the
  // that node will be the same for all paths to the call site and splitting
  // is not beneficial.
  assert(DTU.hasDomTree() && "We need a DTU with a valid DT!");
  auto *CSDTNode = DTU.getDomTree().getNode(CB.getParent());
  BasicBlock *StopAt = CSDTNode ? CSDTNode->getIDom()->getBlock() : nullptr;

  SmallVector<std::pair<BasicBlock *, ConditionsTy>, 2> PredsCS;
  for (auto *Pred : llvm::reverse(Preds)) {
    ConditionsTy Conditions;
    // Record condition on edge BB(CS) <- Pred
    recordCondition(CB, Pred, CB.getParent(), Conditions);
    // Record conditions following Pred's single predecessors.
    recordConditions(CB, Pred, Conditions, StopAt);
    PredsCS.push_back({Pred, Conditions});
  }

  if (all_of(PredsCS, [](const std::pair<BasicBlock *, ConditionsTy> &P) {
        return P.second.empty();
      }))
    return {};

  return PredsCS;
}

static bool tryToSplitCallSite(CallBase &CB, TargetTransformInfo &TTI,
                               DomTreeUpdater &DTU) {
  // Check if we can split the call site.
  if (!CB.arg_size() || !canSplitCallSite(CB, TTI))
    return false;

  auto PredsWithConds = shouldSplitOnPredicatedArgument(CB, DTU);
  if (PredsWithConds.empty())
    PredsWithConds = shouldSplitOnPHIPredicatedArgument(CB);
  if (PredsWithConds.empty())
    return false;

  splitCallSite(CB, PredsWithConds, DTU);
  return true;
}

static bool doCallSiteSplitting(Function &F, TargetLibraryInfo &TLI,
                                TargetTransformInfo &TTI, DominatorTree &DT) {

  DomTreeUpdater DTU(&DT, DomTreeUpdater::UpdateStrategy::Lazy);
  bool Changed = false;
  for (BasicBlock &BB : llvm::make_early_inc_range(F)) {
    auto II = BB.getFirstNonPHIOrDbg()->getIterator();
    auto IE = BB.getTerminator()->getIterator();
    // Iterate until we reach the terminator instruction. tryToSplitCallSite
    // can replace BB's terminator in case BB is a successor of itself. In that
    // case, IE will be invalidated and we also have to check the current
    // terminator.
    while (II != IE && &*II != BB.getTerminator()) {
      CallBase *CB = dyn_cast<CallBase>(&*II++);
      if (!CB || isa<IntrinsicInst>(CB) || isInstructionTriviallyDead(CB, &TLI))
        continue;

      Function *Callee = CB->getCalledFunction();
      if (!Callee || Callee->isDeclaration())
        continue;

      // Successful musttail call-site splits result in erased CI and erased BB.
      // Check if such path is possible before attempting the splitting.
      bool IsMustTail = CB->isMustTailCall();

      Changed |= tryToSplitCallSite(*CB, TTI, DTU);

      // There're no interesting instructions after this. The call site
      // itself might have been erased on splitting.
      if (IsMustTail)
        break;
    }
  }
  return Changed;
}

PreservedAnalyses CallSiteSplittingPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  if (!doCallSiteSplitting(F, TLI, TTI, DT))
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}
