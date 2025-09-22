//===- MustExecute.cpp - Printer for isGuaranteedToExecute ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MustExecute.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "must-execute"

const DenseMap<BasicBlock *, ColorVector> &
LoopSafetyInfo::getBlockColors() const {
  return BlockColors;
}

void LoopSafetyInfo::copyColors(BasicBlock *New, BasicBlock *Old) {
  ColorVector &ColorsForNewBlock = BlockColors[New];
  ColorVector &ColorsForOldBlock = BlockColors[Old];
  ColorsForNewBlock = ColorsForOldBlock;
}

bool SimpleLoopSafetyInfo::blockMayThrow(const BasicBlock *BB) const {
  (void)BB;
  return anyBlockMayThrow();
}

bool SimpleLoopSafetyInfo::anyBlockMayThrow() const {
  return MayThrow;
}

void SimpleLoopSafetyInfo::computeLoopSafetyInfo(const Loop *CurLoop) {
  assert(CurLoop != nullptr && "CurLoop can't be null");
  BasicBlock *Header = CurLoop->getHeader();
  // Iterate over header and compute safety info.
  HeaderMayThrow = !isGuaranteedToTransferExecutionToSuccessor(Header);
  MayThrow = HeaderMayThrow;
  // Iterate over loop instructions and compute safety info.
  // Skip header as it has been computed and stored in HeaderMayThrow.
  // The first block in loopinfo.Blocks is guaranteed to be the header.
  assert(Header == *CurLoop->getBlocks().begin() &&
         "First block must be header");
  for (const BasicBlock *BB : llvm::drop_begin(CurLoop->blocks())) {
    MayThrow |= !isGuaranteedToTransferExecutionToSuccessor(BB);
    if (MayThrow)
      break;
  }

  computeBlockColors(CurLoop);
}

bool ICFLoopSafetyInfo::blockMayThrow(const BasicBlock *BB) const {
  return ICF.hasICF(BB);
}

bool ICFLoopSafetyInfo::anyBlockMayThrow() const {
  return MayThrow;
}

void ICFLoopSafetyInfo::computeLoopSafetyInfo(const Loop *CurLoop) {
  assert(CurLoop != nullptr && "CurLoop can't be null");
  ICF.clear();
  MW.clear();
  MayThrow = false;
  // Figure out the fact that at least one block may throw.
  for (const auto &BB : CurLoop->blocks())
    if (ICF.hasICF(&*BB)) {
      MayThrow = true;
      break;
    }
  computeBlockColors(CurLoop);
}

void ICFLoopSafetyInfo::insertInstructionTo(const Instruction *Inst,
                                            const BasicBlock *BB) {
  ICF.insertInstructionTo(Inst, BB);
  MW.insertInstructionTo(Inst, BB);
}

void ICFLoopSafetyInfo::removeInstruction(const Instruction *Inst) {
  ICF.removeInstruction(Inst);
  MW.removeInstruction(Inst);
}

void LoopSafetyInfo::computeBlockColors(const Loop *CurLoop) {
  // Compute funclet colors if we might sink/hoist in a function with a funclet
  // personality routine.
  Function *Fn = CurLoop->getHeader()->getParent();
  if (Fn->hasPersonalityFn())
    if (Constant *PersonalityFn = Fn->getPersonalityFn())
      if (isScopedEHPersonality(classifyEHPersonality(PersonalityFn)))
        BlockColors = colorEHFunclets(*Fn);
}

