//===-- WebAssemblyCFGStackify.cpp - CFG Stackification -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a CFG stacking pass.
///
/// This pass inserts BLOCK, LOOP, and TRY markers to mark the start of scopes,
/// since scope boundaries serve as the labels for WebAssembly's control
/// transfers.
///
/// This is sufficient to convert arbitrary CFGs into a form that works on
/// WebAssembly, provided that all loops are single-entry.
///
/// In case we use exceptions, this pass also fixes mismatches in unwind
/// destinations created during transforming CFG into wasm structured format.
///
//===----------------------------------------------------------------------===//

#include "Utils/WebAssemblyTypeUtilities.h"
#include "WebAssembly.h"
#include "WebAssemblyExceptionInfo.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySortRegion.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;
using WebAssembly::SortRegionInfo;

#define DEBUG_TYPE "wasm-cfg-stackify"

STATISTIC(NumCallUnwindMismatches, "Number of call unwind mismatches found");
STATISTIC(NumCatchUnwindMismatches, "Number of catch unwind mismatches found");

namespace {
class WebAssemblyCFGStackify final : public MachineFunctionPass {
  StringRef getPassName() const override { return "WebAssembly CFG Stackify"; }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AU.addRequired<WebAssemblyExceptionInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  // For each block whose label represents the end of a scope, record the block
  // which holds the beginning of the scope. This will allow us to quickly skip
  // over scoped regions when walking blocks.
  SmallVector<MachineBasicBlock *, 8> ScopeTops;
  void updateScopeTops(MachineBasicBlock *Begin, MachineBasicBlock *End) {
    int EndNo = End->getNumber();
    if (!ScopeTops[EndNo] || ScopeTops[EndNo]->getNumber() > Begin->getNumber())
      ScopeTops[EndNo] = Begin;
  }

  // Placing markers.
  void placeMarkers(MachineFunction &MF);
  void placeBlockMarker(MachineBasicBlock &MBB);
  void placeLoopMarker(MachineBasicBlock &MBB);
  void placeTryMarker(MachineBasicBlock &MBB);

  // Exception handling related functions
  bool fixCallUnwindMismatches(MachineFunction &MF);
  bool fixCatchUnwindMismatches(MachineFunction &MF);
  void addTryDelegate(MachineInstr *RangeBegin, MachineInstr *RangeEnd,
                      MachineBasicBlock *DelegateDest);
  void recalculateScopeTops(MachineFunction &MF);
  void removeUnnecessaryInstrs(MachineFunction &MF);

  // Wrap-up
  using EndMarkerInfo =
      std::pair<const MachineBasicBlock *, const MachineInstr *>;
  unsigned getBranchDepth(const SmallVectorImpl<EndMarkerInfo> &Stack,
                          const MachineBasicBlock *MBB);
  unsigned getDelegateDepth(const SmallVectorImpl<EndMarkerInfo> &Stack,
                            const MachineBasicBlock *MBB);
  unsigned getRethrowDepth(const SmallVectorImpl<EndMarkerInfo> &Stack,
                           const MachineBasicBlock *EHPadToRethrow);
  void rewriteDepthImmediates(MachineFunction &MF);
  void fixEndsAtEndOfFunction(MachineFunction &MF);
  void cleanupFunctionData(MachineFunction &MF);

  // For each BLOCK|LOOP|TRY, the corresponding END_(BLOCK|LOOP|TRY) or DELEGATE
  // (in case of TRY).
  DenseMap<const MachineInstr *, MachineInstr *> BeginToEnd;
  // For each END_(BLOCK|LOOP|TRY) or DELEGATE, the corresponding
  // BLOCK|LOOP|TRY.
  DenseMap<const MachineInstr *, MachineInstr *> EndToBegin;
  // <TRY marker, EH pad> map
  DenseMap<const MachineInstr *, MachineBasicBlock *> TryToEHPad;
  // <EH pad, TRY marker> map
  DenseMap<const MachineBasicBlock *, MachineInstr *> EHPadToTry;

  // We need an appendix block to place 'end_loop' or 'end_try' marker when the
  // loop / exception bottom block is the last block in a function
  MachineBasicBlock *AppendixBB = nullptr;
  MachineBasicBlock *getAppendixBlock(MachineFunction &MF) {
    if (!AppendixBB) {
      AppendixBB = MF.CreateMachineBasicBlock();
      // Give it a fake predecessor so that AsmPrinter prints its label.
      AppendixBB->addSuccessor(AppendixBB);
      MF.push_back(AppendixBB);
    }
    return AppendixBB;
  }

  // Before running rewriteDepthImmediates function, 'delegate' has a BB as its
  // destination operand. getFakeCallerBlock() returns a fake BB that will be
  // used for the operand when 'delegate' needs to rethrow to the caller. This
  // will be rewritten as an immediate value that is the number of block depths
  // + 1 in rewriteDepthImmediates, and this fake BB will be removed at the end
  // of the pass.
  MachineBasicBlock *FakeCallerBB = nullptr;
  MachineBasicBlock *getFakeCallerBlock(MachineFunction &MF) {
    if (!FakeCallerBB)
      FakeCallerBB = MF.CreateMachineBasicBlock();
    return FakeCallerBB;
  }

  // Helper functions to register / unregister scope information created by
  // marker instructions.
  void registerScope(MachineInstr *Begin, MachineInstr *End);
  void registerTryScope(MachineInstr *Begin, MachineInstr *End,
                        MachineBasicBlock *EHPad);
  void unregisterScope(MachineInstr *Begin);

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyCFGStackify() : MachineFunctionPass(ID) {}
  ~WebAssemblyCFGStackify() override { releaseMemory(); }
  void releaseMemory() override;
};
} // end anonymous namespace

char WebAssemblyCFGStackify::ID = 0;
INITIALIZE_PASS(WebAssemblyCFGStackify, DEBUG_TYPE,
                "Insert BLOCK/LOOP/TRY markers for WebAssembly scopes", false,
                false)

FunctionPass *llvm::createWebAssemblyCFGStackify() {
  return new WebAssemblyCFGStackify();
}

/// Test whether Pred has any terminators explicitly branching to MBB, as
/// opposed to falling through. Note that it's possible (eg. in unoptimized
/// code) for a branch instruction to both branch to a block and fallthrough
/// to it, so we check the actual branch operands to see if there are any
/// explicit mentions.
static bool explicitlyBranchesTo(MachineBasicBlock *Pred,
                                 MachineBasicBlock *MBB) {
  for (MachineInstr &MI : Pred->terminators())
    for (MachineOperand &MO : MI.explicit_operands())
      if (MO.isMBB() && MO.getMBB() == MBB)
        return true;
  return false;
}

// Returns an iterator to the earliest position possible within the MBB,
// satisfying the restrictions given by BeforeSet and AfterSet. BeforeSet
// contains instructions that should go before the marker, and AfterSet contains
// ones that should go after the marker. In this function, AfterSet is only
// used for validation checking.
template <typename Container>
static MachineBasicBlock::iterator
getEarliestInsertPos(MachineBasicBlock *MBB, const Container &BeforeSet,
                     const Container &AfterSet) {
  auto InsertPos = MBB->end();
  while (InsertPos != MBB->begin()) {
    if (BeforeSet.count(&*std::prev(InsertPos))) {
#ifndef NDEBUG
      // Validation check
      for (auto Pos = InsertPos, E = MBB->begin(); Pos != E; --Pos)
        assert(!AfterSet.count(&*std::prev(Pos)));
#endif
      break;
    }
    --InsertPos;
  }
  return InsertPos;
}

// Returns an iterator to the latest position possible within the MBB,
// satisfying the restrictions given by BeforeSet and AfterSet. BeforeSet
// contains instructions that should go before the marker, and AfterSet contains
// ones that should go after the marker. In this function, BeforeSet is only
// used for validation checking.
template <typename Container>
static MachineBasicBlock::iterator
getLatestInsertPos(MachineBasicBlock *MBB, const Container &BeforeSet,
                   const Container &AfterSet) {
  auto InsertPos = MBB->begin();
  while (InsertPos != MBB->end()) {
    if (AfterSet.count(&*InsertPos)) {
#ifndef NDEBUG
      // Validation check
      for (auto Pos = InsertPos, E = MBB->end(); Pos != E; ++Pos)
        assert(!BeforeSet.count(&*Pos));
#endif
      break;
    }
    ++InsertPos;
  }
  return InsertPos;
}

void WebAssemblyCFGStackify::registerScope(MachineInstr *Begin,
                                           MachineInstr *End) {
  BeginToEnd[Begin] = End;
  EndToBegin[End] = Begin;
}

