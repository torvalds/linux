//===- CodeMoverUtils.cpp - CodeMover Utilities ----------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions perform movements on basic blocks, and instructions
// contained within a function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CodeMoverUtils.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"

using namespace llvm;

#define DEBUG_TYPE "codemover-utils"

STATISTIC(HasDependences,
          "Cannot move across instructions that has memory dependences");
STATISTIC(MayThrowException, "Cannot move across instructions that may throw");
STATISTIC(NotControlFlowEquivalent,
          "Instructions are not control flow equivalent");
STATISTIC(NotMovedPHINode, "Movement of PHINodes are not supported");
STATISTIC(NotMovedTerminator, "Movement of Terminator are not supported");

namespace {
/// Represent a control condition. A control condition is a condition of a
/// terminator to decide which successors to execute. The pointer field
/// represents the address of the condition of the terminator. The integer field
/// is a bool, it is true when the basic block is executed when V is true. For
/// example, `br %cond, bb0, bb1` %cond is a control condition of bb0 with the
/// integer field equals to true, while %cond is a control condition of bb1 with
/// the integer field equals to false.
using ControlCondition = PointerIntPair<Value *, 1, bool>;
#ifndef NDEBUG
raw_ostream &operator<<(raw_ostream &OS, const ControlCondition &C) {
  OS << "[" << *C.getPointer() << ", " << (C.getInt() ? "true" : "false")
     << "]";
  return OS;
}
#endif

/// Represent a set of control conditions required to execute ToBB from FromBB.
class ControlConditions {
  using ConditionVectorTy = SmallVector<ControlCondition, 6>;

  /// A SmallVector of control conditions.
  ConditionVectorTy Conditions;

public:
  /// Return a ControlConditions which stores all conditions required to execute
  /// \p BB from \p Dominator. If \p MaxLookup is non-zero, it limits the
  /// number of conditions to collect. Return std::nullopt if not all conditions
  /// are collected successfully, or we hit the limit.
  static const std::optional<ControlConditions>
  collectControlConditions(const BasicBlock &BB, const BasicBlock &Dominator,
                           const DominatorTree &DT,
                           const PostDominatorTree &PDT,
                           unsigned MaxLookup = 6);

  /// Return true if there exists no control conditions required to execute ToBB
  /// from FromBB.
  bool isUnconditional() const { return Conditions.empty(); }

  /// Return a constant reference of Conditions.
  const ConditionVectorTy &getControlConditions() const { return Conditions; }

  /// Add \p V as one of the ControlCondition in Condition with IsTrueCondition
  /// equals to \p True. Return true if inserted successfully.
  bool addControlCondition(ControlCondition C);

  /// Return true if for all control conditions in Conditions, there exists an
  /// equivalent control condition in \p Other.Conditions.
  bool isEquivalent(const ControlConditions &Other) const;

  /// Return true if \p C1 and \p C2 are equivalent.
  static bool isEquivalent(const ControlCondition &C1,
                           const ControlCondition &C2);

private:
  ControlConditions() = default;

  static bool isEquivalent(const Value &V1, const Value &V2);
  static bool isInverse(const Value &V1, const Value &V2);
};
} // namespace

static bool domTreeLevelBefore(DominatorTree *DT, const Instruction *InstA,
                               const Instruction *InstB) {
  // Use ordered basic block in case the 2 instructions are in the same
  // block.
  if (InstA->getParent() == InstB->getParent())
    return InstA->comesBefore(InstB);

  DomTreeNode *DA = DT->getNode(InstA->getParent());
  DomTreeNode *DB = DT->getNode(InstB->getParent());
  return DA->getLevel() < DB->getLevel();
}

