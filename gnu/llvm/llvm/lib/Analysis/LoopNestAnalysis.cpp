//===- LoopNestAnalysis.cpp - Loop Nest Analysis --------------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// The implementation for the loop nest analysis.
///
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/ADT/BreadthFirstIterator.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/ValueTracking.h"

using namespace llvm;

#define DEBUG_TYPE "loopnest"
#ifndef NDEBUG
static const char *VerboseDebug = DEBUG_TYPE "-verbose";
#endif

/// Determine whether the loops structure violates basic requirements for
/// perfect nesting:
///  - the inner loop should be the outer loop's only child
///  - the outer loop header should 'flow' into the inner loop preheader
///    or jump around the inner loop to the outer loop latch
///  - if the inner loop latch exits the inner loop, it should 'flow' into
///    the outer loop latch.
/// Returns true if the loop structure satisfies the basic requirements and
/// false otherwise.
static bool checkLoopsStructure(const Loop &OuterLoop, const Loop &InnerLoop,
                                ScalarEvolution &SE);

//===----------------------------------------------------------------------===//
// LoopNest implementation
//

LoopNest::LoopNest(Loop &Root, ScalarEvolution &SE)
    : MaxPerfectDepth(getMaxPerfectDepth(Root, SE)) {
  append_range(Loops, breadth_first(&Root));
}

std::unique_ptr<LoopNest> LoopNest::getLoopNest(Loop &Root,
                                                ScalarEvolution &SE) {
  return std::make_unique<LoopNest>(Root, SE);
}

static CmpInst *getOuterLoopLatchCmp(const Loop &OuterLoop) {

  const BasicBlock *Latch = OuterLoop.getLoopLatch();
  assert(Latch && "Expecting a valid loop latch");

  const BranchInst *BI = dyn_cast<BranchInst>(Latch->getTerminator());
  assert(BI && BI->isConditional() &&
         "Expecting loop latch terminator to be a branch instruction");

  CmpInst *OuterLoopLatchCmp = dyn_cast<CmpInst>(BI->getCondition());
  DEBUG_WITH_TYPE(
      VerboseDebug, if (OuterLoopLatchCmp) {
        dbgs() << "Outer loop latch compare instruction: " << *OuterLoopLatchCmp
               << "\n";
      });
  return OuterLoopLatchCmp;
}

static CmpInst *getInnerLoopGuardCmp(const Loop &InnerLoop) {

  BranchInst *InnerGuard = InnerLoop.getLoopGuardBranch();
  CmpInst *InnerLoopGuardCmp =
      (InnerGuard) ? dyn_cast<CmpInst>(InnerGuard->getCondition()) : nullptr;

  DEBUG_WITH_TYPE(
      VerboseDebug, if (InnerLoopGuardCmp) {
        dbgs() << "Inner loop guard compare instruction: " << *InnerLoopGuardCmp
               << "\n";
      });
  return InnerLoopGuardCmp;
}

static bool checkSafeInstruction(const Instruction &I,
                                 const CmpInst *InnerLoopGuardCmp,
                                 const CmpInst *OuterLoopLatchCmp,
                                 std::optional<Loop::LoopBounds> OuterLoopLB) {

  bool IsAllowed =
      isSafeToSpeculativelyExecute(&I) || isa<PHINode>(I) || isa<BranchInst>(I);
  if (!IsAllowed)
    return false;
  // The only binary instruction allowed is the outer loop step instruction,
  // the only comparison instructions allowed are the inner loop guard
  // compare instruction and the outer loop latch compare instruction.
  if ((isa<BinaryOperator>(I) && &I != &OuterLoopLB->getStepInst()) ||
      (isa<CmpInst>(I) && &I != OuterLoopLatchCmp && &I != InnerLoopGuardCmp)) {
    return false;
  }
  return true;
}