/// Return true if we can prove that the given ExitBlock is not reached on the
/// first iteration of the given loop.  That is, the backedge of the loop must
/// be executed before the ExitBlock is executed in any dynamic execution trace.
static bool CanProveNotTakenFirstIteration(const BasicBlock *ExitBlock,
                                           const DominatorTree *DT,
                                           const Loop *CurLoop) {
  auto *CondExitBlock = ExitBlock->getSinglePredecessor();
  if (!CondExitBlock)
    // expect unique exits
    return false;
  assert(CurLoop->contains(CondExitBlock) && "meaning of exit block");
  auto *BI = dyn_cast<BranchInst>(CondExitBlock->getTerminator());
  if (!BI || !BI->isConditional())
    return false;
  // If condition is constant and false leads to ExitBlock then we always
  // execute the true branch.
  if (auto *Cond = dyn_cast<ConstantInt>(BI->getCondition()))
    return BI->getSuccessor(Cond->getZExtValue() ? 1 : 0) == ExitBlock;
  auto *Cond = dyn_cast<CmpInst>(BI->getCondition());
  if (!Cond)
    return false;
  // todo: this would be a lot more powerful if we used scev, but all the
  // plumbing is currently missing to pass a pointer in from the pass
  // Check for cmp (phi [x, preheader] ...), y where (pred x, y is known
  auto *LHS = dyn_cast<PHINode>(Cond->getOperand(0));
  auto *RHS = Cond->getOperand(1);
  if (!LHS || LHS->getParent() != CurLoop->getHeader())
    return false;
  auto DL = ExitBlock->getDataLayout();
  auto *IVStart = LHS->getIncomingValueForBlock(CurLoop->getLoopPreheader());
  auto *SimpleValOrNull = simplifyCmpInst(Cond->getPredicate(),
                                          IVStart, RHS,
                                          {DL, /*TLI*/ nullptr,
                                              DT, /*AC*/ nullptr, BI});
  auto *SimpleCst = dyn_cast_or_null<Constant>(SimpleValOrNull);
  if (!SimpleCst)
    return false;
  if (ExitBlock == BI->getSuccessor(0))
    return SimpleCst->isZeroValue();
  assert(ExitBlock == BI->getSuccessor(1) && "implied by above");
  return SimpleCst->isAllOnesValue();
}

/// Collect all blocks from \p CurLoop which lie on all possible paths from
/// the header of \p CurLoop (inclusive) to BB (exclusive) into the set
/// \p Predecessors. If \p BB is the header, \p Predecessors will be empty.
static void collectTransitivePredecessors(
    const Loop *CurLoop, const BasicBlock *BB,
    SmallPtrSetImpl<const BasicBlock *> &Predecessors) {
  assert(Predecessors.empty() && "Garbage in predecessors set?");
  assert(CurLoop->contains(BB) && "Should only be called for loop blocks!");
  if (BB == CurLoop->getHeader())
    return;
  SmallVector<const BasicBlock *, 4> WorkList;
  for (const auto *Pred : predecessors(BB)) {
    Predecessors.insert(Pred);
    WorkList.push_back(Pred);
  }
  while (!WorkList.empty()) {
    auto *Pred = WorkList.pop_back_val();
    assert(CurLoop->contains(Pred) && "Should only reach loop blocks!");
    // We are not interested in backedges and we don't want to leave loop.
    if (Pred == CurLoop->getHeader())
      continue;
    // TODO: If BB lies in an inner loop of CurLoop, this will traverse over all
    // blocks of this inner loop, even those that are always executed AFTER the
    // BB. It may make our analysis more conservative than it could be, see test
    // @nested and @nested_no_throw in test/Analysis/MustExecute/loop-header.ll.
    // We can ignore backedge of all loops containing BB to get a sligtly more
    // optimistic result.
    for (const auto *PredPred : predecessors(Pred))
      if (Predecessors.insert(PredPred).second)
        WorkList.push_back(PredPred);
  }
}