const std::optional<ControlConditions>
ControlConditions::collectControlConditions(const BasicBlock &BB,
                                            const BasicBlock &Dominator,
                                            const DominatorTree &DT,
                                            const PostDominatorTree &PDT,
                                            unsigned MaxLookup) {
  assert(DT.dominates(&Dominator, &BB) && "Expecting Dominator to dominate BB");

  ControlConditions Conditions;
  unsigned NumConditions = 0;

  // BB is executed unconditional from itself.
  if (&Dominator == &BB)
    return Conditions;

  const BasicBlock *CurBlock = &BB;
  // Walk up the dominator tree from the associated DT node for BB to the
  // associated DT node for Dominator.
  do {
    assert(DT.getNode(CurBlock) && "Expecting a valid DT node for CurBlock");
    BasicBlock *IDom = DT.getNode(CurBlock)->getIDom()->getBlock();
    assert(DT.dominates(&Dominator, IDom) &&
           "Expecting Dominator to dominate IDom");

    // Limitation: can only handle branch instruction currently.
    const BranchInst *BI = dyn_cast<BranchInst>(IDom->getTerminator());
    if (!BI)
      return std::nullopt;

    bool Inserted = false;
    if (PDT.dominates(CurBlock, IDom)) {
      LLVM_DEBUG(dbgs() << CurBlock->getName()
                        << " is executed unconditionally from "
                        << IDom->getName() << "\n");
    } else if (PDT.dominates(CurBlock, BI->getSuccessor(0))) {
      LLVM_DEBUG(dbgs() << CurBlock->getName() << " is executed when \""
                        << *BI->getCondition() << "\" is true from "
                        << IDom->getName() << "\n");
      Inserted = Conditions.addControlCondition(
          ControlCondition(BI->getCondition(), true));
    } else if (PDT.dominates(CurBlock, BI->getSuccessor(1))) {
      LLVM_DEBUG(dbgs() << CurBlock->getName() << " is executed when \""
                        << *BI->getCondition() << "\" is false from "
                        << IDom->getName() << "\n");
      Inserted = Conditions.addControlCondition(
          ControlCondition(BI->getCondition(), false));
    } else
      return std::nullopt;

    if (Inserted)
      ++NumConditions;

    if (MaxLookup != 0 && NumConditions > MaxLookup)
      return std::nullopt;

    CurBlock = IDom;
  } while (CurBlock != &Dominator);

  return Conditions;
}

bool ControlConditions::addControlCondition(ControlCondition C) {
  bool Inserted = false;
  if (none_of(Conditions, [&](ControlCondition &Exists) {
        return ControlConditions::isEquivalent(C, Exists);
      })) {
    Conditions.push_back(C);
    Inserted = true;
  }

  LLVM_DEBUG(dbgs() << (Inserted ? "Inserted " : "Not inserted ") << C << "\n");
  return Inserted;
}

bool ControlConditions::isEquivalent(const ControlConditions &Other) const {
  if (Conditions.empty() && Other.Conditions.empty())
    return true;

  if (Conditions.size() != Other.Conditions.size())
    return false;

  return all_of(Conditions, [&](const ControlCondition &C) {
    return any_of(Other.Conditions, [&](const ControlCondition &OtherC) {
      return ControlConditions::isEquivalent(C, OtherC);
    });
  });
}

bool ControlConditions::isEquivalent(const ControlCondition &C1,
                                     const ControlCondition &C2) {
  if (C1.getInt() == C2.getInt()) {
    if (isEquivalent(*C1.getPointer(), *C2.getPointer()))
      return true;
  } else if (isInverse(*C1.getPointer(), *C2.getPointer()))
    return true;

  return false;
}

// FIXME: Use SCEV and reuse GVN/CSE logic to check for equivalence between
// Values.
// Currently, isEquivalent rely on other passes to ensure equivalent conditions
// have the same value, e.g. GVN.
bool ControlConditions::isEquivalent(const Value &V1, const Value &V2) {
  return &V1 == &V2;
}

bool ControlConditions::isInverse(const Value &V1, const Value &V2) {
  if (const CmpInst *Cmp1 = dyn_cast<CmpInst>(&V1))
    if (const CmpInst *Cmp2 = dyn_cast<CmpInst>(&V2)) {
      if (Cmp1->getPredicate() == Cmp2->getInversePredicate() &&
          Cmp1->getOperand(0) == Cmp2->getOperand(0) &&
          Cmp1->getOperand(1) == Cmp2->getOperand(1))
        return true;

      if (Cmp1->getPredicate() ==
              CmpInst::getSwappedPredicate(Cmp2->getInversePredicate()) &&
          Cmp1->getOperand(0) == Cmp2->getOperand(1) &&
          Cmp1->getOperand(1) == Cmp2->getOperand(0))
        return true;
    }
  return false;
}

bool llvm::isControlFlowEquivalent(const Instruction &I0, const Instruction &I1,
                                   const DominatorTree &DT,
                                   const PostDominatorTree &PDT) {
  return isControlFlowEquivalent(*I0.getParent(), *I1.getParent(), DT, PDT);
}

bool llvm::isControlFlowEquivalent(const BasicBlock &BB0, const BasicBlock &BB1,
                                   const DominatorTree &DT,
                                   const PostDominatorTree &PDT) {
  if (&BB0 == &BB1)
    return true;

  if ((DT.dominates(&BB0, &BB1) && PDT.dominates(&BB1, &BB0)) ||
      (PDT.dominates(&BB0, &BB1) && DT.dominates(&BB1, &BB0)))
    return true;

  // If the set of conditions required to execute BB0 and BB1 from their common
  // dominator are the same, then BB0 and BB1 are control flow equivalent.
  const BasicBlock *CommonDominator = DT.findNearestCommonDominator(&BB0, &BB1);
  LLVM_DEBUG(dbgs() << "The nearest common dominator of " << BB0.getName()
                    << " and " << BB1.getName() << " is "
                    << CommonDominator->getName() << "\n");

  const std::optional<ControlConditions> BB0Conditions =
      ControlConditions::collectControlConditions(BB0, *CommonDominator, DT,
                                                  PDT);
  if (BB0Conditions == std::nullopt)
    return false;

  const std::optional<ControlConditions> BB1Conditions =
      ControlConditions::collectControlConditions(BB1, *CommonDominator, DT,
                                                  PDT);
  if (BB1Conditions == std::nullopt)
    return false;

  return BB0Conditions->isEquivalent(*BB1Conditions);
}