bool LoopNest::arePerfectlyNested(const Loop &OuterLoop, const Loop &InnerLoop,
                                  ScalarEvolution &SE) {
  return (analyzeLoopNestForPerfectNest(OuterLoop, InnerLoop, SE) ==
          PerfectLoopNest);
}

LoopNest::LoopNestEnum LoopNest::analyzeLoopNestForPerfectNest(
    const Loop &OuterLoop, const Loop &InnerLoop, ScalarEvolution &SE) {

  assert(!OuterLoop.isInnermost() && "Outer loop should have subloops");
  assert(!InnerLoop.isOutermost() && "Inner loop should have a parent");
  LLVM_DEBUG(dbgs() << "Checking whether loop '" << OuterLoop.getName()
                    << "' and '" << InnerLoop.getName()
                    << "' are perfectly nested.\n");

  // Determine whether the loops structure satisfies the following requirements:
  //  - the inner loop should be the outer loop's only child
  //  - the outer loop header should 'flow' into the inner loop preheader
  //    or jump around the inner loop to the outer loop latch
  //  - if the inner loop latch exits the inner loop, it should 'flow' into
  //    the outer loop latch.
  if (!checkLoopsStructure(OuterLoop, InnerLoop, SE)) {
    LLVM_DEBUG(dbgs() << "Not perfectly nested: invalid loop structure.\n");
    return InvalidLoopStructure;
  }

  // Bail out if we cannot retrieve the outer loop bounds.
  auto OuterLoopLB = OuterLoop.getBounds(SE);
  if (OuterLoopLB == std::nullopt) {
    LLVM_DEBUG(dbgs() << "Cannot compute loop bounds of OuterLoop: "
                      << OuterLoop << "\n";);
    return OuterLoopLowerBoundUnknown;
  }

  CmpInst *OuterLoopLatchCmp = getOuterLoopLatchCmp(OuterLoop);
  CmpInst *InnerLoopGuardCmp = getInnerLoopGuardCmp(InnerLoop);

  // Determine whether instructions in a basic block are one of:
  //  - the inner loop guard comparison
  //  - the outer loop latch comparison
  //  - the outer loop induction variable increment
  //  - a phi node, a cast or a branch
  auto containsOnlySafeInstructions = [&](const BasicBlock &BB) {
    return llvm::all_of(BB, [&](const Instruction &I) {
      bool IsSafeInstr = checkSafeInstruction(I, InnerLoopGuardCmp,
                                              OuterLoopLatchCmp, OuterLoopLB);
      if (IsSafeInstr) {
        DEBUG_WITH_TYPE(VerboseDebug, {
          dbgs() << "Instruction: " << I << "\nin basic block:" << BB
                 << "is unsafe.\n";
        });
      }
      return IsSafeInstr;
    });
  };

  // Check the code surrounding the inner loop for instructions that are deemed
  // unsafe.
  const BasicBlock *OuterLoopHeader = OuterLoop.getHeader();
  const BasicBlock *OuterLoopLatch = OuterLoop.getLoopLatch();
  const BasicBlock *InnerLoopPreHeader = InnerLoop.getLoopPreheader();

  if (!containsOnlySafeInstructions(*OuterLoopHeader) ||
      !containsOnlySafeInstructions(*OuterLoopLatch) ||
      (InnerLoopPreHeader != OuterLoopHeader &&
       !containsOnlySafeInstructions(*InnerLoopPreHeader)) ||
      !containsOnlySafeInstructions(*InnerLoop.getExitBlock())) {
    LLVM_DEBUG(dbgs() << "Not perfectly nested: code surrounding inner loop is "
                         "unsafe\n";);
    return ImperfectLoopNest;
  }

  LLVM_DEBUG(dbgs() << "Loop '" << OuterLoop.getName() << "' and '"
                    << InnerLoop.getName() << "' are perfectly nested.\n");

  return PerfectLoopNest;
}