bool LoopSafetyInfo::allLoopPathsLeadToBlock(const Loop *CurLoop,
                                             const BasicBlock *BB,
                                             const DominatorTree *DT) const {
  assert(CurLoop->contains(BB) && "Should only be called for loop blocks!");

  // Fast path: header is always reached once the loop is entered.
  if (BB == CurLoop->getHeader())
    return true;

  // Collect all transitive predecessors of BB in the same loop. This set will
  // be a subset of the blocks within the loop.
  SmallPtrSet<const BasicBlock *, 4> Predecessors;
  collectTransitivePredecessors(CurLoop, BB, Predecessors);

  // Bail out if a latch block is part of the predecessor set. In this case
  // we may take the backedge to the header and not execute other latch
  // successors.
  for (const BasicBlock *Pred : predecessors(CurLoop->getHeader()))
    // Predecessors only contains loop blocks, so we don't have to worry about
    // preheader predecessors here.
    if (Predecessors.contains(Pred))
      return false;

  // Make sure that all successors of, all predecessors of BB which are not
  // dominated by BB, are either:
  // 1) BB,
  // 2) Also predecessors of BB,
  // 3) Exit blocks which are not taken on 1st iteration.
  // Memoize blocks we've already checked.
  SmallPtrSet<const BasicBlock *, 4> CheckedSuccessors;
  for (const auto *Pred : Predecessors) {
    // Predecessor block may throw, so it has a side exit.
    if (blockMayThrow(Pred))
      return false;

    // BB dominates Pred, so if Pred runs, BB must run.
    // This is true when Pred is a loop latch.
    if (DT->dominates(BB, Pred))
      continue;

    for (const auto *Succ : successors(Pred))
      if (CheckedSuccessors.insert(Succ).second &&
          Succ != BB && !Predecessors.count(Succ))
        // By discharging conditions that are not executed on the 1st iteration,
        // we guarantee that *at least* on the first iteration all paths from
        // header that *may* execute will lead us to the block of interest. So
        // that if we had virtually peeled one iteration away, in this peeled
        // iteration the set of predecessors would contain only paths from
        // header to BB without any exiting edges that may execute.
        //
        // TODO: We only do it for exiting edges currently. We could use the
        // same function to skip some of the edges within the loop if we know
        // that they will not be taken on the 1st iteration.
        //
        // TODO: If we somehow know the number of iterations in loop, the same
        // check may be done for any arbitrary N-th iteration as long as N is
        // not greater than minimum number of iterations in this loop.
        if (CurLoop->contains(Succ) ||
            !CanProveNotTakenFirstIteration(Succ, DT, CurLoop))
          return false;
  }

  // All predecessors can only lead us to BB.
  return true;
}

/// Returns true if the instruction in a loop is guaranteed to execute at least
/// once.
bool SimpleLoopSafetyInfo::isGuaranteedToExecute(const Instruction &Inst,
                                                 const DominatorTree *DT,
                                                 const Loop *CurLoop) const {
  // If the instruction is in the header block for the loop (which is very
  // common), it is always guaranteed to dominate the exit blocks.  Since this
  // is a common case, and can save some work, check it now.
  if (Inst.getParent() == CurLoop->getHeader())
    // If there's a throw in the header block, we can't guarantee we'll reach
    // Inst unless we can prove that Inst comes before the potential implicit
    // exit.  At the moment, we use a (cheap) hack for the common case where
    // the instruction of interest is the first one in the block.
    return !HeaderMayThrow ||
           Inst.getParent()->getFirstNonPHIOrDbg() == &Inst;

  // If there is a path from header to exit or latch that doesn't lead to our
  // instruction's block, return false.
  return allLoopPathsLeadToBlock(CurLoop, Inst.getParent(), DT);
}

bool ICFLoopSafetyInfo::isGuaranteedToExecute(const Instruction &Inst,
                                              const DominatorTree *DT,
                                              const Loop *CurLoop) const {
  return !ICF.isDominatedByICFIFromSameBlock(&Inst) &&
         allLoopPathsLeadToBlock(CurLoop, Inst.getParent(), DT);
}

bool ICFLoopSafetyInfo::doesNotWriteMemoryBefore(const BasicBlock *BB,
                                                 const Loop *CurLoop) const {
  assert(CurLoop->contains(BB) && "Should only be called for loop blocks!");

  // Fast path: there are no instructions before header.
  if (BB == CurLoop->getHeader())
    return true;

  // Collect all transitive predecessors of BB in the same loop. This set will
  // be a subset of the blocks within the loop.
  SmallPtrSet<const BasicBlock *, 4> Predecessors;
  collectTransitivePredecessors(CurLoop, BB, Predecessors);
  // Find if there any instruction in either predecessor that could write
  // to memory.
  for (const auto *Pred : Predecessors)
    if (MW.mayWriteToMemory(Pred))
      return false;
  return true;
}