// When 'End' is not an 'end_try' but 'delegate, EHPad is nullptr.
void WebAssemblyCFGStackify::registerTryScope(MachineInstr *Begin,
                                              MachineInstr *End,
                                              MachineBasicBlock *EHPad) {
  registerScope(Begin, End);
  TryToEHPad[Begin] = EHPad;
  EHPadToTry[EHPad] = Begin;
}

void WebAssemblyCFGStackify::unregisterScope(MachineInstr *Begin) {
  assert(BeginToEnd.count(Begin));
  MachineInstr *End = BeginToEnd[Begin];
  assert(EndToBegin.count(End));
  BeginToEnd.erase(Begin);
  EndToBegin.erase(End);
  MachineBasicBlock *EHPad = TryToEHPad.lookup(Begin);
  if (EHPad) {
    assert(EHPadToTry.count(EHPad));
    TryToEHPad.erase(Begin);
    EHPadToTry.erase(EHPad);
  }
}

/// Insert a BLOCK marker for branches to MBB (if needed).
// TODO Consider a more generalized way of handling block (and also loop and
// try) signatures when we implement the multi-value proposal later.
void WebAssemblyCFGStackify::placeBlockMarker(MachineBasicBlock &MBB) {
  assert(!MBB.isEHPad());
  MachineFunction &MF = *MBB.getParent();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  const auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();

  // First compute the nearest common dominator of all forward non-fallthrough
  // predecessors so that we minimize the time that the BLOCK is on the stack,
  // which reduces overall stack height.
  MachineBasicBlock *Header = nullptr;
  bool IsBranchedTo = false;
  int MBBNumber = MBB.getNumber();
  for (MachineBasicBlock *Pred : MBB.predecessors()) {
    if (Pred->getNumber() < MBBNumber) {
      Header = Header ? MDT.findNearestCommonDominator(Header, Pred) : Pred;
      if (explicitlyBranchesTo(Pred, &MBB))
        IsBranchedTo = true;
    }
  }
  if (!Header)
    return;
  if (!IsBranchedTo)
    return;

  assert(&MBB != &MF.front() && "Header blocks shouldn't have predecessors");
  MachineBasicBlock *LayoutPred = MBB.getPrevNode();

  // If the nearest common dominator is inside a more deeply nested context,
  // walk out to the nearest scope which isn't more deeply nested.
  for (MachineFunction::iterator I(LayoutPred), E(Header); I != E; --I) {
    if (MachineBasicBlock *ScopeTop = ScopeTops[I->getNumber()]) {
      if (ScopeTop->getNumber() > Header->getNumber()) {
        // Skip over an intervening scope.
        I = std::next(ScopeTop->getIterator());
      } else {
        // We found a scope level at an appropriate depth.
        Header = ScopeTop;
        break;
      }
    }
  }

  // Decide where in Header to put the BLOCK.

  // Instructions that should go before the BLOCK.
  SmallPtrSet<const MachineInstr *, 4> BeforeSet;
  // Instructions that should go after the BLOCK.
  SmallPtrSet<const MachineInstr *, 4> AfterSet;
  for (const auto &MI : *Header) {
    // If there is a previously placed LOOP marker and the bottom block of the
    // loop is above MBB, it should be after the BLOCK, because the loop is
    // nested in this BLOCK. Otherwise it should be before the BLOCK.
    if (MI.getOpcode() == WebAssembly::LOOP) {
      auto *LoopBottom = BeginToEnd[&MI]->getParent()->getPrevNode();
      if (MBB.getNumber() > LoopBottom->getNumber())
        AfterSet.insert(&MI);
#ifndef NDEBUG
      else
        BeforeSet.insert(&MI);
#endif
    }

    // If there is a previously placed BLOCK/TRY marker and its corresponding
    // END marker is before the current BLOCK's END marker, that should be
    // placed after this BLOCK. Otherwise it should be placed before this BLOCK
    // marker.
    if (MI.getOpcode() == WebAssembly::BLOCK ||
        MI.getOpcode() == WebAssembly::TRY) {
      if (BeginToEnd[&MI]->getParent()->getNumber() <= MBB.getNumber())
        AfterSet.insert(&MI);
#ifndef NDEBUG
      else
        BeforeSet.insert(&MI);
#endif
    }

#ifndef NDEBUG
    // All END_(BLOCK|LOOP|TRY) markers should be before the BLOCK.
    if (MI.getOpcode() == WebAssembly::END_BLOCK ||
        MI.getOpcode() == WebAssembly::END_LOOP ||
        MI.getOpcode() == WebAssembly::END_TRY)
      BeforeSet.insert(&MI);
#endif

    // Terminators should go after the BLOCK.
    if (MI.isTerminator())
      AfterSet.insert(&MI);
  }

  // Local expression tree should go after the BLOCK.
  for (auto I = Header->getFirstTerminator(), E = Header->begin(); I != E;
       --I) {
    if (std::prev(I)->isDebugInstr() || std::prev(I)->isPosition())
      continue;
    if (WebAssembly::isChild(*std::prev(I), MFI))
      AfterSet.insert(&*std::prev(I));
    else
      break;
  }

  // Add the BLOCK.
  WebAssembly::BlockType ReturnType = WebAssembly::BlockType::Void;
  auto InsertPos = getLatestInsertPos(Header, BeforeSet, AfterSet);
  MachineInstr *Begin =
      BuildMI(*Header, InsertPos, Header->findDebugLoc(InsertPos),
              TII.get(WebAssembly::BLOCK))
          .addImm(int64_t(ReturnType));

  // Decide where in Header to put the END_BLOCK.
  BeforeSet.clear();
  AfterSet.clear();
  for (auto &MI : MBB) {
#ifndef NDEBUG
    // END_BLOCK should precede existing LOOP and TRY markers.
    if (MI.getOpcode() == WebAssembly::LOOP ||
        MI.getOpcode() == WebAssembly::TRY)
      AfterSet.insert(&MI);
#endif

    // If there is a previously placed END_LOOP marker and the header of the
    // loop is above this block's header, the END_LOOP should be placed after
    // the BLOCK, because the loop contains this block. Otherwise the END_LOOP
    // should be placed before the BLOCK. The same for END_TRY.
    if (MI.getOpcode() == WebAssembly::END_LOOP ||
        MI.getOpcode() == WebAssembly::END_TRY) {
      if (EndToBegin[&MI]->getParent()->getNumber() >= Header->getNumber())
        BeforeSet.insert(&MI);
#ifndef NDEBUG
      else
        AfterSet.insert(&MI);
#endif
    }
  }

  // Mark the end of the block.
  InsertPos = getEarliestInsertPos(&MBB, BeforeSet, AfterSet);
  MachineInstr *End = BuildMI(MBB, InsertPos, MBB.findPrevDebugLoc(InsertPos),
                              TII.get(WebAssembly::END_BLOCK));
  registerScope(Begin, End);

  // Track the farthest-spanning scope that ends at this point.
  updateScopeTops(Header, &MBB);
}