static bool reportInvalidCandidate(const Instruction &I,
                                   llvm::Statistic &Stat) {
  ++Stat;
  LLVM_DEBUG(dbgs() << "Unable to move instruction: " << I << ". "
                    << Stat.getDesc());
  return false;
}

/// Collect all instructions in between \p StartInst and \p EndInst, and store
/// them in \p InBetweenInsts.
static void
collectInstructionsInBetween(Instruction &StartInst, const Instruction &EndInst,
                             SmallPtrSetImpl<Instruction *> &InBetweenInsts) {
  assert(InBetweenInsts.empty() && "Expecting InBetweenInsts to be empty");

  /// Get the next instructions of \p I, and push them to \p WorkList.
  auto getNextInsts = [](Instruction &I,
                         SmallPtrSetImpl<Instruction *> &WorkList) {
    if (Instruction *NextInst = I.getNextNode())
      WorkList.insert(NextInst);
    else {
      assert(I.isTerminator() && "Expecting a terminator instruction");
      for (BasicBlock *Succ : successors(&I))
        WorkList.insert(&Succ->front());
    }
  };

  SmallPtrSet<Instruction *, 10> WorkList;
  getNextInsts(StartInst, WorkList);
  while (!WorkList.empty()) {
    Instruction *CurInst = *WorkList.begin();
    WorkList.erase(CurInst);

    if (CurInst == &EndInst)
      continue;

    if (!InBetweenInsts.insert(CurInst).second)
      continue;

    getNextInsts(*CurInst, WorkList);
  }
}

bool llvm::isSafeToMoveBefore(Instruction &I, Instruction &InsertPoint,
                              DominatorTree &DT, const PostDominatorTree *PDT,
                              DependenceInfo *DI, bool CheckForEntireBlock) {
  // Skip tests when we don't have PDT or DI
  if (!PDT || !DI)
    return false;

  // Cannot move itself before itself.
  if (&I == &InsertPoint)
    return false;

  // Not moved.
  if (I.getNextNode() == &InsertPoint)
    return true;

  if (isa<PHINode>(I) || isa<PHINode>(InsertPoint))
    return reportInvalidCandidate(I, NotMovedPHINode);

  if (I.isTerminator())
    return reportInvalidCandidate(I, NotMovedTerminator);

  // TODO remove this limitation.
  if (!isControlFlowEquivalent(I, InsertPoint, DT, *PDT))
    return reportInvalidCandidate(I, NotControlFlowEquivalent);

  if (isReachedBefore(&I, &InsertPoint, &DT, PDT))
    for (const Use &U : I.uses())
      if (auto *UserInst = dyn_cast<Instruction>(U.getUser())) {
        // If InsertPoint is in a BB that comes after I, then we cannot move if
        // I is used in the terminator of the current BB.
        if (I.getParent() == InsertPoint.getParent() &&
            UserInst == I.getParent()->getTerminator())
          return false;
        if (UserInst != &InsertPoint && !DT.dominates(&InsertPoint, U)) {
          // If UserInst is an instruction that appears later in the same BB as
          // I, then it is okay to move since I will still be available when
          // UserInst is executed.
          if (CheckForEntireBlock && I.getParent() == UserInst->getParent() &&
              DT.dominates(&I, UserInst))
            continue;
          return false;
        }
      }
  if (isReachedBefore(&InsertPoint, &I, &DT, PDT))
    for (const Value *Op : I.operands())
      if (auto *OpInst = dyn_cast<Instruction>(Op)) {
        if (&InsertPoint == OpInst)
          return false;
        // If OpInst is an instruction that appears earlier in the same BB as
        // I, then it is okay to move since OpInst will still be available.
        if (CheckForEntireBlock && I.getParent() == OpInst->getParent() &&
            DT.dominates(OpInst, &I))
          continue;
        if (!DT.dominates(OpInst, &InsertPoint))
          return false;
      }

  DT.updateDFSNumbers();
  const bool MoveForward = domTreeLevelBefore(&DT, &I, &InsertPoint);
  Instruction &StartInst = (MoveForward ? I : InsertPoint);
  Instruction &EndInst = (MoveForward ? InsertPoint : I);
  SmallPtrSet<Instruction *, 10> InstsToCheck;
  collectInstructionsInBetween(StartInst, EndInst, InstsToCheck);
  if (!MoveForward)
    InstsToCheck.insert(&InsertPoint);

  // Check if there exists instructions which may throw, may synchonize, or may
  // never return, from I to InsertPoint.
  if (!isSafeToSpeculativelyExecute(&I))
    if (llvm::any_of(InstsToCheck, [](Instruction *I) {
          if (I->mayThrow())
            return true;

          const CallBase *CB = dyn_cast<CallBase>(I);
          if (!CB)
            return false;
          if (!CB->hasFnAttr(Attribute::WillReturn))
            return true;
          if (!CB->hasFnAttr(Attribute::NoSync))
            return true;

          return false;
        })) {
      return reportInvalidCandidate(I, MayThrowException);
    }

  // Check if I has any output/flow/anti dependences with instructions from \p
  // StartInst to \p EndInst.
  if (llvm::any_of(InstsToCheck, [&DI, &I](Instruction *CurInst) {
        auto DepResult = DI->depends(&I, CurInst, true);
        if (DepResult && (DepResult->isOutput() || DepResult->isFlow() ||
                          DepResult->isAnti()))
          return true;
        return false;
      }))
    return reportInvalidCandidate(I, HasDependences);

  return true;
}