bool ICFLoopSafetyInfo::doesNotWriteMemoryBefore(const Instruction &I,
                                                 const Loop *CurLoop) const {
  auto *BB = I.getParent();
  assert(CurLoop->contains(BB) && "Should only be called for loop blocks!");
  return !MW.isDominatedByMemoryWriteFromSameBlock(&I) &&
         doesNotWriteMemoryBefore(BB, CurLoop);
}

static bool isMustExecuteIn(const Instruction &I, Loop *L, DominatorTree *DT) {
  // TODO: merge these two routines.  For the moment, we display the best
  // result obtained by *either* implementation.  This is a bit unfair since no
  // caller actually gets the full power at the moment.
  SimpleLoopSafetyInfo LSI;
  LSI.computeLoopSafetyInfo(L);
  return LSI.isGuaranteedToExecute(I, DT, L) ||
    isGuaranteedToExecuteForEveryIteration(&I, L);
}

namespace {
/// An assembly annotator class to print must execute information in
/// comments.
class MustExecuteAnnotatedWriter : public AssemblyAnnotationWriter {
  DenseMap<const Value*, SmallVector<Loop*, 4> > MustExec;

public:
  MustExecuteAnnotatedWriter(const Function &F,
                             DominatorTree &DT, LoopInfo &LI) {
    for (const auto &I: instructions(F)) {
      Loop *L = LI.getLoopFor(I.getParent());
      while (L) {
        if (isMustExecuteIn(I, L, &DT)) {
          MustExec[&I].push_back(L);
        }
        L = L->getParentLoop();
      };
    }
  }
  MustExecuteAnnotatedWriter(const Module &M,
                             DominatorTree &DT, LoopInfo &LI) {
    for (const auto &F : M)
    for (const auto &I: instructions(F)) {
      Loop *L = LI.getLoopFor(I.getParent());
      while (L) {
        if (isMustExecuteIn(I, L, &DT)) {
          MustExec[&I].push_back(L);
        }
        L = L->getParentLoop();
      };
    }
  }


  void printInfoComment(const Value &V, formatted_raw_ostream &OS) override {
    if (!MustExec.count(&V))
      return;

    const auto &Loops = MustExec.lookup(&V);
    const auto NumLoops = Loops.size();
    if (NumLoops > 1)
      OS << " ; (mustexec in " << NumLoops << " loops: ";
    else
      OS << " ; (mustexec in: ";

    ListSeparator LS;
    for (const Loop *L : Loops)
      OS << LS << L->getHeader()->getName();
    OS << ")";
  }
};
} // namespace

/// Return true if \p L might be an endless loop.
static bool maybeEndlessLoop(const Loop &L) {
  if (L.getHeader()->getParent()->hasFnAttribute(Attribute::WillReturn))
    return false;
  // TODO: Actually try to prove it is not.
  // TODO: If maybeEndlessLoop is going to be expensive, cache it.
  return true;
}

bool llvm::mayContainIrreducibleControl(const Function &F, const LoopInfo *LI) {
  if (!LI)
    return false;
  using RPOTraversal = ReversePostOrderTraversal<const Function *>;
  RPOTraversal FuncRPOT(&F);
  return containsIrreducibleCFG<const BasicBlock *, const RPOTraversal,
                                const LoopInfo>(FuncRPOT, *LI);
}

/// Lookup \p Key in \p Map and return the result, potentially after
/// initializing the optional through \p Fn(\p args).
template <typename K, typename V, typename FnTy, typename... ArgsTy>
static V getOrCreateCachedOptional(K Key, DenseMap<K, std::optional<V>> &Map,
                                   FnTy &&Fn, ArgsTy &&...args) {
  std::optional<V> &OptVal = Map[Key];
  if (!OptVal)
    OptVal = Fn(std::forward<ArgsTy>(args)...);
  return *OptVal;
}