/// Insert a LOOP marker for a loop starting at MBB (if it's a loop header).
void WebAssemblyCFGStackify::placeLoopMarker(MachineBasicBlock &MBB) {
  MachineFunction &MF = *MBB.getParent();
  const auto &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const auto &WEI = getAnalysis<WebAssemblyExceptionInfo>();
  SortRegionInfo SRI(MLI, WEI);
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  MachineLoop *Loop = MLI.getLoopFor(&MBB);
  if (!Loop || Loop->getHeader() != &MBB)
    return;

  // The operand of a LOOP is the first block after the loop. If the loop is the
  // bottom of the function, insert a dummy block at the end.
  MachineBasicBlock *Bottom = SRI.getBottom(Loop);
  auto Iter = std::next(Bottom->getIterator());
  if (Iter == MF.end()) {
    getAppendixBlock(MF);
    Iter = std::next(Bottom->getIterator());
  }
  MachineBasicBlock *AfterLoop = &*Iter;

  // Decide where in Header to put the LOOP.
  SmallPtrSet<const MachineInstr *, 4> BeforeSet;
  SmallPtrSet<const MachineInstr *, 4> AfterSet;
  for (const auto &MI : MBB) {
    // LOOP marker should be after any existing loop that ends here. Otherwise
    // we assume the instruction belongs to the loop.
    if (MI.getOpcode() == WebAssembly::END_LOOP)
      BeforeSet.insert(&MI);
#ifndef NDEBUG
    else
      AfterSet.insert(&MI);
#endif
  }

  // Mark the beginning of the loop.
  auto InsertPos = getEarliestInsertPos(&MBB, BeforeSet, AfterSet);
  MachineInstr *Begin = BuildMI(MBB, InsertPos, MBB.findDebugLoc(InsertPos),
                                TII.get(WebAssembly::LOOP))
                            .addImm(int64_t(WebAssembly::BlockType::Void));

  // Decide where in Header to put the END_LOOP.
  BeforeSet.clear();
  AfterSet.clear();
#ifndef NDEBUG
  for (const auto &MI : MBB)
    // Existing END_LOOP markers belong to parent loops of this loop
    if (MI.getOpcode() == WebAssembly::END_LOOP)
      AfterSet.insert(&MI);
#endif

  // Mark the end of the loop (using arbitrary debug location that branched to
  // the loop end as its location).
  InsertPos = getEarliestInsertPos(AfterLoop, BeforeSet, AfterSet);
  DebugLoc EndDL = AfterLoop->pred_empty()
                       ? DebugLoc()
                       : (*AfterLoop->pred_rbegin())->findBranchDebugLoc();
  MachineInstr *End =
      BuildMI(*AfterLoop, InsertPos, EndDL, TII.get(WebAssembly::END_LOOP));
  registerScope(Begin, End);

  assert((!ScopeTops[AfterLoop->getNumber()] ||
          ScopeTops[AfterLoop->getNumber()]->getNumber() < MBB.getNumber()) &&
         "With block sorting the outermost loop for a block should be first.");
  updateScopeTops(&MBB, AfterLoop);
}

void WebAssemblyCFGStackify::placeTryMarker(MachineBasicBlock &MBB) {
  assert(MBB.isEHPad());
  MachineFunction &MF = *MBB.getParent();
  auto &MDT = getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  const auto &MLI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  const auto &WEI = getAnalysis<WebAssemblyExceptionInfo>();
  SortRegionInfo SRI(MLI, WEI);
  const auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();

  // Compute the nearest common dominator of all unwind predecessors
  MachineBasicBlock *Header = nullptr;
  int MBBNumber = MBB.getNumber();
  for (auto *Pred : MBB.predecessors()) {
    if (Pred->getNumber() < MBBNumber) {
      Header = Header ? MDT.findNearestCommonDominator(Header, Pred) : Pred;
      assert(!explicitlyBranchesTo(Pred, &MBB) &&
             "Explicit branch to an EH pad!");
    }
  }
  if (!Header)
    return;

  // If this try is at the bottom of the function, insert a dummy block at the
  // end.
  WebAssemblyException *WE = WEI.getExceptionFor(&MBB);
  assert(WE);
  MachineBasicBlock *Bottom = SRI.getBottom(WE);

  auto Iter = std::next(Bottom->getIterator());
  if (Iter == MF.end()) {
    getAppendixBlock(MF);
    Iter = std::next(Bottom->getIterator());
  }
  MachineBasicBlock *Cont = &*Iter;

  assert(Cont != &MF.front());
  MachineBasicBlock *LayoutPred = Cont->getPrevNode();

  // If the nearest common dominator is inside a more deeply nested context,
  // walk out to the nearest scope which isn't more deeply nested.
  for (MachineFunction::iterator I(LayoutPred), E(Header); I != E; --I) {
    if (MachineBasicBlock *ScopeTop = ScopeTops[I->getNumber()]) {
      if (ScopeTop->getNumber() > Header->getNumber()) {
        // Skip over an intervening scope.
        I = std::next(ScopeTop->getIterator());
      } else {
        // We found a scope level at an appropriate depth.
        Header = ScopeTop;
        break;
      }
    }
  }

  // Decide where in Header to put the TRY.

  // Instructions that should go before the TRY.
  SmallPtrSet<const MachineInstr *, 4> BeforeSet;
  // Instructions that should go after the TRY.
  SmallPtrSet<const MachineInstr *, 4> AfterSet;
  for (const auto &MI : *Header) {
    // If there is a previously placed LOOP marker and the bottom block of the
    // loop is above MBB, it should be after the TRY, because the loop is nested
    // in this TRY. Otherwise it should be before the TRY.
    if (MI.getOpcode() == WebAssembly::LOOP) {
      auto *LoopBottom = BeginToEnd[&MI]->getParent()->getPrevNode();
      if (MBB.getNumber() > LoopBottom->getNumber())
        AfterSet.insert(&MI);
#ifndef NDEBUG
      else
        BeforeSet.insert(&MI);
#endif
    }

    // All previously inserted BLOCK/TRY markers should be after the TRY because
    // they are all nested trys.
    if (MI.getOpcode() == WebAssembly::BLOCK ||
        MI.getOpcode() == WebAssembly::TRY)
      AfterSet.insert(&MI);

#ifndef NDEBUG
    // All END_(BLOCK/LOOP/TRY) markers should be before the TRY.
    if (MI.getOpcode() == WebAssembly::END_BLOCK ||
        MI.getOpcode() == WebAssembly::END_LOOP ||
        MI.getOpcode() == WebAssembly::END_TRY)
      BeforeSet.insert(&MI);
#endif

    // Terminators should go after the TRY.
    if (MI.isTerminator())
      AfterSet.insert(&MI);
  }

  // If Header unwinds to MBB (= Header contains 'invoke'), the try block should
  // contain the call within it. So the call should go after the TRY. The
  // exception is when the header's terminator is a rethrow instruction, in
  // which case that instruction, not a call instruction before it, is gonna
  // throw.
  MachineInstr *ThrowingCall = nullptr;
  if (MBB.isPredecessor(Header)) {
    auto TermPos = Header->getFirstTerminator();
    if (TermPos == Header->end() ||
        TermPos->getOpcode() != WebAssembly::RETHROW) {
      for (auto &MI : reverse(*Header)) {
        if (MI.isCall()) {
          AfterSet.insert(&MI);
          ThrowingCall = &MI;
          // Possibly throwing calls are usually wrapped by EH_LABEL
          // instructions. We don't want to split them and the call.
          if (MI.getIterator() != Header->begin() &&
              std::prev(MI.getIterator())->isEHLabel()) {
            AfterSet.insert(&*std::prev(MI.getIterator()));
            ThrowingCall = &*std::prev(MI.getIterator());
          }
          break;
        }
      }
    }
  }

  // Local expression tree should go after the TRY.
  // For BLOCK placement, we start the search from the previous instruction of a
  // BB's terminator, but in TRY's case, we should start from the previous
  // instruction of a call that can throw, or a EH_LABEL that precedes the call,
  // because the return values of the call's previous instructions can be
  // stackified and consumed by the throwing call.
  auto SearchStartPt = ThrowingCall ? MachineBasicBlock::iterator(ThrowingCall)
                                    : Header->getFirstTerminator();
  for (auto I = SearchStartPt, E = Header->begin(); I != E; --I) {
    if (std::prev(I)->isDebugInstr() || std::prev(I)->isPosition())
      continue;
    if (WebAssembly::isChild(*std::prev(I), MFI))
      AfterSet.insert(&*std::prev(I));
    else
      break;
  }

  // Add the TRY.
  auto InsertPos = getLatestInsertPos(Header, BeforeSet, AfterSet);
  MachineInstr *Begin =
      BuildMI(*Header, InsertPos, Header->findDebugLoc(InsertPos),
              TII.get(WebAssembly::TRY))
          .addImm(int64_t(WebAssembly::BlockType::Void));

  // Decide where in Header to put the END_TRY.
  BeforeSet.clear();
  AfterSet.clear();
  for (const auto &MI : *Cont) {
#ifndef NDEBUG
    // END_TRY should precede existing LOOP and BLOCK markers.
    if (MI.getOpcode() == WebAssembly::LOOP ||
        MI.getOpcode() == WebAssembly::BLOCK)
      AfterSet.insert(&MI);

    // All END_TRY markers placed earlier belong to exceptions that contains
    // this one.
    if (MI.getOpcode() == WebAssembly::END_TRY)
      AfterSet.insert(&MI);
#endif

    // If there is a previously placed END_LOOP marker and its header is after
    // where TRY marker is, this loop is contained within the 'catch' part, so
    // the END_TRY marker should go after that. Otherwise, the whole try-catch
    // is contained within this loop, so the END_TRY should go before that.
    if (MI.getOpcode() == WebAssembly::END_LOOP) {
      // For a LOOP to be after TRY, LOOP's BB should be after TRY's BB; if they
      // are in the same BB, LOOP is always before TRY.
      if (EndToBegin[&MI]->getParent()->getNumber() > Header->getNumber())
        BeforeSet.insert(&MI);
#ifndef NDEBUG
      else
        AfterSet.insert(&MI);
#endif
    }

    // It is not possible for an END_BLOCK to be already in this block.
  }

  // Mark the end of the TRY.
  InsertPos = getEarliestInsertPos(Cont, BeforeSet, AfterSet);
  MachineInstr *End =
      BuildMI(*Cont, InsertPos, Bottom->findBranchDebugLoc(),
              TII.get(WebAssembly::END_TRY));
  registerTryScope(Begin, End, &MBB);

  // Track the farthest-spanning scope that ends at this point. We create two
  // mappings: (BB with 'end_try' -> BB with 'try') and (BB with 'catch' -> BB
  // with 'try'). We need to create 'catch' -> 'try' mapping here too because
  // markers should not span across 'catch'. For example, this should not
  // happen:
  //
  // try
  //   block     --|  (X)
  // catch         |
  //   end_block --|
  // end_try
  for (auto *End : {&MBB, Cont})
    updateScopeTops(Header, End);
}

