//===-- WebAssemblyCFGSort.cpp - CFG Sorting ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a CFG sorting pass.
///
/// This pass reorders the blocks in a function to put them into topological
/// order, ignoring loop backedges, and without any loop or exception being
/// interrupted by a block not dominated by the its header, with special care
/// to keep the order as similar as possible to the original order.
///
////===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyExceptionInfo.h"
#include "WebAssemblySortRegion.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;
using WebAssembly::SortRegion;
using WebAssembly::SortRegionInfo;

#define DEBUG_TYPE "wasm-cfg-sort"

// Option to disable EH pad first sorting. Only for testing unwind destination
// mismatches in CFGStackify.
static cl::opt<bool> WasmDisableEHPadSort(
    "wasm-disable-ehpad-sort", cl::ReallyHidden,
    cl::desc(
        "WebAssembly: Disable EH pad-first sort order. Testing purpose only."),
    cl::init(false));

namespace {

class WebAssemblyCFGSort final : public MachineFunctionPass {
  StringRef getPassName() const override { return "WebAssembly CFG Sort"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    AU.addRequired<WebAssemblyExceptionInfo>();
    AU.addPreserved<WebAssemblyExceptionInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyCFGSort() : MachineFunctionPass(ID) {}
};
} // end anonymous namespace

char WebAssemblyCFGSort::ID = 0;
INITIALIZE_PASS(WebAssemblyCFGSort, DEBUG_TYPE,
                "Reorders blocks in topological order", false, false)

FunctionPass *llvm::createWebAssemblyCFGSort() {
  return new WebAssemblyCFGSort();
}

static void maybeUpdateTerminator(MachineBasicBlock *MBB) {
#ifndef NDEBUG
  bool AnyBarrier = false;
#endif
  bool AllAnalyzable = true;
  for (const MachineInstr &Term : MBB->terminators()) {
#ifndef NDEBUG
    AnyBarrier |= Term.isBarrier();
#endif
    AllAnalyzable &= Term.isBranch() && !Term.isIndirectBranch();
  }
  assert((AnyBarrier || AllAnalyzable) &&
         "analyzeBranch needs to analyze any block with a fallthrough");

  // Find the layout successor from the original block order.
  MachineFunction *MF = MBB->getParent();
  MachineBasicBlock *OriginalSuccessor =
      unsigned(MBB->getNumber() + 1) < MF->getNumBlockIDs()
          ? MF->getBlockNumbered(MBB->getNumber() + 1)
          : nullptr;

  if (AllAnalyzable)
    MBB->updateTerminator(OriginalSuccessor);
}

namespace {
// EH pads are selected first regardless of the block comparison order.
// When only one of the BBs is an EH pad, we give a higher priority to it, to
// prevent common mismatches between possibly throwing calls and ehpads they
// unwind to, as in the example below:
//
// bb0:
//   call @foo      // If this throws, unwind to bb2
// bb1:
//   call @bar      // If this throws, unwind to bb3
// bb2 (ehpad):
//   handler_bb2
// bb3 (ehpad):
//   handler_bb3
// continuing code
//
// Because this pass tries to preserve the original BB order, this order will
// not change. But this will result in this try-catch structure in CFGStackify,
// resulting in a mismatch:
// try
//   try
//     call @foo
//     call @bar    // This should unwind to bb3, not bb2!
//   catch
//     handler_bb2
//   end
// catch
//   handler_bb3
// end
// continuing code
//
// If we give a higher priority to an EH pad whenever it is ready in this
// example, when both bb1 and bb2 are ready, we would pick up bb2 first.

/// Sort blocks by their number.
struct CompareBlockNumbers {
  bool operator()(const MachineBasicBlock *A,
                  const MachineBasicBlock *B) const {
    if (!WasmDisableEHPadSort) {
      if (A->isEHPad() && !B->isEHPad())
        return false;
      if (!A->isEHPad() && B->isEHPad())
        return true;
    }

    return A->getNumber() > B->getNumber();
  }
};
/// Sort blocks by their number in the opposite order..
struct CompareBlockNumbersBackwards {
  bool operator()(const MachineBasicBlock *A,
                  const MachineBasicBlock *B) const {
    if (!WasmDisableEHPadSort) {
      if (A->isEHPad() && !B->isEHPad())
        return false;
      if (!A->isEHPad() && B->isEHPad())
        return true;
    }

    return A->getNumber() < B->getNumber();
  }
};
/// Bookkeeping for a region to help ensure that we don't mix blocks not
/// dominated by the its header among its blocks.
struct Entry {
  const SortRegion *TheRegion;
  unsigned NumBlocksLeft;