const BasicBlock *
MustBeExecutedContextExplorer::findForwardJoinPoint(const BasicBlock *InitBB) {
  const LoopInfo *LI = LIGetter(*InitBB->getParent());
  const PostDominatorTree *PDT = PDTGetter(*InitBB->getParent());

  LLVM_DEBUG(dbgs() << "\tFind forward join point for " << InitBB->getName()
                    << (LI ? " [LI]" : "") << (PDT ? " [PDT]" : ""));

  const Function &F = *InitBB->getParent();
  const Loop *L = LI ? LI->getLoopFor(InitBB) : nullptr;
  const BasicBlock *HeaderBB = L ? L->getHeader() : InitBB;
  bool WillReturnAndNoThrow = (F.hasFnAttribute(Attribute::WillReturn) ||
                               (L && !maybeEndlessLoop(*L))) &&
                              F.doesNotThrow();
  LLVM_DEBUG(dbgs() << (L ? " [in loop]" : "")
                    << (WillReturnAndNoThrow ? " [WillReturn] [NoUnwind]" : "")
                    << "\n");

  // Determine the adjacent blocks in the given direction but exclude (self)
  // loops under certain circumstances.
  SmallVector<const BasicBlock *, 8> Worklist;
  for (const BasicBlock *SuccBB : successors(InitBB)) {
    bool IsLatch = SuccBB == HeaderBB;
    // Loop latches are ignored in forward propagation if the loop cannot be
    // endless and may not throw: control has to go somewhere.
    if (!WillReturnAndNoThrow || !IsLatch)
      Worklist.push_back(SuccBB);
  }
  LLVM_DEBUG(dbgs() << "\t\t#Worklist: " << Worklist.size() << "\n");

  // If there are no other adjacent blocks, there is no join point.
  if (Worklist.empty())
    return nullptr;

  // If there is one adjacent block, it is the join point.
  if (Worklist.size() == 1)
    return Worklist[0];

  // Try to determine a join block through the help of the post-dominance
  // tree. If no tree was provided, we perform simple pattern matching for one
  // block conditionals and one block loops only.
  const BasicBlock *JoinBB = nullptr;
  if (PDT)
    if (const auto *InitNode = PDT->getNode(InitBB))
      if (const auto *IDomNode = InitNode->getIDom())
        JoinBB = IDomNode->getBlock();

  if (!JoinBB && Worklist.size() == 2) {
    const BasicBlock *Succ0 = Worklist[0];
    const BasicBlock *Succ1 = Worklist[1];
    const BasicBlock *Succ0UniqueSucc = Succ0->getUniqueSuccessor();
    const BasicBlock *Succ1UniqueSucc = Succ1->getUniqueSuccessor();
    if (Succ0UniqueSucc == InitBB) {
      // InitBB -> Succ0 -> InitBB
      // InitBB -> Succ1  = JoinBB
      JoinBB = Succ1;
    } else if (Succ1UniqueSucc == InitBB) {
      // InitBB -> Succ1 -> InitBB
      // InitBB -> Succ0  = JoinBB
      JoinBB = Succ0;
    } else if (Succ0 == Succ1UniqueSucc) {
      // InitBB ->          Succ0 = JoinBB
      // InitBB -> Succ1 -> Succ0 = JoinBB
      JoinBB = Succ0;
    } else if (Succ1 == Succ0UniqueSucc) {
      // InitBB -> Succ0 -> Succ1 = JoinBB
      // InitBB ->          Succ1 = JoinBB
      JoinBB = Succ1;
    } else if (Succ0UniqueSucc == Succ1UniqueSucc) {
      // InitBB -> Succ0 -> JoinBB
      // InitBB -> Succ1 -> JoinBB
      JoinBB = Succ0UniqueSucc;
    }
  }

  if (!JoinBB && L)
    JoinBB = L->getUniqueExitBlock();

  if (!JoinBB)
    return nullptr;

  LLVM_DEBUG(dbgs() << "\t\tJoin block candidate: " << JoinBB->getName() << "\n");

  // In forward direction we check if control will for sure reach JoinBB from
  // InitBB, thus it can not be "stopped" along the way. Ways to "stop" control
  // are: infinite loops and instructions that do not necessarily transfer
  // execution to their successor. To check for them we traverse the CFG from
  // the adjacent blocks to the JoinBB, looking at all intermediate blocks.

  // If we know the function is "will-return" and "no-throw" there is no need
  // for futher checks.
  if (!F.hasFnAttribute(Attribute::WillReturn) || !F.doesNotThrow()) {

    auto BlockTransfersExecutionToSuccessor = [](const BasicBlock *BB) {
      return isGuaranteedToTransferExecutionToSuccessor(BB);
    };

    SmallPtrSet<const BasicBlock *, 16> Visited;
    while (!Worklist.empty()) {
      const BasicBlock *ToBB = Worklist.pop_back_val();
      if (ToBB == JoinBB)
        continue;

      // Make sure all loops in-between are finite.
      if (!Visited.insert(ToBB).second) {
        if (!F.hasFnAttribute(Attribute::WillReturn)) {
          if (!LI)
            return nullptr;

          bool MayContainIrreducibleControl = getOrCreateCachedOptional(
              &F, IrreducibleControlMap, mayContainIrreducibleControl, F, LI);
          if (MayContainIrreducibleControl)
            return nullptr;

          const Loop *L = LI->getLoopFor(ToBB);
          if (L && maybeEndlessLoop(*L))
            return nullptr;
        }

        continue;
      }

      // Make sure the block has no instructions that could stop control
      // transfer.
      bool TransfersExecution = getOrCreateCachedOptional(
          ToBB, BlockTransferMap, BlockTransfersExecutionToSuccessor, ToBB);
      if (!TransfersExecution)
        return nullptr;

      append_range(Worklist, successors(ToBB));
    }
  }

  LLVM_DEBUG(dbgs() << "\tJoin block: " << JoinBB->getName() << "\n");
  return JoinBB;
}
const BasicBlock *
MustBeExecutedContextExplorer::findBackwardJoinPoint(const BasicBlock *InitBB) {
  const LoopInfo *LI = LIGetter(*InitBB->getParent());
  const DominatorTree *DT = DTGetter(*InitBB->getParent());
  LLVM_DEBUG(dbgs() << "\tFind backward join point for " << InitBB->getName()
                    << (LI ? " [LI]" : "") << (DT ? " [DT]" : ""));

  // Try to determine a join block through the help of the dominance tree. If no
  // tree was provided, we perform simple pattern matching for one block
  // conditionals only.
  if (DT)
    if (const auto *InitNode = DT->getNode(InitBB))
      if (const auto *IDomNode = InitNode->getIDom())
        return IDomNode->getBlock();

  const Loop *L = LI ? LI->getLoopFor(InitBB) : nullptr;
  const BasicBlock *HeaderBB = L ? L->getHeader() : nullptr;

  // Determine the predecessor blocks but ignore backedges.
  SmallVector<const BasicBlock *, 8> Worklist;
  for (const BasicBlock *PredBB : predecessors(InitBB)) {
    bool IsBackedge =
        (PredBB == InitBB) || (HeaderBB == InitBB && L->contains(PredBB));
    // Loop backedges are ignored in backwards propagation: control has to come
    // from somewhere.
    if (!IsBackedge)
      Worklist.push_back(PredBB);
  }

  // If there are no other predecessor blocks, there is no join point.
  if (Worklist.empty())
    return nullptr;

  // If there is one predecessor block, it is the join point.
  if (Worklist.size() == 1)
    return Worklist[0];

  const BasicBlock *JoinBB = nullptr;
  if (Worklist.size() == 2) {
    const BasicBlock *Pred0 = Worklist[0];
    const BasicBlock *Pred1 = Worklist[1];
    const BasicBlock *Pred0UniquePred = Pred0->getUniquePredecessor();
    const BasicBlock *Pred1UniquePred = Pred1->getUniquePredecessor();
    if (Pred0 == Pred1UniquePred) {
      // InitBB <-          Pred0 = JoinBB
      // InitBB <- Pred1 <- Pred0 = JoinBB
      JoinBB = Pred0;
    } else if (Pred1 == Pred0UniquePred) {
      // InitBB <- Pred0 <- Pred1 = JoinBB
      // InitBB <-          Pred1 = JoinBB
      JoinBB = Pred1;
    } else if (Pred0UniquePred == Pred1UniquePred) {
      // InitBB <- Pred0 <- JoinBB
      // InitBB <- Pred1 <- JoinBB
      JoinBB = Pred0UniquePred;
    }
  }

  if (!JoinBB && L)
    JoinBB = L->getHeader();

  // In backwards direction there is no need to show termination of previous
  // instructions. If they do not terminate, the code afterward is dead, making
  // any information/transformation correct anyway.
  return JoinBB;
}