void WebAssemblyCFGStackify::removeUnnecessaryInstrs(MachineFunction &MF) {
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  // When there is an unconditional branch right before a catch instruction and
  // it branches to the end of end_try marker, we don't need the branch, because
  // if there is no exception, the control flow transfers to that point anyway.
  // bb0:
  //   try
  //     ...
  //     br bb2      <- Not necessary
  // bb1 (ehpad):
  //   catch
  //     ...
  // bb2:            <- Continuation BB
  //   end
  //
  // A more involved case: When the BB where 'end' is located is an another EH
  // pad, the Cont (= continuation) BB is that EH pad's 'end' BB. For example,
  // bb0:
  //   try
  //     try
  //       ...
  //       br bb3      <- Not necessary
  // bb1 (ehpad):
  //     catch
  // bb2 (ehpad):
  //     end
  //   catch
  //     ...
  // bb3:            <- Continuation BB
  //   end
  //
  // When the EH pad at hand is bb1, its matching end_try is in bb2. But it is
  // another EH pad, so bb0's continuation BB becomes bb3. So 'br bb3' in the
  // code can be deleted. This is why we run 'while' until 'Cont' is not an EH
  // pad.
  for (auto &MBB : MF) {
    if (!MBB.isEHPad())
      continue;

    MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
    SmallVector<MachineOperand, 4> Cond;
    MachineBasicBlock *EHPadLayoutPred = MBB.getPrevNode();

    MachineBasicBlock *Cont = &MBB;
    while (Cont->isEHPad()) {
      MachineInstr *Try = EHPadToTry[Cont];
      MachineInstr *EndTry = BeginToEnd[Try];
      // We started from an EH pad, so the end marker cannot be a delegate
      assert(EndTry->getOpcode() != WebAssembly::DELEGATE);
      Cont = EndTry->getParent();
    }

    bool Analyzable = !TII.analyzeBranch(*EHPadLayoutPred, TBB, FBB, Cond);
    // This condition means either
    // 1. This BB ends with a single unconditional branch whose destinaion is
    //    Cont.
    // 2. This BB ends with a conditional branch followed by an unconditional
    //    branch, and the unconditional branch's destination is Cont.
    // In both cases, we want to remove the last (= unconditional) branch.
    if (Analyzable && ((Cond.empty() && TBB && TBB == Cont) ||
                       (!Cond.empty() && FBB && FBB == Cont))) {
      bool ErasedUncondBr = false;
      (void)ErasedUncondBr;
      for (auto I = EHPadLayoutPred->end(), E = EHPadLayoutPred->begin();
           I != E; --I) {
        auto PrevI = std::prev(I);
        if (PrevI->isTerminator()) {
          assert(PrevI->getOpcode() == WebAssembly::BR);
          PrevI->eraseFromParent();
          ErasedUncondBr = true;
          break;
        }
      }
      assert(ErasedUncondBr && "Unconditional branch not erased!");
    }
  }

  // When there are block / end_block markers that overlap with try / end_try
  // markers, and the block and try markers' return types are the same, the
  // block /end_block markers are not necessary, because try / end_try markers
  // also can serve as boundaries for branches.
  // block         <- Not necessary
  //   try
  //     ...
  //   catch
  //     ...
  //   end
  // end           <- Not necessary
  SmallVector<MachineInstr *, 32> ToDelete;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.getOpcode() != WebAssembly::TRY)
        continue;
      MachineInstr *Try = &MI, *EndTry = BeginToEnd[Try];
      if (EndTry->getOpcode() == WebAssembly::DELEGATE)
        continue;

      MachineBasicBlock *TryBB = Try->getParent();
      MachineBasicBlock *Cont = EndTry->getParent();
      int64_t RetType = Try->getOperand(0).getImm();
      for (auto B = Try->getIterator(), E = std::next(EndTry->getIterator());
           B != TryBB->begin() && E != Cont->end() &&
           std::prev(B)->getOpcode() == WebAssembly::BLOCK &&
           E->getOpcode() == WebAssembly::END_BLOCK &&
           std::prev(B)->getOperand(0).getImm() == RetType;
           --B, ++E) {
        ToDelete.push_back(&*std::prev(B));
        ToDelete.push_back(&*E);
      }
    }
  }
  for (auto *MI : ToDelete) {
    if (MI->getOpcode() == WebAssembly::BLOCK)
      unregisterScope(MI);
    MI->eraseFromParent();
  }
}

// When MBB is split into MBB and Split, we should unstackify defs in MBB that
// have their uses in Split.
static void unstackifyVRegsUsedInSplitBB(MachineBasicBlock &MBB,
                                         MachineBasicBlock &Split) {
  MachineFunction &MF = *MBB.getParent();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  auto &MRI = MF.getRegInfo();

  for (auto &MI : Split) {
    for (auto &MO : MI.explicit_uses()) {
      if (!MO.isReg() || MO.getReg().isPhysical())
        continue;
      if (MachineInstr *Def = MRI.getUniqueVRegDef(MO.getReg()))
        if (Def->getParent() == &MBB)
          MFI.unstackifyVReg(MO.getReg());
    }
  }

  // In RegStackify, when a register definition is used multiple times,
  //    Reg = INST ...
  //    INST ..., Reg, ...
  //    INST ..., Reg, ...
  //    INST ..., Reg, ...
  //
  // we introduce a TEE, which has the following form:
  //    DefReg = INST ...
  //    TeeReg, Reg = TEE_... DefReg
  //    INST ..., TeeReg, ...
  //    INST ..., Reg, ...
  //    INST ..., Reg, ...
  // with DefReg and TeeReg stackified but Reg not stackified.
  //
  // But the invariant that TeeReg should be stackified can be violated while we
  // unstackify registers in the split BB above. In this case, we convert TEEs
  // into two COPYs. This COPY will be eventually eliminated in ExplicitLocals.
  //    DefReg = INST ...
  //    TeeReg = COPY DefReg
  //    Reg = COPY DefReg
  //    INST ..., TeeReg, ...
  //    INST ..., Reg, ...
  //    INST ..., Reg, ...
  for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
    if (!WebAssembly::isTee(MI.getOpcode()))
      continue;
    Register TeeReg = MI.getOperand(0).getReg();
    Register Reg = MI.getOperand(1).getReg();
    Register DefReg = MI.getOperand(2).getReg();
    if (!MFI.isVRegStackified(TeeReg)) {
      // Now we are not using TEE anymore, so unstackify DefReg too
      MFI.unstackifyVReg(DefReg);
      unsigned CopyOpc =
          WebAssembly::getCopyOpcodeForRegClass(MRI.getRegClass(DefReg));
      BuildMI(MBB, &MI, MI.getDebugLoc(), TII.get(CopyOpc), TeeReg)
          .addReg(DefReg);
      BuildMI(MBB, &MI, MI.getDebugLoc(), TII.get(CopyOpc), Reg).addReg(DefReg);
      MI.eraseFromParent();
    }
  }
}