bool llvm::isSafeToMoveBefore(BasicBlock &BB, Instruction &InsertPoint,
                              DominatorTree &DT, const PostDominatorTree *PDT,
                              DependenceInfo *DI) {
  return llvm::all_of(BB, [&](Instruction &I) {
    if (BB.getTerminator() == &I)
      return true;

    return isSafeToMoveBefore(I, InsertPoint, DT, PDT, DI,
                              /*CheckForEntireBlock=*/true);
  });
}

void llvm::moveInstructionsToTheBeginning(BasicBlock &FromBB, BasicBlock &ToBB,
                                          DominatorTree &DT,
                                          const PostDominatorTree &PDT,
                                          DependenceInfo &DI) {
  for (Instruction &I :
       llvm::make_early_inc_range(llvm::drop_begin(llvm::reverse(FromBB)))) {
    Instruction *MovePos = ToBB.getFirstNonPHIOrDbg();

    if (isSafeToMoveBefore(I, *MovePos, DT, &PDT, &DI))
      I.moveBeforePreserving(MovePos);
  }
}

void llvm::moveInstructionsToTheEnd(BasicBlock &FromBB, BasicBlock &ToBB,
                                    DominatorTree &DT,
                                    const PostDominatorTree &PDT,
                                    DependenceInfo &DI) {
  Instruction *MovePos = ToBB.getTerminator();
  while (FromBB.size() > 1) {
    Instruction &I = FromBB.front();
    if (isSafeToMoveBefore(I, *MovePos, DT, &PDT, &DI))
      I.moveBeforePreserving(MovePos);
  }
}

bool llvm::nonStrictlyPostDominate(const BasicBlock *ThisBlock,
                                   const BasicBlock *OtherBlock,
                                   const DominatorTree *DT,
                                   const PostDominatorTree *PDT) {
  assert(isControlFlowEquivalent(*ThisBlock, *OtherBlock, *DT, *PDT) &&
         "ThisBlock and OtherBlock must be CFG equivalent!");
  const BasicBlock *CommonDominator =
      DT->findNearestCommonDominator(ThisBlock, OtherBlock);
  if (CommonDominator == nullptr)
    return false;

  /// Recursively check the predecessors of \p ThisBlock up to
  /// their common dominator, and see if any of them post-dominates
  /// \p OtherBlock.
  SmallVector<const BasicBlock *, 8> WorkList;
  SmallPtrSet<const BasicBlock *, 8> Visited;
  WorkList.push_back(ThisBlock);
  while (!WorkList.empty()) {
    const BasicBlock *CurBlock = WorkList.back();
    WorkList.pop_back();
    Visited.insert(CurBlock);
    if (PDT->dominates(CurBlock, OtherBlock))
      return true;

    for (const auto *Pred : predecessors(CurBlock)) {
      if (Pred == CommonDominator || Visited.count(Pred))
        continue;
      WorkList.push_back(Pred);
    }
  }
  return false;
}

bool llvm::isReachedBefore(const Instruction *I0, const Instruction *I1,
                           const DominatorTree *DT,
                           const PostDominatorTree *PDT) {
  const BasicBlock *BB0 = I0->getParent();
  const BasicBlock *BB1 = I1->getParent();
  if (BB0 == BB1)
    return DT->dominates(I0, I1);

  return nonStrictlyPostDominate(BB1, BB0, DT, PDT);
}