const Instruction *
MustBeExecutedContextExplorer::getMustBeExecutedNextInstruction(
    MustBeExecutedIterator &It, const Instruction *PP) {
  if (!PP)
    return PP;
  LLVM_DEBUG(dbgs() << "Find next instruction for " << *PP << "\n");

  // If we explore only inside a given basic block we stop at terminators.
  if (!ExploreInterBlock && PP->isTerminator()) {
    LLVM_DEBUG(dbgs() << "\tReached terminator in intra-block mode, done\n");
    return nullptr;
  }

  // If we do not traverse the call graph we check if we can make progress in
  // the current function. First, check if the instruction is guaranteed to
  // transfer execution to the successor.
  bool TransfersExecution = isGuaranteedToTransferExecutionToSuccessor(PP);
  if (!TransfersExecution)
    return nullptr;

  // If this is not a terminator we know that there is a single instruction
  // after this one that is executed next if control is transfered. If not,
  // we can try to go back to a call site we entered earlier. If none exists, we
  // do not know any instruction that has to be executd next.
  if (!PP->isTerminator()) {
    const Instruction *NextPP = PP->getNextNode();
    LLVM_DEBUG(dbgs() << "\tIntermediate instruction does transfer control\n");
    return NextPP;
  }

  // Finally, we have to handle terminators, trivial ones first.
  assert(PP->isTerminator() && "Expected a terminator!");

  // A terminator without a successor is not handled yet.
  if (PP->getNumSuccessors() == 0) {
    LLVM_DEBUG(dbgs() << "\tUnhandled terminator\n");
    return nullptr;
  }

  // A terminator with a single successor, we will continue at the beginning of
  // that one.
  if (PP->getNumSuccessors() == 1) {
    LLVM_DEBUG(
        dbgs() << "\tUnconditional terminator, continue with successor\n");
    return &PP->getSuccessor(0)->front();
  }

  // Multiple successors mean we need to find the join point where control flow
  // converges again. We use the findForwardJoinPoint helper function with
  // information about the function and helper analyses, if available.
  if (const BasicBlock *JoinBB = findForwardJoinPoint(PP->getParent()))
    return &JoinBB->front();

  LLVM_DEBUG(dbgs() << "\tNo join point found\n");
  return nullptr;
}