// Wrap the given range of instruction with try-delegate. RangeBegin and
// RangeEnd are inclusive.
void WebAssemblyCFGStackify::addTryDelegate(MachineInstr *RangeBegin,
                                            MachineInstr *RangeEnd,
                                            MachineBasicBlock *DelegateDest) {
  auto *BeginBB = RangeBegin->getParent();
  auto *EndBB = RangeEnd->getParent();
  MachineFunction &MF = *BeginBB->getParent();
  const auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  // Local expression tree before the first call of this range should go
  // after the nested TRY.
  SmallPtrSet<const MachineInstr *, 4> AfterSet;
  AfterSet.insert(RangeBegin);
  for (auto I = MachineBasicBlock::iterator(RangeBegin), E = BeginBB->begin();
       I != E; --I) {
    if (std::prev(I)->isDebugInstr() || std::prev(I)->isPosition())
      continue;
    if (WebAssembly::isChild(*std::prev(I), MFI))
      AfterSet.insert(&*std::prev(I));
    else
      break;
  }

  // Create the nested try instruction.
  auto TryPos = getLatestInsertPos(
      BeginBB, SmallPtrSet<const MachineInstr *, 4>(), AfterSet);
  MachineInstr *Try = BuildMI(*BeginBB, TryPos, RangeBegin->getDebugLoc(),
                              TII.get(WebAssembly::TRY))
                          .addImm(int64_t(WebAssembly::BlockType::Void));

  // Create a BB to insert the 'delegate' instruction.
  MachineBasicBlock *DelegateBB = MF.CreateMachineBasicBlock();
  // If the destination of 'delegate' is not the caller, adds the destination to
  // the BB's successors.
  if (DelegateDest != FakeCallerBB)
    DelegateBB->addSuccessor(DelegateDest);

  auto SplitPos = std::next(RangeEnd->getIterator());
  if (SplitPos == EndBB->end()) {
    // If the range's end instruction is at the end of the BB, insert the new
    // delegate BB after the current BB.
    MF.insert(std::next(EndBB->getIterator()), DelegateBB);
    EndBB->addSuccessor(DelegateBB);

  } else {
    // When the split pos is in the middle of a BB, we split the BB into two and
    // put the 'delegate' BB in between. We normally create a split BB and make
    // it a successor of the original BB (PostSplit == true), but in case the BB
    // is an EH pad and the split pos is before 'catch', we should preserve the
    // BB's property, including that it is an EH pad, in the later part of the
    // BB, where 'catch' is. In this case we set PostSplit to false.
    bool PostSplit = true;
    if (EndBB->isEHPad()) {
      for (auto I = MachineBasicBlock::iterator(SplitPos), E = EndBB->end();
           I != E; ++I) {
        if (WebAssembly::isCatch(I->getOpcode())) {
          PostSplit = false;
          break;
        }
      }
    }

    MachineBasicBlock *PreBB = nullptr, *PostBB = nullptr;
    if (PostSplit) {
      // If the range's end instruction is in the middle of the BB, we split the
      // BB into two and insert the delegate BB in between.
      // - Before:
      // bb:
      //   range_end
      //   other_insts
      //
      // - After:
      // pre_bb: (previous 'bb')
      //   range_end
      // delegate_bb: (new)
      //   delegate
      // post_bb: (new)
      //   other_insts
      PreBB = EndBB;
      PostBB = MF.CreateMachineBasicBlock();
      MF.insert(std::next(PreBB->getIterator()), PostBB);
      MF.insert(std::next(PreBB->getIterator()), DelegateBB);
      PostBB->splice(PostBB->end(), PreBB, SplitPos, PreBB->end());
      PostBB->transferSuccessors(PreBB);
    } else {
      // - Before:
      // ehpad:
      //   range_end
      //   catch
      //   ...
      //
      // - After:
      // pre_bb: (new)
      //   range_end
      // delegate_bb: (new)
      //   delegate
      // post_bb: (previous 'ehpad')
      //   catch
      //   ...
      assert(EndBB->isEHPad());
      PreBB = MF.CreateMachineBasicBlock();
      PostBB = EndBB;
      MF.insert(PostBB->getIterator(), PreBB);
      MF.insert(PostBB->getIterator(), DelegateBB);
      PreBB->splice(PreBB->end(), PostBB, PostBB->begin(), SplitPos);
      // We don't need to transfer predecessors of the EH pad to 'PreBB',
      // because an EH pad's predecessors are all through unwind edges and they
      // should still unwind to the EH pad, not PreBB.
    }
    unstackifyVRegsUsedInSplitBB(*PreBB, *PostBB);
    PreBB->addSuccessor(DelegateBB);
    PreBB->addSuccessor(PostBB);
  }

  // Add 'delegate' instruction in the delegate BB created above.
  MachineInstr *Delegate = BuildMI(DelegateBB, RangeEnd->getDebugLoc(),
                                   TII.get(WebAssembly::DELEGATE))
                               .addMBB(DelegateDest);
  registerTryScope(Try, Delegate, nullptr);
}

