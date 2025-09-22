//==- CanonicalizeFreezeInLoops - Canonicalize freezes in a loop-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass canonicalizes freeze instructions in a loop by pushing them out to
// the preheader.
//
//   loop:
//     i = phi init, i.next
//     i.next = add nsw i, 1
//     i.next.fr = freeze i.next // push this out of this loop
//     use(i.next.fr)
//     br i1 (i.next <= N), loop, exit
//   =>
//     init.fr = freeze init
//   loop:
//     i = phi init.fr, i.next
//     i.next = add i, 1         // nsw is dropped here
//     use(i.next)
//     br i1 (i.next <= N), loop, exit
//
// Removing freezes from these chains help scalar evolution successfully analyze
// expressions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CanonicalizeFreezeInLoops.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/IVDescriptors.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Dominators.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/Utils.h"

using namespace llvm;

#define DEBUG_TYPE "canon-freeze"

namespace {

class CanonicalizeFreezeInLoops : public LoopPass {
public:
  static char ID;

  CanonicalizeFreezeInLoops();

private:
  bool runOnLoop(Loop *L, LPPassManager &LPM) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

class CanonicalizeFreezeInLoopsImpl {
  Loop *L;
  ScalarEvolution &SE;
  DominatorTree &DT;

  // Can freeze instruction be pushed into operands of I?
  // In order to do this, I should not create a poison after I's flags are
  // stripped.
  bool canHandleInst(const Instruction *I) {
    auto Opc = I->getOpcode();
    // If add/sub/mul, drop nsw/nuw flags.
    return Opc == Instruction::Add || Opc == Instruction::Sub ||
           Opc == Instruction::Mul;
  }

  void InsertFreezeAndForgetFromSCEV(Use &U);

public:
  CanonicalizeFreezeInLoopsImpl(Loop *L, ScalarEvolution &SE, DominatorTree &DT)
      : L(L), SE(SE), DT(DT) {}
  bool run();
};

} // anonymous namespace

namespace llvm {

struct FrozenIndPHIInfo {
  // A freeze instruction that uses an induction phi
  FreezeInst *FI = nullptr;
  // The induction phi, step instruction, the operand idx of StepInst which is
  // a step value
  PHINode *PHI;
  BinaryOperator *StepInst;
  unsigned StepValIdx = 0;

  FrozenIndPHIInfo(PHINode *PHI, BinaryOperator *StepInst)
      : PHI(PHI), StepInst(StepInst) {}