LoopNest::InstrVectorTy LoopNest::getInterveningInstructions(
    const Loop &OuterLoop, const Loop &InnerLoop, ScalarEvolution &SE) {
  InstrVectorTy Instr;
  switch (analyzeLoopNestForPerfectNest(OuterLoop, InnerLoop, SE)) {
  case PerfectLoopNest:
    LLVM_DEBUG(dbgs() << "The loop Nest is Perfect, returning empty "
                         "instruction vector. \n";);
    return Instr;

  case InvalidLoopStructure:
    LLVM_DEBUG(dbgs() << "Not perfectly nested: invalid loop structure. "
                         "Instruction vector is empty.\n";);
    return Instr;

  case OuterLoopLowerBoundUnknown:
    LLVM_DEBUG(dbgs() << "Cannot compute loop bounds of OuterLoop: "
                      << OuterLoop << "\nInstruction vector is empty.\n";);
    return Instr;

  case ImperfectLoopNest:
    break;
  }

  // Identify the outer loop latch comparison instruction.
  auto OuterLoopLB = OuterLoop.getBounds(SE);

  CmpInst *OuterLoopLatchCmp = getOuterLoopLatchCmp(OuterLoop);
  CmpInst *InnerLoopGuardCmp = getInnerLoopGuardCmp(InnerLoop);

  auto GetUnsafeInstructions = [&](const BasicBlock &BB) {
    for (const Instruction &I : BB) {
      if (!checkSafeInstruction(I, InnerLoopGuardCmp, OuterLoopLatchCmp,
                                OuterLoopLB)) {
        Instr.push_back(&I);
        DEBUG_WITH_TYPE(VerboseDebug, {
          dbgs() << "Instruction: " << I << "\nin basic block:" << BB
                 << "is unsafe.\n";
        });
      }
    }
  };

  // Check the code surrounding the inner loop for instructions that are deemed
  // unsafe.
  const BasicBlock *OuterLoopHeader = OuterLoop.getHeader();
  const BasicBlock *OuterLoopLatch = OuterLoop.getLoopLatch();
  const BasicBlock *InnerLoopPreHeader = InnerLoop.getLoopPreheader();
  const BasicBlock *InnerLoopExitBlock = InnerLoop.getExitBlock();

  GetUnsafeInstructions(*OuterLoopHeader);
  GetUnsafeInstructions(*OuterLoopLatch);
  GetUnsafeInstructions(*InnerLoopExitBlock);

  if (InnerLoopPreHeader != OuterLoopHeader) {
    GetUnsafeInstructions(*InnerLoopPreHeader);
  }
  return Instr;
}

SmallVector<LoopVectorTy, 4>
LoopNest::getPerfectLoops(ScalarEvolution &SE) const {
  SmallVector<LoopVectorTy, 4> LV;
  LoopVectorTy PerfectNest;

  for (Loop *L : depth_first(const_cast<Loop *>(Loops.front()))) {
    if (PerfectNest.empty())
      PerfectNest.push_back(L);

    auto &SubLoops = L->getSubLoops();
    if (SubLoops.size() == 1 && arePerfectlyNested(*L, *SubLoops.front(), SE)) {
      PerfectNest.push_back(SubLoops.front());
    } else {
      LV.push_back(PerfectNest);
      PerfectNest.clear();
    }
  }

  return LV;
}

unsigned LoopNest::getMaxPerfectDepth(const Loop &Root, ScalarEvolution &SE) {
  LLVM_DEBUG(dbgs() << "Get maximum perfect depth of loop nest rooted by loop '"
                    << Root.getName() << "'\n");

  const Loop *CurrentLoop = &Root;
  const auto *SubLoops = &CurrentLoop->getSubLoops();
  unsigned CurrentDepth = 1;

  while (SubLoops->size() == 1) {
    const Loop *InnerLoop = SubLoops->front();
    if (!arePerfectlyNested(*CurrentLoop, *InnerLoop, SE)) {
      LLVM_DEBUG({
        dbgs() << "Not a perfect nest: loop '" << CurrentLoop->getName()
               << "' is not perfectly nested with loop '"
               << InnerLoop->getName() << "'\n";
      });
      break;
    }

    CurrentLoop = InnerLoop;
    SubLoops = &CurrentLoop->getSubLoops();
    ++CurrentDepth;
  }

  return CurrentDepth;
}