bool WebAssemblyCFGStackify::fixCallUnwindMismatches(MachineFunction &MF) {
  // Linearizing the control flow by placing TRY / END_TRY markers can create
  // mismatches in unwind destinations for throwing instructions, such as calls.
  //
  // We use the 'delegate' instruction to fix the unwind mismatches. 'delegate'
  // instruction delegates an exception to an outer 'catch'. It can target not
  // only 'catch' but all block-like structures including another 'delegate',
  // but with slightly different semantics than branches. When it targets a
  // 'catch', it will delegate the exception to that catch. It is being
  // discussed how to define the semantics when 'delegate''s target is a non-try
  // block: it will either be a validation failure or it will target the next
  // outer try-catch. But anyway our LLVM backend currently does not generate
  // such code. The example below illustrates where the 'delegate' instruction
  // in the middle will delegate the exception to, depending on the value of N.
  // try
  //   try
  //     block
  //       try
  //         try
  //           call @foo
  //         delegate N    ;; Where will this delegate to?
  //       catch           ;; N == 0
  //       end
  //     end               ;; N == 1 (invalid; will not be generated)
  //   delegate            ;; N == 2
  // catch                 ;; N == 3
  // end
  //                       ;; N == 4 (to caller)

  // 1. When an instruction may throw, but the EH pad it will unwind to can be
  //    different from the original CFG.
  //
  // Example: we have the following CFG:
  // bb0:
  //   call @foo    ; if it throws, unwind to bb2
  // bb1:
  //   call @bar    ; if it throws, unwind to bb3
  // bb2 (ehpad):
  //   catch
  //   ...
  // bb3 (ehpad)
  //   catch
  //   ...
  //
  // And the CFG is sorted in this order. Then after placing TRY markers, it
  // will look like: (BB markers are omitted)
  // try
  //   try
  //     call @foo
  //     call @bar   ;; if it throws, unwind to bb3
  //   catch         ;; ehpad (bb2)
  //     ...
  //   end_try
  // catch           ;; ehpad (bb3)
  //   ...
  // end_try
  //
  // Now if bar() throws, it is going to end up ip in bb2, not bb3, where it
  // is supposed to end up. We solve this problem by wrapping the mismatching
  // call with an inner try-delegate that rethrows the exception to the right
  // 'catch'.
  //
  // try
  //   try
  //     call @foo
  //     try               ;; (new)
  //       call @bar
  //     delegate 1 (bb3)  ;; (new)
  //   catch               ;; ehpad (bb2)
  //     ...
  //   end_try
  // catch                 ;; ehpad (bb3)
  //   ...
  // end_try
  //
  // ---
  // 2. The same as 1, but in this case an instruction unwinds to a caller
  //    function and not another EH pad.
  //
  // Example: we have the following CFG:
  // bb0:
  //   call @foo       ; if it throws, unwind to bb2
  // bb1:
  //   call @bar       ; if it throws, unwind to caller
  // bb2 (ehpad):
  //   catch
  //   ...
  //
  // And the CFG is sorted in this order. Then after placing TRY markers, it
  // will look like:
  // try
  //   call @foo
  //   call @bar     ;; if it throws, unwind to caller
  // catch           ;; ehpad (bb2)
  //   ...
  // end_try
  //
  // Now if bar() throws, it is going to end up ip in bb2, when it is supposed
  // throw up to the caller. We solve this problem in the same way, but in this
  // case 'delegate's immediate argument is the number of block depths + 1,
  // which means it rethrows to the caller.
  // try
  //   call @foo
  //   try                  ;; (new)
  //     call @bar
  //   delegate 1 (caller)  ;; (new)
  // catch                  ;; ehpad (bb2)
  //   ...
  // end_try
  //
  // Before rewriteDepthImmediates, delegate's argument is a BB. In case of the
  // caller, it will take a fake BB generated by getFakeCallerBlock(), which
  // will be converted to a correct immediate argument later.
  //
  // In case there are multiple calls in a BB that may throw to the caller, they
  // can be wrapped together in one nested try-delegate scope. (In 1, this
  // couldn't happen, because may-throwing instruction there had an unwind
  // destination, i.e., it was an invoke before, and there could be only one
  // invoke within a BB.)

  SmallVector<const MachineBasicBlock *, 8> EHPadStack;
  // Range of intructions to be wrapped in a new nested try/catch. A range
  // exists in a single BB and does not span multiple BBs.
  using TryRange = std::pair<MachineInstr *, MachineInstr *>;
  // In original CFG, <unwind destination BB, a vector of try ranges>
  DenseMap<MachineBasicBlock *, SmallVector<TryRange, 4>> UnwindDestToTryRanges;

  // Gather possibly throwing calls (i.e., previously invokes) whose current
  // unwind destination is not the same as the original CFG. (Case 1)

  for (auto &MBB : reverse(MF)) {
    bool SeenThrowableInstInBB = false;
    for (auto &MI : reverse(MBB)) {
      if (MI.getOpcode() == WebAssembly::TRY)
        EHPadStack.pop_back();
      else if (WebAssembly::isCatch(MI.getOpcode()))
        EHPadStack.push_back(MI.getParent());

      // In this loop we only gather calls that have an EH pad to unwind. So
      // there will be at most 1 such call (= invoke) in a BB, so after we've
      // seen one, we can skip the rest of BB. Also if MBB has no EH pad
      // successor or MI does not throw, this is not an invoke.
      if (SeenThrowableInstInBB || !MBB.hasEHPadSuccessor() ||
          !WebAssembly::mayThrow(MI))
        continue;
      SeenThrowableInstInBB = true;

      // If the EH pad on the stack top is where this instruction should unwind
      // next, we're good.
      MachineBasicBlock *UnwindDest = getFakeCallerBlock(MF);
      for (auto *Succ : MBB.successors()) {
        // Even though semantically a BB can have multiple successors in case an
        // exception is not caught by a catchpad, in our backend implementation
        // it is guaranteed that a BB can have at most one EH pad successor. For
        // details, refer to comments in findWasmUnwindDestinations function in
        // SelectionDAGBuilder.cpp.
        if (Succ->isEHPad()) {
          UnwindDest = Succ;
          break;
        }
      }
      if (EHPadStack.back() == UnwindDest)
        continue;

      // Include EH_LABELs in the range before and afer the invoke
      MachineInstr *RangeBegin = &MI, *RangeEnd = &MI;
      if (RangeBegin->getIterator() != MBB.begin() &&
          std::prev(RangeBegin->getIterator())->isEHLabel())
        RangeBegin = &*std::prev(RangeBegin->getIterator());
      if (std::next(RangeEnd->getIterator()) != MBB.end() &&
          std::next(RangeEnd->getIterator())->isEHLabel())
        RangeEnd = &*std::next(RangeEnd->getIterator());

      // If not, record the range.
      UnwindDestToTryRanges[UnwindDest].push_back(
          TryRange(RangeBegin, RangeEnd));
      LLVM_DEBUG(dbgs() << "- Call unwind mismatch: MBB = " << MBB.getName()
                        << "\nCall = " << MI
                        << "\nOriginal dest = " << UnwindDest->getName()
                        << "  Current dest = " << EHPadStack.back()->getName()
                        << "\n\n");
    }
  }

  assert(EHPadStack.empty());

  // Gather possibly throwing calls that are supposed to unwind up to the caller
  // if they throw, but currently unwind to an incorrect destination. Unlike the
  // loop above, there can be multiple calls within a BB that unwind to the
  // caller, which we should group together in a range. (Case 2)

  MachineInstr *RangeBegin = nullptr, *RangeEnd = nullptr; // inclusive

  // Record the range.
  auto RecordCallerMismatchRange = [&](const MachineBasicBlock *CurrentDest) {
    UnwindDestToTryRanges[getFakeCallerBlock(MF)].push_back(
        TryRange(RangeBegin, RangeEnd));
    LLVM_DEBUG(dbgs() << "- Call unwind mismatch: MBB = "
                      << RangeBegin->getParent()->getName()
                      << "\nRange begin = " << *RangeBegin
                      << "Range end = " << *RangeEnd
                      << "\nOriginal dest = caller  Current dest = "
                      << CurrentDest->getName() << "\n\n");
    RangeBegin = RangeEnd = nullptr; // Reset range pointers
  };

  for (auto &MBB : reverse(MF)) {
    bool SeenThrowableInstInBB = false;
    for (auto &MI : reverse(MBB)) {
      bool MayThrow = WebAssembly::mayThrow(MI);

      // If MBB has an EH pad successor and this is the last instruction that
      // may throw, this instruction unwinds to the EH pad and not to the
      // caller.
      if (MBB.hasEHPadSuccessor() && MayThrow && !SeenThrowableInstInBB)
        SeenThrowableInstInBB = true;

      // We wrap up the current range when we see a marker even if we haven't
      // finished a BB.
      else if (RangeEnd && WebAssembly::isMarker(MI.getOpcode()))
        RecordCallerMismatchRange(EHPadStack.back());

      // If EHPadStack is empty, that means it correctly unwinds to the caller
      // if it throws, so we're good. If MI does not throw, we're good too.
      else if (EHPadStack.empty() || !MayThrow) {
      }

      // We found an instruction that unwinds to the caller but currently has an
      // incorrect unwind destination. Create a new range or increment the
      // currently existing range.
      else {
        if (!RangeEnd)
          RangeBegin = RangeEnd = &MI;
        else
          RangeBegin = &MI;
      }

      // Update EHPadStack.
      if (MI.getOpcode() == WebAssembly::TRY)
        EHPadStack.pop_back();
      else if (WebAssembly::isCatch(MI.getOpcode()))
        EHPadStack.push_back(MI.getParent());
    }

    if (RangeEnd)
      RecordCallerMismatchRange(EHPadStack.back());
  }

  assert(EHPadStack.empty());

  // We don't have any unwind destination mismatches to resolve.
  if (UnwindDestToTryRanges.empty())
    return false;

  // Now we fix the mismatches by wrapping calls with inner try-delegates.
  for (auto &P : UnwindDestToTryRanges) {
    NumCallUnwindMismatches += P.second.size();
    MachineBasicBlock *UnwindDest = P.first;
    auto &TryRanges = P.second;

    for (auto Range : TryRanges) {
      MachineInstr *RangeBegin = nullptr, *RangeEnd = nullptr;
      std::tie(RangeBegin, RangeEnd) = Range;
      auto *MBB = RangeBegin->getParent();

      // If this BB has an EH pad successor, i.e., ends with an 'invoke', now we
      // are going to wrap the invoke with try-delegate, making the 'delegate'
      // BB the new successor instead, so remove the EH pad succesor here. The
      // BB may not have an EH pad successor if calls in this BB throw to the
      // caller.
      MachineBasicBlock *EHPad = nullptr;
      for (auto *Succ : MBB->successors()) {
        if (Succ->isEHPad()) {
          EHPad = Succ;
          break;
        }
      }
      if (EHPad)
        MBB->removeSuccessor(EHPad);

      addTryDelegate(RangeBegin, RangeEnd, UnwindDest);
    }
  }

  return true;
}