  /// List of blocks not dominated by Loop's header that are deferred until
  /// after all of Loop's blocks have been seen.
  std::vector<MachineBasicBlock *> Deferred;

  explicit Entry(const SortRegion *R)
      : TheRegion(R), NumBlocksLeft(R->getNumBlocks()) {}
};
} // end anonymous namespace

/// Sort the blocks, taking special care to make sure that regions are not
/// interrupted by blocks not dominated by their header.
/// TODO: There are many opportunities for improving the heuristics here.
/// Explore them.
static void sortBlocks(MachineFunction &MF, const MachineLoopInfo &MLI,
                       const WebAssemblyExceptionInfo &WEI,
                       const MachineDominatorTree &MDT) {
  // Remember original layout ordering, so we can update terminators after
  // reordering to point to the original layout successor.
  MF.RenumberBlocks();

  // Prepare for a topological sort: Record the number of predecessors each
  // block has, ignoring loop backedges.
  SmallVector<unsigned, 16> NumPredsLeft(MF.getNumBlockIDs(), 0);
  for (MachineBasicBlock &MBB : MF) {
    unsigned N = MBB.pred_size();
    if (MachineLoop *L = MLI.getLoopFor(&MBB))
      if (L->getHeader() == &MBB)
        for (const MachineBasicBlock *Pred : MBB.predecessors())
          if (L->contains(Pred))
            --N;
    NumPredsLeft[MBB.getNumber()] = N;
  }

  // Topological sort the CFG, with additional constraints:
  //  - Between a region header and the last block in the region, there can be
  //    no blocks not dominated by its header.
  //  - It's desirable to preserve the original block order when possible.
  // We use two ready lists; Preferred and Ready. Preferred has recently
  // processed successors, to help preserve block sequences from the original
  // order. Ready has the remaining ready blocks. EH blocks are picked first
  // from both queues.
  PriorityQueue<MachineBasicBlock *, std::vector<MachineBasicBlock *>,
                CompareBlockNumbers>
      Preferred;
  PriorityQueue<MachineBasicBlock *, std::vector<MachineBasicBlock *>,
                CompareBlockNumbersBackwards>
      Ready;

  const auto *EHInfo = MF.getWasmEHFuncInfo();
  SortRegionInfo SRI(MLI, WEI);
  SmallVector<Entry, 4> Entries;
  for (MachineBasicBlock *MBB = &MF.front();;) {
    const SortRegion *R = SRI.getRegionFor(MBB);
    if (R) {
      // If MBB is a region header, add it to the active region list. We can't
      // put any blocks that it doesn't dominate until we see the end of the
      // region.
      if (R->getHeader() == MBB)
        Entries.push_back(Entry(R));
      // For each active region the block is in, decrement the count. If MBB is
      // the last block in an active region, take it off the list and pick up
      // any blocks deferred because the header didn't dominate them.
      for (Entry &E : Entries)
        if (E.TheRegion->contains(MBB) && --E.NumBlocksLeft == 0)
          for (auto *DeferredBlock : E.Deferred)
            Ready.push(DeferredBlock);
      while (!Entries.empty() && Entries.back().NumBlocksLeft == 0)
        Entries.pop_back();
    }
    // The main topological sort logic.
    for (MachineBasicBlock *Succ : MBB->successors()) {
      // Ignore backedges.
      if (MachineLoop *SuccL = MLI.getLoopFor(Succ))
        if (SuccL->getHeader() == Succ && SuccL->contains(MBB))
          continue;
      // Decrement the predecessor count. If it's now zero, it's ready.
      if (--NumPredsLeft[Succ->getNumber()] == 0) {
        // When we are in a SortRegion, we allow sorting of not only BBs that
        // belong to the current (innermost) region but also BBs that are
        // dominated by the current region header. But we should not do this for
        // exceptions because there can be cases in which, for example:
        // EHPad A's unwind destination (where the exception lands when it is
        // not caught by EHPad A) is EHPad B, so EHPad B does not belong to the
        // exception dominated by EHPad A. But EHPad B is dominated by EHPad A,
        // so EHPad B can be sorted within EHPad A's exception. This is
        // incorrect because we may end up delegating/rethrowing to an inner
        // scope in CFGStackify. So here we make sure those unwind destinations
        // are deferred until their unwind source's exception is sorted.
        if (EHInfo && EHInfo->hasUnwindSrcs(Succ)) {
          SmallPtrSet<MachineBasicBlock *, 4> UnwindSrcs =
              EHInfo->getUnwindSrcs(Succ);
          bool IsDeferred = false;
          for (Entry &E : Entries) {
            if (UnwindSrcs.count(E.TheRegion->getHeader())) {
              E.Deferred.push_back(Succ);
              IsDeferred = true;
              break;
            }
          }
          if (IsDeferred)
            continue;
        }
        Preferred.push(Succ);
      }
    }
    // Determine the block to follow MBB. First try to find a preferred block,
    // to preserve the original block order when possible.
    MachineBasicBlock *Next = nullptr;
    while (!Preferred.empty()) {
      Next = Preferred.top();
      Preferred.pop();
      // If X isn't dominated by the top active region header, defer it until
      // that region is done.
      if (!Entries.empty() &&
          !MDT.dominates(Entries.back().TheRegion->getHeader(), Next)) {
        Entries.back().Deferred.push_back(Next);
        Next = nullptr;
        continue;
      }
      // If Next was originally ordered before MBB, and it isn't because it was
      // loop-rotated above the header, it's not preferred.
      if (Next->getNumber() < MBB->getNumber() &&
          (WasmDisableEHPadSort || !Next->isEHPad()) &&
          (!R || !R->contains(Next) ||
           R->getHeader()->getNumber() < Next->getNumber())) {
        Ready.push(Next);
        Next = nullptr;
        continue;
      }
      break;
    }
    // If we didn't find a suitable block in the Preferred list, check the
    // general Ready list.
    if (!Next) {
      // If there are no more blocks to process, we're done.
      if (Ready.empty()) {
        maybeUpdateTerminator(MBB);
        break;
      }
      for (;;) {
        Next = Ready.top();
        Ready.pop();
        // If Next isn't dominated by the top active region header, defer it
        // until that region is done.
        if (!Entries.empty() &&
            !MDT.dominates(Entries.back().TheRegion->getHeader(), Next)) {
          Entries.back().Deferred.push_back(Next);
          continue;
        }
        break;
      }
    }
    // Move the next block into place and iterate.
    Next->moveAfter(MBB);
    maybeUpdateTerminator(MBB);
    MBB = Next;
  }
  assert(Entries.empty() && "Active sort region list not finished");
  MF.RenumberBlocks();

#ifndef NDEBUG
  SmallSetVector<const SortRegion *, 8> OnStack;

  // Insert a sentinel representing the degenerate loop that starts at the
  // function entry block and includes the entire function as a "loop" that
  // executes once.
  OnStack.insert(nullptr);

  for (auto &MBB : MF) {
    assert(MBB.getNumber() >= 0 && "Renumbered blocks should be non-negative.");
    const SortRegion *Region = SRI.getRegionFor(&MBB);

    if (Region && &MBB == Region->getHeader()) {
      // Region header.
      if (Region->isLoop()) {
        // Loop header. The loop predecessor should be sorted above, and the
        // other predecessors should be backedges below.
        for (auto *Pred : MBB.predecessors())
          assert(
              (Pred->getNumber() < MBB.getNumber() || Region->contains(Pred)) &&
              "Loop header predecessors must be loop predecessors or "
              "backedges");
      } else {
        // Exception header. All predecessors should be sorted above.
        for (auto *Pred : MBB.predecessors())
          assert(Pred->getNumber() < MBB.getNumber() &&
                 "Non-loop-header predecessors should be topologically sorted");
      }
      assert(OnStack.insert(Region) &&
             "Regions should be declared at most once.");

    } else {
      // Not a region header. All predecessors should be sorted above.
      for (auto *Pred : MBB.predecessors())
        assert(Pred->getNumber() < MBB.getNumber() &&
               "Non-loop-header predecessors should be topologically sorted");
      assert(OnStack.count(SRI.getRegionFor(&MBB)) &&
             "Blocks must be nested in their regions");
    }
    while (OnStack.size() > 1 && &MBB == SRI.getBottom(OnStack.back()))
      OnStack.pop_back();
  }
  assert(OnStack.pop_back_val() == nullptr &&
         "The function entry block shouldn't actually be a region header");
  assert(OnStack.empty() &&
         "Control flow stack pushes and pops should be balanced.");
#endif
}

bool WebAssemblyCFGSort::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** CFG Sorting **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  const auto &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const auto &WEI = getAnalysis<WebAssemblyExceptionInfo>();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  // Liveness is not tracked for VALUE_STACK physreg.
  MF.getRegInfo().invalidateLiveness();

  // Sort the blocks, with contiguous sort regions.
  sortBlocks(MF, MLI, WEI, MDT);

  return true;
}