const Instruction *
MustBeExecutedContextExplorer::getMustBeExecutedPrevInstruction(
    MustBeExecutedIterator &It, const Instruction *PP) {
  if (!PP)
    return PP;

  bool IsFirst = !(PP->getPrevNode());
  LLVM_DEBUG(dbgs() << "Find next instruction for " << *PP
                    << (IsFirst ? " [IsFirst]" : "") << "\n");

  // If we explore only inside a given basic block we stop at the first
  // instruction.
  if (!ExploreInterBlock && IsFirst) {
    LLVM_DEBUG(dbgs() << "\tReached block front in intra-block mode, done\n");
    return nullptr;
  }

  // The block and function that contains the current position.
  const BasicBlock *PPBlock = PP->getParent();

  // If we are inside a block we know what instruction was executed before, the
  // previous one.
  if (!IsFirst) {
    const Instruction *PrevPP = PP->getPrevNode();
    LLVM_DEBUG(
        dbgs() << "\tIntermediate instruction, continue with previous\n");
    // We did not enter a callee so we simply return the previous instruction.
    return PrevPP;
  }

  // Finally, we have to handle the case where the program point is the first in
  // a block but not in the function. We use the findBackwardJoinPoint helper
  // function with information about the function and helper analyses, if
  // available.
  if (const BasicBlock *JoinBB = findBackwardJoinPoint(PPBlock))
    return &JoinBB->back();

  LLVM_DEBUG(dbgs() << "\tNo join point found\n");
  return nullptr;
}