bool WebAssemblyCFGStackify::fixCatchUnwindMismatches(MachineFunction &MF) {
  // There is another kind of unwind destination mismatches besides call unwind
  // mismatches, which we will call "catch unwind mismatches". See this example
  // after the marker placement:
  // try
  //   try
  //     call @foo
  //   catch __cpp_exception  ;; ehpad A (next unwind dest: caller)
  //     ...
  //   end_try
  // catch_all                ;; ehpad B
  //   ...
  // end_try
  //
  // 'call @foo's unwind destination is the ehpad A. But suppose 'call @foo'
  // throws a foreign exception that is not caught by ehpad A, and its next
  // destination should be the caller. But after control flow linearization,
  // another EH pad can be placed in between (e.g. ehpad B here), making the
  // next unwind destination incorrect. In this case, the  foreign exception
  // will instead go to ehpad B and will be caught there instead. In this
  // example the correct next unwind destination is the caller, but it can be
  // another outer catch in other cases.
  //
  // There is no specific 'call' or 'throw' instruction to wrap with a
  // try-delegate, so we wrap the whole try-catch-end with a try-delegate and
  // make it rethrow to the right destination, as in the example below:
  // try
  //   try                     ;; (new)
  //     try
  //       call @foo
  //     catch __cpp_exception ;; ehpad A (next unwind dest: caller)
  //       ...
  //     end_try
  //   delegate 1 (caller)     ;; (new)
  // catch_all                 ;; ehpad B
  //   ...
  // end_try

  const auto *EHInfo = MF.getWasmEHFuncInfo();
  assert(EHInfo);
  SmallVector<const MachineBasicBlock *, 8> EHPadStack;
  // For EH pads that have catch unwind mismatches, a map of <EH pad, its
  // correct unwind destination>.
  DenseMap<MachineBasicBlock *, MachineBasicBlock *> EHPadToUnwindDest;

  for (auto &MBB : reverse(MF)) {
    for (auto &MI : reverse(MBB)) {
      if (MI.getOpcode() == WebAssembly::TRY)
        EHPadStack.pop_back();
      else if (MI.getOpcode() == WebAssembly::DELEGATE)
        EHPadStack.push_back(&MBB);
      else if (WebAssembly::isCatch(MI.getOpcode())) {
        auto *EHPad = &MBB;

        // catch_all always catches an exception, so we don't need to do
        // anything
        if (MI.getOpcode() == WebAssembly::CATCH_ALL) {
        }

        // This can happen when the unwind dest was removed during the
        // optimization, e.g. because it was unreachable.
        else if (EHPadStack.empty() && EHInfo->hasUnwindDest(EHPad)) {
          LLVM_DEBUG(dbgs() << "EHPad (" << EHPad->getName()
                            << "'s unwind destination does not exist anymore"
                            << "\n\n");
        }

        // The EHPad's next unwind destination is the caller, but we incorrectly
        // unwind to another EH pad.
        else if (!EHPadStack.empty() && !EHInfo->hasUnwindDest(EHPad)) {
          EHPadToUnwindDest[EHPad] = getFakeCallerBlock(MF);
          LLVM_DEBUG(dbgs()
                     << "- Catch unwind mismatch:\nEHPad = " << EHPad->getName()
                     << "  Original dest = caller  Current dest = "
                     << EHPadStack.back()->getName() << "\n\n");
        }

        // The EHPad's next unwind destination is an EH pad, whereas we
        // incorrectly unwind to another EH pad.
        else if (!EHPadStack.empty() && EHInfo->hasUnwindDest(EHPad)) {
          auto *UnwindDest = EHInfo->getUnwindDest(EHPad);
          if (EHPadStack.back() != UnwindDest) {
            EHPadToUnwindDest[EHPad] = UnwindDest;
            LLVM_DEBUG(dbgs() << "- Catch unwind mismatch:\nEHPad = "
                              << EHPad->getName() << "  Original dest = "
                              << UnwindDest->getName() << "  Current dest = "
                              << EHPadStack.back()->getName() << "\n\n");
          }
        }

        EHPadStack.push_back(EHPad);
      }
    }
  }

  assert(EHPadStack.empty());
  if (EHPadToUnwindDest.empty())
    return false;
  NumCatchUnwindMismatches += EHPadToUnwindDest.size();
  SmallPtrSet<MachineBasicBlock *, 4> NewEndTryBBs;

  for (auto &P : EHPadToUnwindDest) {
    MachineBasicBlock *EHPad = P.first;
    MachineBasicBlock *UnwindDest = P.second;
    MachineInstr *Try = EHPadToTry[EHPad];
    MachineInstr *EndTry = BeginToEnd[Try];
    addTryDelegate(Try, EndTry, UnwindDest);
    NewEndTryBBs.insert(EndTry->getParent());
  }

  // Adding a try-delegate wrapping an existing try-catch-end can make existing
  // branch destination BBs invalid. For example,
  //
  // - Before:
  // bb0:
  //   block
  //     br bb3
  // bb1:
  //     try
  //       ...
  // bb2: (ehpad)
  //     catch
  // bb3:
  //     end_try
  //   end_block   ;; 'br bb3' targets here
  //
  // Suppose this try-catch-end has a catch unwind mismatch, so we need to wrap
  // this with a try-delegate. Then this becomes:
  //
  // - After:
  // bb0:
  //   block
  //     br bb3    ;; invalid destination!
  // bb1:
  //     try       ;; (new instruction)
  //       try
  //         ...
  // bb2: (ehpad)
  //       catch
  // bb3:
  //       end_try ;; 'br bb3' still incorrectly targets here!
  // delegate_bb:  ;; (new BB)
  //     delegate  ;; (new instruction)
  // split_bb:     ;; (new BB)
  //   end_block
  //
  // Now 'br bb3' incorrectly branches to an inner scope.
  //
  // As we can see in this case, when branches target a BB that has both
  // 'end_try' and 'end_block' and the BB is split to insert a 'delegate', we
  // have to remap existing branch destinations so that they target not the
  // 'end_try' BB but the new 'end_block' BB. There can be multiple 'delegate's
  // in between, so we try to find the next BB with 'end_block' instruction. In
  // this example, the 'br bb3' instruction should be remapped to 'br split_bb'.
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.isTerminator()) {
        for (auto &MO : MI.operands()) {
          if (MO.isMBB() && NewEndTryBBs.count(MO.getMBB())) {
            auto *BrDest = MO.getMBB();
            bool FoundEndBlock = false;
            for (; std::next(BrDest->getIterator()) != MF.end();
                 BrDest = BrDest->getNextNode()) {
              for (const auto &MI : *BrDest) {
                if (MI.getOpcode() == WebAssembly::END_BLOCK) {
                  FoundEndBlock = true;
                  break;
                }
              }
              if (FoundEndBlock)
                break;
            }
            assert(FoundEndBlock);
            MO.setMBB(BrDest);
          }
        }
      }
    }
  }

  return true;
}

void WebAssemblyCFGStackify::recalculateScopeTops(MachineFunction &MF) {
  // Renumber BBs and recalculate ScopeTop info because new BBs might have been
  // created and inserted during fixing unwind mismatches.
  MF.RenumberBlocks();
  ScopeTops.clear();
  ScopeTops.resize(MF.getNumBlockIDs());
  for (auto &MBB : reverse(MF)) {
    for (auto &MI : reverse(MBB)) {
      if (ScopeTops[MBB.getNumber()])
        break;
      switch (MI.getOpcode()) {
      case WebAssembly::END_BLOCK:
      case WebAssembly::END_LOOP:
      case WebAssembly::END_TRY:
      case WebAssembly::DELEGATE:
        updateScopeTops(EndToBegin[&MI]->getParent(), &MBB);
        break;
      case WebAssembly::CATCH:
      case WebAssembly::CATCH_ALL:
        updateScopeTops(EHPadToTry[&MBB]->getParent(), &MBB);
        break;
      }
    }
  }
}

/// In normal assembly languages, when the end of a function is unreachable,
/// because the function ends in an infinite loop or a noreturn call or similar,
/// it isn't necessary to worry about the function return type at the end of
/// the function, because it's never reached. However, in WebAssembly, blocks
/// that end at the function end need to have a return type signature that
/// matches the function signature, even though it's unreachable. This function
/// checks for such cases and fixes up the signatures.
void WebAssemblyCFGStackify::fixEndsAtEndOfFunction(MachineFunction &MF) {
  const auto &MFI = *MF.getInfo<WebAssemblyFunctionInfo>();

  if (MFI.getResults().empty())
    return;

  // MCInstLower will add the proper types to multivalue signatures based on the
  // function return type
  WebAssembly::BlockType RetType =
      MFI.getResults().size() > 1
          ? WebAssembly::BlockType::Multivalue
          : WebAssembly::BlockType(
                WebAssembly::toValType(MFI.getResults().front()));

  SmallVector<MachineBasicBlock::reverse_iterator, 4> Worklist;
  Worklist.push_back(MF.rbegin()->rbegin());

  auto Process = [&](MachineBasicBlock::reverse_iterator It) {
    auto *MBB = It->getParent();
    while (It != MBB->rend()) {
      MachineInstr &MI = *It++;
      if (MI.isPosition() || MI.isDebugInstr())
        continue;
      switch (MI.getOpcode()) {
      case WebAssembly::END_TRY: {
        // If a 'try''s return type is fixed, both its try body and catch body
        // should satisfy the return type, so we need to search 'end'
        // instructions before its corresponding 'catch' too.
        auto *EHPad = TryToEHPad.lookup(EndToBegin[&MI]);
        assert(EHPad);
        auto NextIt =
            std::next(WebAssembly::findCatch(EHPad)->getReverseIterator());
        if (NextIt != EHPad->rend())
          Worklist.push_back(NextIt);
        [[fallthrough]];
      }
      case WebAssembly::END_BLOCK:
      case WebAssembly::END_LOOP:
      case WebAssembly::DELEGATE:
        EndToBegin[&MI]->getOperand(0).setImm(int32_t(RetType));
        continue;
      default:
        // Something other than an `end`. We're done for this BB.
        return;
      }
    }
    // We've reached the beginning of a BB. Continue the search in the previous
    // BB.
    Worklist.push_back(MBB->getPrevNode()->rbegin());
  };

  while (!Worklist.empty())
    Process(Worklist.pop_back_val());
}