  bool operator==(const FrozenIndPHIInfo &Other) { return FI == Other.FI; }
};

template <> struct DenseMapInfo<FrozenIndPHIInfo> {
  static inline FrozenIndPHIInfo getEmptyKey() {
    return FrozenIndPHIInfo(DenseMapInfo<PHINode *>::getEmptyKey(),
                            DenseMapInfo<BinaryOperator *>::getEmptyKey());
  }

  static inline FrozenIndPHIInfo getTombstoneKey() {
    return FrozenIndPHIInfo(DenseMapInfo<PHINode *>::getTombstoneKey(),
                            DenseMapInfo<BinaryOperator *>::getTombstoneKey());
  }

  static unsigned getHashValue(const FrozenIndPHIInfo &Val) {
    return DenseMapInfo<FreezeInst *>::getHashValue(Val.FI);
  };

  static bool isEqual(const FrozenIndPHIInfo &LHS,
                      const FrozenIndPHIInfo &RHS) {
    return LHS.FI == RHS.FI;
  };
};

} // end namespace llvm

// Given U = (value, user), replace value with freeze(value), and let
// SCEV forget user. The inserted freeze is placed in the preheader.
void CanonicalizeFreezeInLoopsImpl::InsertFreezeAndForgetFromSCEV(Use &U) {
  auto *PH = L->getLoopPreheader();

  auto *UserI = cast<Instruction>(U.getUser());
  auto *ValueToFr = U.get();
  assert(L->contains(UserI->getParent()) &&
         "Should not process an instruction that isn't inside the loop");
  if (isGuaranteedNotToBeUndefOrPoison(ValueToFr, nullptr, UserI, &DT))
    return;

  LLVM_DEBUG(dbgs() << "canonfr: inserting freeze:\n");
  LLVM_DEBUG(dbgs() << "\tUser: " << *U.getUser() << "\n");
  LLVM_DEBUG(dbgs() << "\tOperand: " << *U.get() << "\n");

  U.set(new FreezeInst(ValueToFr, ValueToFr->getName() + ".frozen",
                       PH->getTerminator()->getIterator()));

  SE.forgetValue(UserI);
}

bool CanonicalizeFreezeInLoopsImpl::run() {
  // The loop should be in LoopSimplify form.
  if (!L->isLoopSimplifyForm())
    return false;

  SmallSetVector<FrozenIndPHIInfo, 4> Candidates;

  for (auto &PHI : L->getHeader()->phis()) {
    InductionDescriptor ID;
    if (!InductionDescriptor::isInductionPHI(&PHI, L, &SE, ID))
      continue;

    LLVM_DEBUG(dbgs() << "canonfr: PHI: " << PHI << "\n");
    FrozenIndPHIInfo Info(&PHI, ID.getInductionBinOp());
    if (!Info.StepInst || !canHandleInst(Info.StepInst)) {
      // The stepping instruction has unknown form.
      // Ignore this PHI.
      continue;
    }

    Info.StepValIdx = Info.StepInst->getOperand(0) == &PHI;
    Value *StepV = Info.StepInst->getOperand(Info.StepValIdx);
    if (auto *StepI = dyn_cast<Instruction>(StepV)) {
      if (L->contains(StepI->getParent())) {
        // The step value is inside the loop. Freezing step value will introduce
        // another freeze into the loop, so skip this PHI.
        continue;
      }
    }

    auto Visit = [&](User *U) {
      if (auto *FI = dyn_cast<FreezeInst>(U)) {
        LLVM_DEBUG(dbgs() << "canonfr: found: " << *FI << "\n");
        Info.FI = FI;
        Candidates.insert(Info);
      }
    };
    for_each(PHI.users(), Visit);
    for_each(Info.StepInst->users(), Visit);
  }

  if (Candidates.empty())
    return false;

  SmallSet<PHINode *, 8> ProcessedPHIs;
  for (const auto &Info : Candidates) {
    PHINode *PHI = Info.PHI;
    if (!ProcessedPHIs.insert(Info.PHI).second)
      continue;

    BinaryOperator *StepI = Info.StepInst;
    assert(StepI && "Step instruction should have been found");

    // Drop flags from the step instruction.
    if (!isGuaranteedNotToBeUndefOrPoison(StepI, nullptr, StepI, &DT)) {
      LLVM_DEBUG(dbgs() << "canonfr: drop flags: " << *StepI << "\n");
      StepI->dropPoisonGeneratingFlags();
      SE.forgetValue(StepI);
    }

    InsertFreezeAndForgetFromSCEV(StepI->getOperandUse(Info.StepValIdx));

    unsigned OperandIdx =
        PHI->getOperandNumForIncomingValue(PHI->getIncomingValue(0) == StepI);
    InsertFreezeAndForgetFromSCEV(PHI->getOperandUse(OperandIdx));
  }

  // Finally, remove the old freeze instructions.
  for (const auto &Item : Candidates) {
    auto *FI = Item.FI;
    LLVM_DEBUG(dbgs() << "canonfr: removing " << *FI << "\n");
    SE.forgetValue(FI);
    FI->replaceAllUsesWith(FI->getOperand(0));
    FI->eraseFromParent();
  }

  return true;
}

CanonicalizeFreezeInLoops::CanonicalizeFreezeInLoops() : LoopPass(ID) {
  initializeCanonicalizeFreezeInLoopsPass(*PassRegistry::getPassRegistry());
}

void CanonicalizeFreezeInLoops::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addPreservedID(LoopSimplifyID);
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addPreserved<LoopInfoWrapperPass>();
  AU.addRequiredID(LoopSimplifyID);
  AU.addRequired<ScalarEvolutionWrapperPass>();
  AU.addPreserved<ScalarEvolutionWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addPreserved<DominatorTreeWrapperPass>();
}

bool CanonicalizeFreezeInLoops::runOnLoop(Loop *L, LPPassManager &) {
  if (skipLoop(L))
    return false;

  auto &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  return CanonicalizeFreezeInLoopsImpl(L, SE, DT).run();
}

PreservedAnalyses
CanonicalizeFreezeInLoopsPass::run(Loop &L, LoopAnalysisManager &AM,
                                   LoopStandardAnalysisResults &AR,
                                   LPMUpdater &U) {
  if (!CanonicalizeFreezeInLoopsImpl(&L, AR.SE, AR.DT).run())
    return PreservedAnalyses::all();

  return getLoopPassPreservedAnalyses();
}

INITIALIZE_PASS_BEGIN(CanonicalizeFreezeInLoops, "canon-freeze",
                      "Canonicalize Freeze Instructions in Loops", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_END(CanonicalizeFreezeInLoops, "canon-freeze",
                    "Canonicalize Freeze Instructions in Loops", false, false)

Pass *llvm::createCanonicalizeFreezeInLoopsPass() {
  return new CanonicalizeFreezeInLoops();
}

char CanonicalizeFreezeInLoops::ID = 0;