MustBeExecutedIterator::MustBeExecutedIterator(
    MustBeExecutedContextExplorer &Explorer, const Instruction *I)
    : Explorer(Explorer), CurInst(I) {
  reset(I);
}

void MustBeExecutedIterator::reset(const Instruction *I) {
  Visited.clear();
  resetInstruction(I);
}

void MustBeExecutedIterator::resetInstruction(const Instruction *I) {
  CurInst = I;
  Head = Tail = nullptr;
  Visited.insert({I, ExplorationDirection::FORWARD});
  Visited.insert({I, ExplorationDirection::BACKWARD});
  if (Explorer.ExploreCFGForward)
    Head = I;
  if (Explorer.ExploreCFGBackward)
    Tail = I;
}

const Instruction *MustBeExecutedIterator::advance() {
  assert(CurInst && "Cannot advance an end iterator!");
  Head = Explorer.getMustBeExecutedNextInstruction(*this, Head);
  if (Head && Visited.insert({Head, ExplorationDirection ::FORWARD}).second)
    return Head;
  Head = nullptr;

  Tail = Explorer.getMustBeExecutedPrevInstruction(*this, Tail);
  if (Tail && Visited.insert({Tail, ExplorationDirection ::BACKWARD}).second)
    return Tail;
  Tail = nullptr;
  return nullptr;
}

PreservedAnalyses MustExecutePrinterPass::run(Function &F,
                                              FunctionAnalysisManager &AM) {
  auto &LI = AM.getResult<LoopAnalysis>(F);
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);

  MustExecuteAnnotatedWriter Writer(F, DT, LI);
  F.print(OS, &Writer);
  return PreservedAnalyses::all();
}

PreservedAnalyses
MustBeExecutedContextPrinterPass::run(Module &M, ModuleAnalysisManager &AM) {
  FunctionAnalysisManager &FAM =
      AM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
  GetterTy<const LoopInfo> LIGetter = [&](const Function &F) {
    return &FAM.getResult<LoopAnalysis>(const_cast<Function &>(F));
  };
  GetterTy<const DominatorTree> DTGetter = [&](const Function &F) {
    return &FAM.getResult<DominatorTreeAnalysis>(const_cast<Function &>(F));
  };
  GetterTy<const PostDominatorTree> PDTGetter = [&](const Function &F) {
    return &FAM.getResult<PostDominatorTreeAnalysis>(const_cast<Function &>(F));
  };

  MustBeExecutedContextExplorer Explorer(
      /* ExploreInterBlock */ true,
      /* ExploreCFGForward */ true,
      /* ExploreCFGBackward */ true, LIGetter, DTGetter, PDTGetter);

  for (Function &F : M) {
    for (Instruction &I : instructions(F)) {
      OS << "-- Explore context of: " << I << "\n";
      for (const Instruction *CI : Explorer.range(&I))
        OS << "  [F: " << CI->getFunction()->getName() << "] " << *CI << "\n";
    }
  }
  return PreservedAnalyses::all();
}