// WebAssembly functions end with an end instruction, as if the function body
// were a block.
static void appendEndToFunction(MachineFunction &MF,
                                const WebAssemblyInstrInfo &TII) {
  BuildMI(MF.back(), MF.back().end(),
          MF.back().findPrevDebugLoc(MF.back().end()),
          TII.get(WebAssembly::END_FUNCTION));
}

/// Insert LOOP/TRY/BLOCK markers at appropriate places.
void WebAssemblyCFGStackify::placeMarkers(MachineFunction &MF) {
  // We allocate one more than the number of blocks in the function to
  // accommodate for the possible fake block we may insert at the end.
  ScopeTops.resize(MF.getNumBlockIDs() + 1);
  // Place the LOOP for MBB if MBB is the header of a loop.
  for (auto &MBB : MF)
    placeLoopMarker(MBB);

  const MCAsmInfo *MCAI = MF.getTarget().getMCAsmInfo();
  for (auto &MBB : MF) {
    if (MBB.isEHPad()) {
      // Place the TRY for MBB if MBB is the EH pad of an exception.
      if (MCAI->getExceptionHandlingType() == ExceptionHandling::Wasm &&
          MF.getFunction().hasPersonalityFn())
        placeTryMarker(MBB);
    } else {
      // Place the BLOCK for MBB if MBB is branched to from above.
      placeBlockMarker(MBB);
    }
  }
  // Fix mismatches in unwind destinations induced by linearizing the code.
  if (MCAI->getExceptionHandlingType() == ExceptionHandling::Wasm &&
      MF.getFunction().hasPersonalityFn()) {
    bool Changed = fixCallUnwindMismatches(MF);
    Changed |= fixCatchUnwindMismatches(MF);
    if (Changed)
      recalculateScopeTops(MF);
  }
}

unsigned WebAssemblyCFGStackify::getBranchDepth(
    const SmallVectorImpl<EndMarkerInfo> &Stack, const MachineBasicBlock *MBB) {
  unsigned Depth = 0;
  for (auto X : reverse(Stack)) {
    if (X.first == MBB)
      break;
    ++Depth;
  }
  assert(Depth < Stack.size() && "Branch destination should be in scope");
  return Depth;
}

unsigned WebAssemblyCFGStackify::getDelegateDepth(
    const SmallVectorImpl<EndMarkerInfo> &Stack, const MachineBasicBlock *MBB) {
  if (MBB == FakeCallerBB)
    return Stack.size();
  // Delegate's destination is either a catch or a another delegate BB. When the
  // destination is another delegate, we can compute the argument in the same
  // way as branches, because the target delegate BB only contains the single
  // delegate instruction.
  if (!MBB->isEHPad()) // Target is a delegate BB
    return getBranchDepth(Stack, MBB);

  // When the delegate's destination is a catch BB, we need to use its
  // corresponding try's end_try BB because Stack contains each marker's end BB.
  // Also we need to check if the end marker instruction matches, because a
  // single BB can contain multiple end markers, like this:
  // bb:
  //   END_BLOCK
  //   END_TRY
  //   END_BLOCK
  //   END_TRY
  //   ...
  //
  // In case of branches getting the immediate that targets any of these is
  // fine, but delegate has to exactly target the correct try.
  unsigned Depth = 0;
  const MachineInstr *EndTry = BeginToEnd[EHPadToTry[MBB]];
  for (auto X : reverse(Stack)) {
    if (X.first == EndTry->getParent() && X.second == EndTry)
      break;
    ++Depth;
  }
  assert(Depth < Stack.size() && "Delegate destination should be in scope");
  return Depth;
}

unsigned WebAssemblyCFGStackify::getRethrowDepth(
    const SmallVectorImpl<EndMarkerInfo> &Stack,
    const MachineBasicBlock *EHPadToRethrow) {
  unsigned Depth = 0;
  for (auto X : reverse(Stack)) {
    const MachineInstr *End = X.second;
    if (End->getOpcode() == WebAssembly::END_TRY) {
      auto *EHPad = TryToEHPad[EndToBegin[End]];
      if (EHPadToRethrow == EHPad)
        break;
    }
    ++Depth;
  }
  assert(Depth < Stack.size() && "Rethrow destination should be in scope");
  return Depth;
}

void WebAssemblyCFGStackify::rewriteDepthImmediates(MachineFunction &MF) {
  // Now rewrite references to basic blocks to be depth immediates.
  SmallVector<EndMarkerInfo, 8> Stack;
  for (auto &MBB : reverse(MF)) {
    for (MachineInstr &MI : llvm::reverse(MBB)) {
      switch (MI.getOpcode()) {
      case WebAssembly::BLOCK:
      case WebAssembly::TRY:
        assert(ScopeTops[Stack.back().first->getNumber()]->getNumber() <=
                   MBB.getNumber() &&
               "Block/try marker should be balanced");
        Stack.pop_back();
        break;

      case WebAssembly::LOOP:
        assert(Stack.back().first == &MBB && "Loop top should be balanced");
        Stack.pop_back();
        break;

      case WebAssembly::END_BLOCK:
      case WebAssembly::END_TRY:
        Stack.push_back(std::make_pair(&MBB, &MI));
        break;

      case WebAssembly::END_LOOP:
        Stack.push_back(std::make_pair(EndToBegin[&MI]->getParent(), &MI));
        break;

      default:
        if (MI.isTerminator()) {
          // Rewrite MBB operands to be depth immediates.
          SmallVector<MachineOperand, 4> Ops(MI.operands());
          while (MI.getNumOperands() > 0)
            MI.removeOperand(MI.getNumOperands() - 1);
          for (auto MO : Ops) {
            if (MO.isMBB()) {
              if (MI.getOpcode() == WebAssembly::DELEGATE)
                MO = MachineOperand::CreateImm(
                    getDelegateDepth(Stack, MO.getMBB()));
              else if (MI.getOpcode() == WebAssembly::RETHROW)
                MO = MachineOperand::CreateImm(
                    getRethrowDepth(Stack, MO.getMBB()));
              else
                MO = MachineOperand::CreateImm(
                    getBranchDepth(Stack, MO.getMBB()));
            }
            MI.addOperand(MF, MO);
          }
        }

        if (MI.getOpcode() == WebAssembly::DELEGATE)
          Stack.push_back(std::make_pair(&MBB, &MI));
        break;
      }
    }
  }
  assert(Stack.empty() && "Control flow should be balanced");
}

void WebAssemblyCFGStackify::cleanupFunctionData(MachineFunction &MF) {
  if (FakeCallerBB)
    MF.deleteMachineBasicBlock(FakeCallerBB);
  AppendixBB = FakeCallerBB = nullptr;
}

void WebAssemblyCFGStackify::releaseMemory() {
  ScopeTops.clear();
  BeginToEnd.clear();
  EndToBegin.clear();
  TryToEHPad.clear();
  EHPadToTry.clear();
}

bool WebAssemblyCFGStackify::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** CFG Stackifying **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');
  const MCAsmInfo *MCAI = MF.getTarget().getMCAsmInfo();

  releaseMemory();

  // Liveness is not tracked for VALUE_STACK physreg.
  MF.getRegInfo().invalidateLiveness();

  // Place the BLOCK/LOOP/TRY markers to indicate the beginnings of scopes.
  placeMarkers(MF);

  // Remove unnecessary instructions possibly introduced by try/end_trys.
  if (MCAI->getExceptionHandlingType() == ExceptionHandling::Wasm &&
      MF.getFunction().hasPersonalityFn())
    removeUnnecessaryInstrs(MF);

  // Convert MBB operands in terminators to relative depth immediates.
  rewriteDepthImmediates(MF);

  // Fix up block/loop/try signatures at the end of the function to conform to
  // WebAssembly's rules.
  fixEndsAtEndOfFunction(MF);

  // Add an end instruction at the end of the function body.
  const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
  if (!MF.getSubtarget<WebAssemblySubtarget>()
           .getTargetTriple()
           .isOSBinFormatELF())
    appendEndToFunction(MF, TII);

  cleanupFunctionData(MF);

  MF.getInfo<WebAssemblyFunctionInfo>()->setCFGStackified();
  return true;
}