const BasicBlock &LoopNest::skipEmptyBlockUntil(const BasicBlock *From,
                                                const BasicBlock *End,
                                                bool CheckUniquePred) {
  assert(From && "Expecting valid From");
  assert(End && "Expecting valid End");

  if (From == End || !From->getUniqueSuccessor())
    return *From;

  auto IsEmpty = [](const BasicBlock *BB) {
    return (BB->size() == 1);
  };

  // Visited is used to avoid running into an infinite loop.
  SmallPtrSet<const BasicBlock *, 4> Visited;
  const BasicBlock *BB = From->getUniqueSuccessor();
  const BasicBlock *PredBB = From;
  while (BB && BB != End && IsEmpty(BB) && !Visited.count(BB) &&
         (!CheckUniquePred || BB->getUniquePredecessor())) {
    Visited.insert(BB);
    PredBB = BB;
    BB = BB->getUniqueSuccessor();
  }

  return (BB == End) ? *End : *PredBB;
}

static bool checkLoopsStructure(const Loop &OuterLoop, const Loop &InnerLoop,
                                ScalarEvolution &SE) {
  // The inner loop must be the only outer loop's child.
  if ((OuterLoop.getSubLoops().size() != 1) ||
      (InnerLoop.getParentLoop() != &OuterLoop))
    return false;

  // We expect loops in normal form which have a preheader, header, latch...
  if (!OuterLoop.isLoopSimplifyForm() || !InnerLoop.isLoopSimplifyForm())
    return false;

  const BasicBlock *OuterLoopHeader = OuterLoop.getHeader();
  const BasicBlock *OuterLoopLatch = OuterLoop.getLoopLatch();
  const BasicBlock *InnerLoopPreHeader = InnerLoop.getLoopPreheader();
  const BasicBlock *InnerLoopLatch = InnerLoop.getLoopLatch();
  const BasicBlock *InnerLoopExit = InnerLoop.getExitBlock();

  // We expect rotated loops. The inner loop should have a single exit block.
  if (OuterLoop.getExitingBlock() != OuterLoopLatch ||
      InnerLoop.getExitingBlock() != InnerLoopLatch || !InnerLoopExit)
    return false;

  // Returns whether the block `ExitBlock` contains at least one LCSSA Phi node.
  auto ContainsLCSSAPhi = [](const BasicBlock &ExitBlock) {
    return any_of(ExitBlock.phis(), [](const PHINode &PN) {
      return PN.getNumIncomingValues() == 1;
    });
  };

  // Returns whether the block `BB` qualifies for being an extra Phi block. The
  // extra Phi block is the additional block inserted after the exit block of an
  // "guarded" inner loop which contains "only" Phi nodes corresponding to the
  // LCSSA Phi nodes in the exit block.
  auto IsExtraPhiBlock = [&](const BasicBlock &BB) {
    return BB.getFirstNonPHI() == BB.getTerminator() &&
           all_of(BB.phis(), [&](const PHINode &PN) {
             return all_of(PN.blocks(), [&](const BasicBlock *IncomingBlock) {
               return IncomingBlock == InnerLoopExit ||
                      IncomingBlock == OuterLoopHeader;
             });
           });
  };

  const BasicBlock *ExtraPhiBlock = nullptr;
  // Ensure the only branch that may exist between the loops is the inner loop
  // guard.
  if (OuterLoopHeader != InnerLoopPreHeader) {
    const BasicBlock &SingleSucc =
        LoopNest::skipEmptyBlockUntil(OuterLoopHeader, InnerLoopPreHeader);

    // no conditional branch present
    if (&SingleSucc != InnerLoopPreHeader) {
      const BranchInst *BI = dyn_cast<BranchInst>(SingleSucc.getTerminator());

      if (!BI || BI != InnerLoop.getLoopGuardBranch())
        return false;

      bool InnerLoopExitContainsLCSSA = ContainsLCSSAPhi(*InnerLoopExit);

      // The successors of the inner loop guard should be the inner loop
      // preheader or the outer loop latch possibly through empty blocks.
      for (const BasicBlock *Succ : BI->successors()) {
        const BasicBlock *PotentialInnerPreHeader = Succ;
        const BasicBlock *PotentialOuterLatch = Succ;

        // Ensure the inner loop guard successor is empty before skipping
        // blocks.
        if (Succ->size() == 1) {
          PotentialInnerPreHeader =
              &LoopNest::skipEmptyBlockUntil(Succ, InnerLoopPreHeader);
          PotentialOuterLatch =
              &LoopNest::skipEmptyBlockUntil(Succ, OuterLoopLatch);
        }

        if (PotentialInnerPreHeader == InnerLoopPreHeader)
          continue;
        if (PotentialOuterLatch == OuterLoopLatch)
          continue;

        // If `InnerLoopExit` contains LCSSA Phi instructions, additional block
        // may be inserted before the `OuterLoopLatch` to which `BI` jumps. The
        // loops are still considered perfectly nested if the extra block only
        // contains Phi instructions from InnerLoopExit and OuterLoopHeader.
        if (InnerLoopExitContainsLCSSA && IsExtraPhiBlock(*Succ) &&
            Succ->getSingleSuccessor() == OuterLoopLatch) {
          // Points to the extra block so that we can reference it later in the
          // final check. We can also conclude that the inner loop is
          // guarded and there exists LCSSA Phi node in the exit block later if
          // we see a non-null `ExtraPhiBlock`.
          ExtraPhiBlock = Succ;
          continue;
        }

        DEBUG_WITH_TYPE(VerboseDebug, {
          dbgs() << "Inner loop guard successor " << Succ->getName()
                 << " doesn't lead to inner loop preheader or "
                    "outer loop latch.\n";
        });
        return false;
      }
    }
  }

  // Ensure the inner loop exit block lead to the outer loop latch possibly
  // through empty blocks.
  if ((!ExtraPhiBlock ||
       &LoopNest::skipEmptyBlockUntil(InnerLoop.getExitBlock(),
                                      ExtraPhiBlock) != ExtraPhiBlock) &&
      (&LoopNest::skipEmptyBlockUntil(InnerLoop.getExitBlock(),
                                      OuterLoopLatch) != OuterLoopLatch)) {
    DEBUG_WITH_TYPE(
        VerboseDebug,
        dbgs() << "Inner loop exit block " << *InnerLoopExit
               << " does not directly lead to the outer loop latch.\n";);
    return false;
  }

  return true;
}

AnalysisKey LoopNestAnalysis::Key;

raw_ostream &llvm::operator<<(raw_ostream &OS, const LoopNest &LN) {
  OS << "IsPerfect=";
  if (LN.getMaxPerfectDepth() == LN.getNestDepth())
    OS << "true";
  else
    OS << "false";
  OS << ", Depth=" << LN.getNestDepth();
  OS << ", OutermostLoop: " << LN.getOutermostLoop().getName();
  OS << ", Loops: ( ";
  for (const Loop *L : LN.getLoops())
    OS << L->getName() << " ";
  OS << ")";

  return OS;
}

//===----------------------------------------------------------------------===//
// LoopNestPrinterPass implementation
//

PreservedAnalyses LoopNestPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                                           LoopStandardAnalysisResults &AR,
                                           LPMUpdater &U) {
  if (auto LN = LoopNest::getLoopNest(L, AR.SE))
    OS << *LN << "\n";

  return PreservedAnalyses::all();
}
