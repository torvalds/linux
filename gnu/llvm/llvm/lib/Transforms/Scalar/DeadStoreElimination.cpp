//===- DeadStoreElimination.cpp - MemorySSA Backed Dead Store Elimination -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The code below implements dead store elimination using MemorySSA. It uses
// the following general approach: given a MemoryDef, walk upwards to find
// clobbering MemoryDefs that may be killed by the starting def. Then check
// that there are no uses that may read the location of the original MemoryDef
// in between both MemoryDefs. A bit more concretely:
//
// For all MemoryDefs StartDef:
// 1. Get the next dominating clobbering MemoryDef (MaybeDeadAccess) by walking
//    upwards.
// 2. Check that there are no reads between MaybeDeadAccess and the StartDef by
//    checking all uses starting at MaybeDeadAccess and walking until we see
//    StartDef.
// 3. For each found CurrentDef, check that:
//   1. There are no barrier instructions between CurrentDef and StartDef (like
//       throws or stores with ordering constraints).
//   2. StartDef is executed whenever CurrentDef is executed.
//   3. StartDef completely overwrites CurrentDef.
// 4. Erase CurrentDef from the function and MemorySSA.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/DeadStoreElimination.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/MustExecute.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugCounter.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <utility>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "dse"

STATISTIC(NumRemainingStores, "Number of stores remaining after DSE");
STATISTIC(NumRedundantStores, "Number of redundant stores deleted");
STATISTIC(NumFastStores, "Number of stores deleted");
STATISTIC(NumFastOther, "Number of other instrs removed");
STATISTIC(NumCompletePartials, "Number of stores dead by later partials");
STATISTIC(NumModifiedStores, "Number of stores modified");
STATISTIC(NumCFGChecks, "Number of stores modified");
STATISTIC(NumCFGTries, "Number of stores modified");
STATISTIC(NumCFGSuccess, "Number of stores modified");
STATISTIC(NumGetDomMemoryDefPassed,
          "Number of times a valid candidate is returned from getDomMemoryDef");
STATISTIC(NumDomMemDefChecks,
          "Number iterations check for reads in getDomMemoryDef");

DEBUG_COUNTER(MemorySSACounter, "dse-memoryssa",
              "Controls which MemoryDefs are eliminated.");

static cl::opt<bool>
EnablePartialOverwriteTracking("enable-dse-partial-overwrite-tracking",
  cl::init(true), cl::Hidden,
  cl::desc("Enable partial-overwrite tracking in DSE"));

static cl::opt<bool>
EnablePartialStoreMerging("enable-dse-partial-store-merging",
  cl::init(true), cl::Hidden,
  cl::desc("Enable partial store merging in DSE"));

static cl::opt<unsigned>
    MemorySSAScanLimit("dse-memoryssa-scanlimit", cl::init(150), cl::Hidden,
                       cl::desc("The number of memory instructions to scan for "
                                "dead store elimination (default = 150)"));
static cl::opt<unsigned> MemorySSAUpwardsStepLimit(
    "dse-memoryssa-walklimit", cl::init(90), cl::Hidden,
    cl::desc("The maximum number of steps while walking upwards to find "
             "MemoryDefs that may be killed (default = 90)"));

static cl::opt<unsigned> MemorySSAPartialStoreLimit(
    "dse-memoryssa-partial-store-limit", cl::init(5), cl::Hidden,
    cl::desc("The maximum number candidates that only partially overwrite the "
             "killing MemoryDef to consider"
             " (default = 5)"));

static cl::opt<unsigned> MemorySSADefsPerBlockLimit(
    "dse-memoryssa-defs-per-block-limit", cl::init(5000), cl::Hidden,
    cl::desc("The number of MemoryDefs we consider as candidates to eliminated "
             "other stores per basic block (default = 5000)"));

static cl::opt<unsigned> MemorySSASameBBStepCost(
    "dse-memoryssa-samebb-cost", cl::init(1), cl::Hidden,
    cl::desc(
        "The cost of a step in the same basic block as the killing MemoryDef"
        "(default = 1)"));

static cl::opt<unsigned>
    MemorySSAOtherBBStepCost("dse-memoryssa-otherbb-cost", cl::init(5),
                             cl::Hidden,
                             cl::desc("The cost of a step in a different basic "
                                      "block than the killing MemoryDef"
                                      "(default = 5)"));

static cl::opt<unsigned> MemorySSAPathCheckLimit(
    "dse-memoryssa-path-check-limit", cl::init(50), cl::Hidden,
    cl::desc("The maximum number of blocks to check when trying to prove that "
             "all paths to an exit go through a killing block (default = 50)"));

// This flags allows or disallows DSE to optimize MemorySSA during its
// traversal. Note that DSE optimizing MemorySSA may impact other passes
// downstream of the DSE invocation and can lead to issues not being
// reproducible in isolation (i.e. when MemorySSA is built from scratch). In
// those cases, the flag can be used to check if DSE's MemorySSA optimizations
// impact follow-up passes.
static cl::opt<bool>
    OptimizeMemorySSA("dse-optimize-memoryssa", cl::init(true), cl::Hidden,
                      cl::desc("Allow DSE to optimize memory accesses."));

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//
using OverlapIntervalsTy = std::map<int64_t, int64_t>;
using InstOverlapIntervalsTy = DenseMap<Instruction *, OverlapIntervalsTy>;

/// Returns true if the end of this instruction can be safely shortened in
/// length.
static bool isShortenableAtTheEnd(Instruction *I) {
  // Don't shorten stores for now
  if (isa<StoreInst>(I))
    return false;

  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
      default: return false;
      case Intrinsic::memset:
      case Intrinsic::memcpy:
      case Intrinsic::memcpy_element_unordered_atomic:
      case Intrinsic::memset_element_unordered_atomic:
        // Do shorten memory intrinsics.
        // FIXME: Add memmove if it's also safe to transform.
        return true;
    }
  }

  // Don't shorten libcalls calls for now.

  return false;
}

/// Returns true if the beginning of this instruction can be safely shortened
/// in length.
static bool isShortenableAtTheBeginning(Instruction *I) {
  // FIXME: Handle only memset for now. Supporting memcpy/memmove should be
  // easily done by offsetting the source address.
  return isa<AnyMemSetInst>(I);
}

static std::optional<TypeSize> getPointerSize(const Value *V,
                                              const DataLayout &DL,
                                              const TargetLibraryInfo &TLI,
                                              const Function *F) {
  uint64_t Size;
  ObjectSizeOpts Opts;
  Opts.NullIsUnknownSize = NullPointerIsDefined(F);

  if (getObjectSize(V, Size, DL, &TLI, Opts))
    return TypeSize::getFixed(Size);
  return std::nullopt;
}

namespace {

enum OverwriteResult {
  OW_Begin,
  OW_Complete,
  OW_End,
  OW_PartialEarlierWithFullLater,
  OW_MaybePartial,
  OW_None,
  OW_Unknown
};

} // end anonymous namespace

/// Check if two instruction are masked stores that completely
/// overwrite one another. More specifically, \p KillingI has to
/// overwrite \p DeadI.
static OverwriteResult isMaskedStoreOverwrite(const Instruction *KillingI,
                                              const Instruction *DeadI,
                                              BatchAAResults &AA) {
  const auto *KillingII = dyn_cast<IntrinsicInst>(KillingI);
  const auto *DeadII = dyn_cast<IntrinsicInst>(DeadI);
  if (KillingII == nullptr || DeadII == nullptr)
    return OW_Unknown;
  if (KillingII->getIntrinsicID() != DeadII->getIntrinsicID())
    return OW_Unknown;
  if (KillingII->getIntrinsicID() == Intrinsic::masked_store) {
    // Type size.
    VectorType *KillingTy =
        cast<VectorType>(KillingII->getArgOperand(0)->getType());
    VectorType *DeadTy = cast<VectorType>(DeadII->getArgOperand(0)->getType());
    if (KillingTy->getScalarSizeInBits() != DeadTy->getScalarSizeInBits())
      return OW_Unknown;
    // Element count.
    if (KillingTy->getElementCount() != DeadTy->getElementCount())
      return OW_Unknown;
    // Pointers.
    Value *KillingPtr = KillingII->getArgOperand(1)->stripPointerCasts();
    Value *DeadPtr = DeadII->getArgOperand(1)->stripPointerCasts();
    if (KillingPtr != DeadPtr && !AA.isMustAlias(KillingPtr, DeadPtr))
      return OW_Unknown;
    // Masks.
    // TODO: check that KillingII's mask is a superset of the DeadII's mask.
    if (KillingII->getArgOperand(3) != DeadII->getArgOperand(3))
      return OW_Unknown;
    return OW_Complete;
  }
  return OW_Unknown;
}

/// Return 'OW_Complete' if a store to the 'KillingLoc' location completely
/// overwrites a store to the 'DeadLoc' location, 'OW_End' if the end of the
/// 'DeadLoc' location is completely overwritten by 'KillingLoc', 'OW_Begin'
/// if the beginning of the 'DeadLoc' location is overwritten by 'KillingLoc'.
/// 'OW_PartialEarlierWithFullLater' means that a dead (big) store was
/// overwritten by a killing (smaller) store which doesn't write outside the big
/// store's memory locations. Returns 'OW_Unknown' if nothing can be determined.
/// NOTE: This function must only be called if both \p KillingLoc and \p
/// DeadLoc belong to the same underlying object with valid \p KillingOff and
/// \p DeadOff.
static OverwriteResult isPartialOverwrite(const MemoryLocation &KillingLoc,
                                          const MemoryLocation &DeadLoc,
                                          int64_t KillingOff, int64_t DeadOff,
                                          Instruction *DeadI,
                                          InstOverlapIntervalsTy &IOL) {
  const uint64_t KillingSize = KillingLoc.Size.getValue();
  const uint64_t DeadSize = DeadLoc.Size.getValue();
  // We may now overlap, although the overlap is not complete. There might also
  // be other incomplete overlaps, and together, they might cover the complete
  // dead store.
  // Note: The correctness of this logic depends on the fact that this function
  // is not even called providing DepWrite when there are any intervening reads.
  if (EnablePartialOverwriteTracking &&
      KillingOff < int64_t(DeadOff + DeadSize) &&
      int64_t(KillingOff + KillingSize) >= DeadOff) {

    // Insert our part of the overlap into the map.
    auto &IM = IOL[DeadI];
    LLVM_DEBUG(dbgs() << "DSE: Partial overwrite: DeadLoc [" << DeadOff << ", "
                      << int64_t(DeadOff + DeadSize) << ") KillingLoc ["
                      << KillingOff << ", " << int64_t(KillingOff + KillingSize)
                      << ")\n");

    // Make sure that we only insert non-overlapping intervals and combine
    // adjacent intervals. The intervals are stored in the map with the ending
    // offset as the key (in the half-open sense) and the starting offset as
    // the value.
    int64_t KillingIntStart = KillingOff;
    int64_t KillingIntEnd = KillingOff + KillingSize;

    // Find any intervals ending at, or after, KillingIntStart which start
    // before KillingIntEnd.
    auto ILI = IM.lower_bound(KillingIntStart);
    if (ILI != IM.end() && ILI->second <= KillingIntEnd) {
      // This existing interval is overlapped with the current store somewhere
      // in [KillingIntStart, KillingIntEnd]. Merge them by erasing the existing
      // intervals and adjusting our start and end.
      KillingIntStart = std::min(KillingIntStart, ILI->second);
      KillingIntEnd = std::max(KillingIntEnd, ILI->first);
      ILI = IM.erase(ILI);

      // Continue erasing and adjusting our end in case other previous
      // intervals are also overlapped with the current store.
      //
      // |--- dead 1 ---|  |--- dead 2 ---|
      //     |------- killing---------|
      //
      while (ILI != IM.end() && ILI->second <= KillingIntEnd) {
        assert(ILI->second > KillingIntStart && "Unexpected interval");
        KillingIntEnd = std::max(KillingIntEnd, ILI->first);
        ILI = IM.erase(ILI);
      }
    }

    IM[KillingIntEnd] = KillingIntStart;

    ILI = IM.begin();
    if (ILI->second <= DeadOff && ILI->first >= int64_t(DeadOff + DeadSize)) {
      LLVM_DEBUG(dbgs() << "DSE: Full overwrite from partials: DeadLoc ["
                        << DeadOff << ", " << int64_t(DeadOff + DeadSize)
                        << ") Composite KillingLoc [" << ILI->second << ", "
                        << ILI->first << ")\n");
      ++NumCompletePartials;
      return OW_Complete;
    }
  }

  // Check for a dead store which writes to all the memory locations that
  // the killing store writes to.
  if (EnablePartialStoreMerging && KillingOff >= DeadOff &&
      int64_t(DeadOff + DeadSize) > KillingOff &&
      uint64_t(KillingOff - DeadOff) + KillingSize <= DeadSize) {
    LLVM_DEBUG(dbgs() << "DSE: Partial overwrite a dead load [" << DeadOff
                      << ", " << int64_t(DeadOff + DeadSize)
                      << ") by a killing store [" << KillingOff << ", "
                      << int64_t(KillingOff + KillingSize) << ")\n");
    // TODO: Maybe come up with a better name?
    return OW_PartialEarlierWithFullLater;
  }

  // Another interesting case is if the killing store overwrites the end of the
  // dead store.
  //
  //      |--dead--|
  //                |--   killing   --|
  //
  // In this case we may want to trim the size of dead store to avoid
  // generating stores to addresses which will definitely be overwritten killing
  // store.
  if (!EnablePartialOverwriteTracking &&
      (KillingOff > DeadOff && KillingOff < int64_t(DeadOff + DeadSize) &&
       int64_t(KillingOff + KillingSize) >= int64_t(DeadOff + DeadSize)))
    return OW_End;

  // Finally, we also need to check if the killing store overwrites the
  // beginning of the dead store.
  //
  //                |--dead--|
  //      |--  killing  --|
  //
  // In this case we may want to move the destination address and trim the size
  // of dead store to avoid generating stores to addresses which will definitely
  // be overwritten killing store.
  if (!EnablePartialOverwriteTracking &&
      (KillingOff <= DeadOff && int64_t(KillingOff + KillingSize) > DeadOff)) {
    assert(int64_t(KillingOff + KillingSize) < int64_t(DeadOff + DeadSize) &&
           "Expect to be handled as OW_Complete");
    return OW_Begin;
  }
  // Otherwise, they don't completely overlap.
  return OW_Unknown;
}

/// Returns true if the memory which is accessed by the second instruction is not
/// modified between the first and the second instruction.
/// Precondition: Second instruction must be dominated by the first
/// instruction.
static bool
memoryIsNotModifiedBetween(Instruction *FirstI, Instruction *SecondI,
                           BatchAAResults &AA, const DataLayout &DL,
                           DominatorTree *DT) {
  // Do a backwards scan through the CFG from SecondI to FirstI. Look for
  // instructions which can modify the memory location accessed by SecondI.
  //
  // While doing the walk keep track of the address to check. It might be
  // different in different basic blocks due to PHI translation.
  using BlockAddressPair = std::pair<BasicBlock *, PHITransAddr>;
  SmallVector<BlockAddressPair, 16> WorkList;
  // Keep track of the address we visited each block with. Bail out if we
  // visit a block with different addresses.
  DenseMap<BasicBlock *, Value *> Visited;

  BasicBlock::iterator FirstBBI(FirstI);
  ++FirstBBI;
  BasicBlock::iterator SecondBBI(SecondI);
  BasicBlock *FirstBB = FirstI->getParent();
  BasicBlock *SecondBB = SecondI->getParent();
  MemoryLocation MemLoc;
  if (auto *MemSet = dyn_cast<MemSetInst>(SecondI))
    MemLoc = MemoryLocation::getForDest(MemSet);
  else
    MemLoc = MemoryLocation::get(SecondI);

  auto *MemLocPtr = const_cast<Value *>(MemLoc.Ptr);

  // Start checking the SecondBB.
  WorkList.push_back(
      std::make_pair(SecondBB, PHITransAddr(MemLocPtr, DL, nullptr)));
  bool isFirstBlock = true;

  // Check all blocks going backward until we reach the FirstBB.
  while (!WorkList.empty()) {
    BlockAddressPair Current = WorkList.pop_back_val();
    BasicBlock *B = Current.first;
    PHITransAddr &Addr = Current.second;
    Value *Ptr = Addr.getAddr();

    // Ignore instructions before FirstI if this is the FirstBB.
    BasicBlock::iterator BI = (B == FirstBB ? FirstBBI : B->begin());

    BasicBlock::iterator EI;
    if (isFirstBlock) {
      // Ignore instructions after SecondI if this is the first visit of SecondBB.
      assert(B == SecondBB && "first block is not the store block");
      EI = SecondBBI;
      isFirstBlock = false;
    } else {
      // It's not SecondBB or (in case of a loop) the second visit of SecondBB.
      // In this case we also have to look at instructions after SecondI.
      EI = B->end();
    }
    for (; BI != EI; ++BI) {
      Instruction *I = &*BI;
      if (I->mayWriteToMemory() && I != SecondI)
        if (isModSet(AA.getModRefInfo(I, MemLoc.getWithNewPtr(Ptr))))
          return false;
    }
    if (B != FirstBB) {
      assert(B != &FirstBB->getParent()->getEntryBlock() &&
          "Should not hit the entry block because SI must be dominated by LI");
      for (BasicBlock *Pred : predecessors(B)) {
        PHITransAddr PredAddr = Addr;
        if (PredAddr.needsPHITranslationFromBlock(B)) {
          if (!PredAddr.isPotentiallyPHITranslatable())
            return false;
          if (!PredAddr.translateValue(B, Pred, DT, false))
            return false;
        }
        Value *TranslatedPtr = PredAddr.getAddr();
        auto Inserted = Visited.insert(std::make_pair(Pred, TranslatedPtr));
        if (!Inserted.second) {
          // We already visited this block before. If it was with a different
          // address - bail out!
          if (TranslatedPtr != Inserted.first->second)
            return false;
          // ... otherwise just skip it.
          continue;
        }
        WorkList.push_back(std::make_pair(Pred, PredAddr));
      }
    }
  }
  return true;
}

static void shortenAssignment(Instruction *Inst, Value *OriginalDest,
                              uint64_t OldOffsetInBits, uint64_t OldSizeInBits,
                              uint64_t NewSizeInBits, bool IsOverwriteEnd) {
  const DataLayout &DL = Inst->getDataLayout();
  uint64_t DeadSliceSizeInBits = OldSizeInBits - NewSizeInBits;
  uint64_t DeadSliceOffsetInBits =
      OldOffsetInBits + (IsOverwriteEnd ? NewSizeInBits : 0);
  auto SetDeadFragExpr = [](auto *Assign,
                            DIExpression::FragmentInfo DeadFragment) {
    // createFragmentExpression expects an offset relative to the existing
    // fragment offset if there is one.
    uint64_t RelativeOffset = DeadFragment.OffsetInBits -
                              Assign->getExpression()
                                  ->getFragmentInfo()
                                  .value_or(DIExpression::FragmentInfo(0, 0))
                                  .OffsetInBits;
    if (auto NewExpr = DIExpression::createFragmentExpression(
            Assign->getExpression(), RelativeOffset, DeadFragment.SizeInBits)) {
      Assign->setExpression(*NewExpr);
      return;
    }
    // Failed to create a fragment expression for this so discard the value,
    // making this a kill location.
    auto *Expr = *DIExpression::createFragmentExpression(
        DIExpression::get(Assign->getContext(), std::nullopt),
        DeadFragment.OffsetInBits, DeadFragment.SizeInBits);
    Assign->setExpression(Expr);
    Assign->setKillLocation();
  };

  // A DIAssignID to use so that the inserted dbg.assign intrinsics do not
  // link to any instructions. Created in the loop below (once).
  DIAssignID *LinkToNothing = nullptr;
  LLVMContext &Ctx = Inst->getContext();
  auto GetDeadLink = [&Ctx, &LinkToNothing]() {
    if (!LinkToNothing)
      LinkToNothing = DIAssignID::getDistinct(Ctx);
    return LinkToNothing;
  };

  // Insert an unlinked dbg.assign intrinsic for the dead fragment after each
  // overlapping dbg.assign intrinsic. The loop invalidates the iterators
  // returned by getAssignmentMarkers so save a copy of the markers to iterate
  // over.
  auto LinkedRange = at::getAssignmentMarkers(Inst);
  SmallVector<DbgVariableRecord *> LinkedDVRAssigns =
      at::getDVRAssignmentMarkers(Inst);
  SmallVector<DbgAssignIntrinsic *> Linked(LinkedRange.begin(),
                                           LinkedRange.end());
  auto InsertAssignForOverlap = [&](auto *Assign) {
    std::optional<DIExpression::FragmentInfo> NewFragment;
    if (!at::calculateFragmentIntersect(DL, OriginalDest, DeadSliceOffsetInBits,
                                        DeadSliceSizeInBits, Assign,
                                        NewFragment) ||
        !NewFragment) {
      // We couldn't calculate the intersecting fragment for some reason. Be
      // cautious and unlink the whole assignment from the store.
      Assign->setKillAddress();
      Assign->setAssignId(GetDeadLink());
      return;
    }
    // No intersect.
    if (NewFragment->SizeInBits == 0)
      return;

    // Fragments overlap: insert a new dbg.assign for this dead part.
    auto *NewAssign = static_cast<decltype(Assign)>(Assign->clone());
    NewAssign->insertAfter(Assign);
    NewAssign->setAssignId(GetDeadLink());
    if (NewFragment)
      SetDeadFragExpr(NewAssign, *NewFragment);
    NewAssign->setKillAddress();
  };
  for_each(Linked, InsertAssignForOverlap);
  for_each(LinkedDVRAssigns, InsertAssignForOverlap);
}

static bool tryToShorten(Instruction *DeadI, int64_t &DeadStart,
                         uint64_t &DeadSize, int64_t KillingStart,
                         uint64_t KillingSize, bool IsOverwriteEnd) {
  auto *DeadIntrinsic = cast<AnyMemIntrinsic>(DeadI);
  Align PrefAlign = DeadIntrinsic->getDestAlign().valueOrOne();

  // We assume that memet/memcpy operates in chunks of the "largest" native
  // type size and aligned on the same value. That means optimal start and size
  // of memset/memcpy should be modulo of preferred alignment of that type. That
  // is it there is no any sense in trying to reduce store size any further
  // since any "extra" stores comes for free anyway.
  // On the other hand, maximum alignment we can achieve is limited by alignment
  // of initial store.

  // TODO: Limit maximum alignment by preferred (or abi?) alignment of the
  // "largest" native type.
  // Note: What is the proper way to get that value?
  // Should TargetTransformInfo::getRegisterBitWidth be used or anything else?
  // PrefAlign = std::min(DL.getPrefTypeAlign(LargestType), PrefAlign);

  int64_t ToRemoveStart = 0;
  uint64_t ToRemoveSize = 0;
  // Compute start and size of the region to remove. Make sure 'PrefAlign' is
  // maintained on the remaining store.
  if (IsOverwriteEnd) {
    // Calculate required adjustment for 'KillingStart' in order to keep
    // remaining store size aligned on 'PerfAlign'.
    uint64_t Off =
        offsetToAlignment(uint64_t(KillingStart - DeadStart), PrefAlign);
    ToRemoveStart = KillingStart + Off;
    if (DeadSize <= uint64_t(ToRemoveStart - DeadStart))
      return false;
    ToRemoveSize = DeadSize - uint64_t(ToRemoveStart - DeadStart);
  } else {
    ToRemoveStart = DeadStart;
    assert(KillingSize >= uint64_t(DeadStart - KillingStart) &&
           "Not overlapping accesses?");
    ToRemoveSize = KillingSize - uint64_t(DeadStart - KillingStart);
    // Calculate required adjustment for 'ToRemoveSize'in order to keep
    // start of the remaining store aligned on 'PerfAlign'.
    uint64_t Off = offsetToAlignment(ToRemoveSize, PrefAlign);
    if (Off != 0) {
      if (ToRemoveSize <= (PrefAlign.value() - Off))
        return false;
      ToRemoveSize -= PrefAlign.value() - Off;
    }
    assert(isAligned(PrefAlign, ToRemoveSize) &&
           "Should preserve selected alignment");
  }

  assert(ToRemoveSize > 0 && "Shouldn't reach here if nothing to remove");
  assert(DeadSize > ToRemoveSize && "Can't remove more than original size");

  uint64_t NewSize = DeadSize - ToRemoveSize;
  if (auto *AMI = dyn_cast<AtomicMemIntrinsic>(DeadI)) {
    // When shortening an atomic memory intrinsic, the newly shortened
    // length must remain an integer multiple of the element size.
    const uint32_t ElementSize = AMI->getElementSizeInBytes();
    if (0 != NewSize % ElementSize)
      return false;
  }

  LLVM_DEBUG(dbgs() << "DSE: Remove Dead Store:\n  OW "
                    << (IsOverwriteEnd ? "END" : "BEGIN") << ": " << *DeadI
                    << "\n  KILLER [" << ToRemoveStart << ", "
                    << int64_t(ToRemoveStart + ToRemoveSize) << ")\n");

  Value *DeadWriteLength = DeadIntrinsic->getLength();
  Value *TrimmedLength = ConstantInt::get(DeadWriteLength->getType(), NewSize);
  DeadIntrinsic->setLength(TrimmedLength);
  DeadIntrinsic->setDestAlignment(PrefAlign);

  Value *OrigDest = DeadIntrinsic->getRawDest();
  if (!IsOverwriteEnd) {
    Value *Indices[1] = {
        ConstantInt::get(DeadWriteLength->getType(), ToRemoveSize)};
    Instruction *NewDestGEP = GetElementPtrInst::CreateInBounds(
        Type::getInt8Ty(DeadIntrinsic->getContext()), OrigDest, Indices, "",
        DeadI->getIterator());
    NewDestGEP->setDebugLoc(DeadIntrinsic->getDebugLoc());
    DeadIntrinsic->setDest(NewDestGEP);
  }

  // Update attached dbg.assign intrinsics. Assume 8-bit byte.
  shortenAssignment(DeadI, OrigDest, DeadStart * 8, DeadSize * 8, NewSize * 8,
                    IsOverwriteEnd);

  // Finally update start and size of dead access.
  if (!IsOverwriteEnd)
    DeadStart += ToRemoveSize;
  DeadSize = NewSize;

  return true;
}

static bool tryToShortenEnd(Instruction *DeadI, OverlapIntervalsTy &IntervalMap,
                            int64_t &DeadStart, uint64_t &DeadSize) {
  if (IntervalMap.empty() || !isShortenableAtTheEnd(DeadI))
    return false;

  OverlapIntervalsTy::iterator OII = --IntervalMap.end();
  int64_t KillingStart = OII->second;
  uint64_t KillingSize = OII->first - KillingStart;

  assert(OII->first - KillingStart >= 0 && "Size expected to be positive");

  if (KillingStart > DeadStart &&
      // Note: "KillingStart - KillingStart" is known to be positive due to
      // preceding check.
      (uint64_t)(KillingStart - DeadStart) < DeadSize &&
      // Note: "DeadSize - (uint64_t)(KillingStart - DeadStart)" is known to
      // be non negative due to preceding checks.
      KillingSize >= DeadSize - (uint64_t)(KillingStart - DeadStart)) {
    if (tryToShorten(DeadI, DeadStart, DeadSize, KillingStart, KillingSize,
                     true)) {
      IntervalMap.erase(OII);
      return true;
    }
  }
  return false;
}

static bool tryToShortenBegin(Instruction *DeadI,
                              OverlapIntervalsTy &IntervalMap,
                              int64_t &DeadStart, uint64_t &DeadSize) {
  if (IntervalMap.empty() || !isShortenableAtTheBeginning(DeadI))
    return false;

  OverlapIntervalsTy::iterator OII = IntervalMap.begin();
  int64_t KillingStart = OII->second;
  uint64_t KillingSize = OII->first - KillingStart;

  assert(OII->first - KillingStart >= 0 && "Size expected to be positive");

  if (KillingStart <= DeadStart &&
      // Note: "DeadStart - KillingStart" is known to be non negative due to
      // preceding check.
      KillingSize > (uint64_t)(DeadStart - KillingStart)) {
    // Note: "KillingSize - (uint64_t)(DeadStart - DeadStart)" is known to
    // be positive due to preceding checks.
    assert(KillingSize - (uint64_t)(DeadStart - KillingStart) < DeadSize &&
           "Should have been handled as OW_Complete");
    if (tryToShorten(DeadI, DeadStart, DeadSize, KillingStart, KillingSize,
                     false)) {
      IntervalMap.erase(OII);
      return true;
    }
  }
  return false;
}

static Constant *
tryToMergePartialOverlappingStores(StoreInst *KillingI, StoreInst *DeadI,
                                   int64_t KillingOffset, int64_t DeadOffset,
                                   const DataLayout &DL, BatchAAResults &AA,
                                   DominatorTree *DT) {

  if (DeadI && isa<ConstantInt>(DeadI->getValueOperand()) &&
      DL.typeSizeEqualsStoreSize(DeadI->getValueOperand()->getType()) &&
      KillingI && isa<ConstantInt>(KillingI->getValueOperand()) &&
      DL.typeSizeEqualsStoreSize(KillingI->getValueOperand()->getType()) &&
      memoryIsNotModifiedBetween(DeadI, KillingI, AA, DL, DT)) {
    // If the store we find is:
    //   a) partially overwritten by the store to 'Loc'
    //   b) the killing store is fully contained in the dead one and
    //   c) they both have a constant value
    //   d) none of the two stores need padding
    // Merge the two stores, replacing the dead store's value with a
    // merge of both values.
    // TODO: Deal with other constant types (vectors, etc), and probably
    // some mem intrinsics (if needed)

    APInt DeadValue = cast<ConstantInt>(DeadI->getValueOperand())->getValue();
    APInt KillingValue =
        cast<ConstantInt>(KillingI->getValueOperand())->getValue();
    unsigned KillingBits = KillingValue.getBitWidth();
    assert(DeadValue.getBitWidth() > KillingValue.getBitWidth());
    KillingValue = KillingValue.zext(DeadValue.getBitWidth());

    // Offset of the smaller store inside the larger store
    unsigned BitOffsetDiff = (KillingOffset - DeadOffset) * 8;
    unsigned LShiftAmount =
        DL.isBigEndian() ? DeadValue.getBitWidth() - BitOffsetDiff - KillingBits
                         : BitOffsetDiff;
    APInt Mask = APInt::getBitsSet(DeadValue.getBitWidth(), LShiftAmount,
                                   LShiftAmount + KillingBits);
    // Clear the bits we'll be replacing, then OR with the smaller
    // store, shifted appropriately.
    APInt Merged = (DeadValue & ~Mask) | (KillingValue << LShiftAmount);
    LLVM_DEBUG(dbgs() << "DSE: Merge Stores:\n  Dead: " << *DeadI
                      << "\n  Killing: " << *KillingI
                      << "\n  Merged Value: " << Merged << '\n');
    return ConstantInt::get(DeadI->getValueOperand()->getType(), Merged);
  }
  return nullptr;
}

namespace {
// Returns true if \p I is an intrinsic that does not read or write memory.
bool isNoopIntrinsic(Instruction *I) {
  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::invariant_end:
    case Intrinsic::launder_invariant_group:
    case Intrinsic::assume:
      return true;
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_label:
    case Intrinsic::dbg_value:
      llvm_unreachable("Intrinsic should not be modeled in MemorySSA");
    default:
      return false;
    }
  }
  return false;
}

// Check if we can ignore \p D for DSE.
bool canSkipDef(MemoryDef *D, bool DefVisibleToCaller) {
  Instruction *DI = D->getMemoryInst();
  // Calls that only access inaccessible memory cannot read or write any memory
  // locations we consider for elimination.
  if (auto *CB = dyn_cast<CallBase>(DI))
    if (CB->onlyAccessesInaccessibleMemory())
      return true;

  // We can eliminate stores to locations not visible to the caller across
  // throwing instructions.
  if (DI->mayThrow() && !DefVisibleToCaller)
    return true;

  // We can remove the dead stores, irrespective of the fence and its ordering
  // (release/acquire/seq_cst). Fences only constraints the ordering of
  // already visible stores, it does not make a store visible to other
  // threads. So, skipping over a fence does not change a store from being
  // dead.
  if (isa<FenceInst>(DI))
    return true;

  // Skip intrinsics that do not really read or modify memory.
  if (isNoopIntrinsic(DI))
    return true;

  return false;
}

struct DSEState {
  Function &F;
  AliasAnalysis &AA;
  EarliestEscapeInfo EI;

  /// The single BatchAA instance that is used to cache AA queries. It will
  /// not be invalidated over the whole run. This is safe, because:
  /// 1. Only memory writes are removed, so the alias cache for memory
  ///    locations remains valid.
  /// 2. No new instructions are added (only instructions removed), so cached
  ///    information for a deleted value cannot be accessed by a re-used new
  ///    value pointer.
  BatchAAResults BatchAA;

  MemorySSA &MSSA;
  DominatorTree &DT;
  PostDominatorTree &PDT;
  const TargetLibraryInfo &TLI;
  const DataLayout &DL;
  const LoopInfo &LI;

  // Whether the function contains any irreducible control flow, useful for
  // being accurately able to detect loops.
  bool ContainsIrreducibleLoops;

  // All MemoryDefs that potentially could kill other MemDefs.
  SmallVector<MemoryDef *, 64> MemDefs;
  // Any that should be skipped as they are already deleted
  SmallPtrSet<MemoryAccess *, 4> SkipStores;
  // Keep track whether a given object is captured before return or not.
  DenseMap<const Value *, bool> CapturedBeforeReturn;
  // Keep track of all of the objects that are invisible to the caller after
  // the function returns.
  DenseMap<const Value *, bool> InvisibleToCallerAfterRet;
  // Keep track of blocks with throwing instructions not modeled in MemorySSA.
  SmallPtrSet<BasicBlock *, 16> ThrowingBlocks;
  // Post-order numbers for each basic block. Used to figure out if memory
  // accesses are executed before another access.
  DenseMap<BasicBlock *, unsigned> PostOrderNumbers;

  /// Keep track of instructions (partly) overlapping with killing MemoryDefs per
  /// basic block.
  MapVector<BasicBlock *, InstOverlapIntervalsTy> IOLs;
  // Check if there are root nodes that are terminated by UnreachableInst.
  // Those roots pessimize post-dominance queries. If there are such roots,
  // fall back to CFG scan starting from all non-unreachable roots.
  bool AnyUnreachableExit;

  // Whether or not we should iterate on removing dead stores at the end of the
  // function due to removing a store causing a previously captured pointer to
  // no longer be captured.
  bool ShouldIterateEndOfFunctionDSE;

  /// Dead instructions to be removed at the end of DSE.
  SmallVector<Instruction *> ToRemove;

  // Class contains self-reference, make sure it's not copied/moved.
  DSEState(const DSEState &) = delete;
  DSEState &operator=(const DSEState &) = delete;

  DSEState(Function &F, AliasAnalysis &AA, MemorySSA &MSSA, DominatorTree &DT,
           PostDominatorTree &PDT, const TargetLibraryInfo &TLI,
           const LoopInfo &LI)
      : F(F), AA(AA), EI(DT, &LI), BatchAA(AA, &EI), MSSA(MSSA), DT(DT),
        PDT(PDT), TLI(TLI), DL(F.getDataLayout()), LI(LI) {
    // Collect blocks with throwing instructions not modeled in MemorySSA and
    // alloc-like objects.
    unsigned PO = 0;
    for (BasicBlock *BB : post_order(&F)) {
      PostOrderNumbers[BB] = PO++;
      for (Instruction &I : *BB) {
        MemoryAccess *MA = MSSA.getMemoryAccess(&I);
        if (I.mayThrow() && !MA)
          ThrowingBlocks.insert(I.getParent());

        auto *MD = dyn_cast_or_null<MemoryDef>(MA);
        if (MD && MemDefs.size() < MemorySSADefsPerBlockLimit &&
            (getLocForWrite(&I) || isMemTerminatorInst(&I)))
          MemDefs.push_back(MD);
      }
    }

    // Treat byval or inalloca arguments the same as Allocas, stores to them are
    // dead at the end of the function.
    for (Argument &AI : F.args())
      if (AI.hasPassPointeeByValueCopyAttr())
        InvisibleToCallerAfterRet.insert({&AI, true});

    // Collect whether there is any irreducible control flow in the function.
    ContainsIrreducibleLoops = mayContainIrreducibleControl(F, &LI);

    AnyUnreachableExit = any_of(PDT.roots(), [](const BasicBlock *E) {
      return isa<UnreachableInst>(E->getTerminator());
    });
  }

  static void pushMemUses(MemoryAccess *Acc,
                          SmallVectorImpl<MemoryAccess *> &WorkList,
                          SmallPtrSetImpl<MemoryAccess *> &Visited) {
    for (Use &U : Acc->uses()) {
      auto *MA = cast<MemoryAccess>(U.getUser());
      if (Visited.insert(MA).second)
        WorkList.push_back(MA);
    }
  };

  LocationSize strengthenLocationSize(const Instruction *I,
                                      LocationSize Size) const {
    if (auto *CB = dyn_cast<CallBase>(I)) {
      LibFunc F;
      if (TLI.getLibFunc(*CB, F) && TLI.has(F) &&
          (F == LibFunc_memset_chk || F == LibFunc_memcpy_chk)) {
        // Use the precise location size specified by the 3rd argument
        // for determining KillingI overwrites DeadLoc if it is a memset_chk
        // instruction. memset_chk will write either the amount specified as 3rd
        // argument or the function will immediately abort and exit the program.
        // NOTE: AA may determine NoAlias if it can prove that the access size
        // is larger than the allocation size due to that being UB. To avoid
        // returning potentially invalid NoAlias results by AA, limit the use of
        // the precise location size to isOverwrite.
        if (const auto *Len = dyn_cast<ConstantInt>(CB->getArgOperand(2)))
          return LocationSize::precise(Len->getZExtValue());
      }
    }
    return Size;
  }

  /// Return 'OW_Complete' if a store to the 'KillingLoc' location (by \p
  /// KillingI instruction) completely overwrites a store to the 'DeadLoc'
  /// location (by \p DeadI instruction).
  /// Return OW_MaybePartial if \p KillingI does not completely overwrite
  /// \p DeadI, but they both write to the same underlying object. In that
  /// case, use isPartialOverwrite to check if \p KillingI partially overwrites
  /// \p DeadI. Returns 'OR_None' if \p KillingI is known to not overwrite the
  /// \p DeadI. Returns 'OW_Unknown' if nothing can be determined.
  OverwriteResult isOverwrite(const Instruction *KillingI,
                              const Instruction *DeadI,
                              const MemoryLocation &KillingLoc,
                              const MemoryLocation &DeadLoc,
                              int64_t &KillingOff, int64_t &DeadOff) {
    // AliasAnalysis does not always account for loops. Limit overwrite checks
    // to dependencies for which we can guarantee they are independent of any
    // loops they are in.
    if (!isGuaranteedLoopIndependent(DeadI, KillingI, DeadLoc))
      return OW_Unknown;

    LocationSize KillingLocSize =
        strengthenLocationSize(KillingI, KillingLoc.Size);
    const Value *DeadPtr = DeadLoc.Ptr->stripPointerCasts();
    const Value *KillingPtr = KillingLoc.Ptr->stripPointerCasts();
    const Value *DeadUndObj = getUnderlyingObject(DeadPtr);
    const Value *KillingUndObj = getUnderlyingObject(KillingPtr);

    // Check whether the killing store overwrites the whole object, in which
    // case the size/offset of the dead store does not matter.
    if (DeadUndObj == KillingUndObj && KillingLocSize.isPrecise() &&
        isIdentifiedObject(KillingUndObj)) {
      std::optional<TypeSize> KillingUndObjSize =
          getPointerSize(KillingUndObj, DL, TLI, &F);
      if (KillingUndObjSize && *KillingUndObjSize == KillingLocSize.getValue())
        return OW_Complete;
    }

    // FIXME: Vet that this works for size upper-bounds. Seems unlikely that we'll
    // get imprecise values here, though (except for unknown sizes).
    if (!KillingLocSize.isPrecise() || !DeadLoc.Size.isPrecise()) {
      // In case no constant size is known, try to an IR values for the number
      // of bytes written and check if they match.
      const auto *KillingMemI = dyn_cast<MemIntrinsic>(KillingI);
      const auto *DeadMemI = dyn_cast<MemIntrinsic>(DeadI);
      if (KillingMemI && DeadMemI) {
        const Value *KillingV = KillingMemI->getLength();
        const Value *DeadV = DeadMemI->getLength();
        if (KillingV == DeadV && BatchAA.isMustAlias(DeadLoc, KillingLoc))
          return OW_Complete;
      }

      // Masked stores have imprecise locations, but we can reason about them
      // to some extent.
      return isMaskedStoreOverwrite(KillingI, DeadI, BatchAA);
    }

    const TypeSize KillingSize = KillingLocSize.getValue();
    const TypeSize DeadSize = DeadLoc.Size.getValue();
    // Bail on doing Size comparison which depends on AA for now
    // TODO: Remove AnyScalable once Alias Analysis deal with scalable vectors
    const bool AnyScalable =
        DeadSize.isScalable() || KillingLocSize.isScalable();

    if (AnyScalable)
      return OW_Unknown;
    // Query the alias information
    AliasResult AAR = BatchAA.alias(KillingLoc, DeadLoc);

    // If the start pointers are the same, we just have to compare sizes to see if
    // the killing store was larger than the dead store.
    if (AAR == AliasResult::MustAlias) {
      // Make sure that the KillingSize size is >= the DeadSize size.
      if (KillingSize >= DeadSize)
        return OW_Complete;
    }

    // If we hit a partial alias we may have a full overwrite
    if (AAR == AliasResult::PartialAlias && AAR.hasOffset()) {
      int32_t Off = AAR.getOffset();
      if (Off >= 0 && (uint64_t)Off + DeadSize <= KillingSize)
        return OW_Complete;
    }

    // If we can't resolve the same pointers to the same object, then we can't
    // analyze them at all.
    if (DeadUndObj != KillingUndObj) {
      // Non aliasing stores to different objects don't overlap. Note that
      // if the killing store is known to overwrite whole object (out of
      // bounds access overwrites whole object as well) then it is assumed to
      // completely overwrite any store to the same object even if they don't
      // actually alias (see next check).
      if (AAR == AliasResult::NoAlias)
        return OW_None;
      return OW_Unknown;
    }

    // Okay, we have stores to two completely different pointers.  Try to
    // decompose the pointer into a "base + constant_offset" form.  If the base
    // pointers are equal, then we can reason about the two stores.
    DeadOff = 0;
    KillingOff = 0;
    const Value *DeadBasePtr =
        GetPointerBaseWithConstantOffset(DeadPtr, DeadOff, DL);
    const Value *KillingBasePtr =
        GetPointerBaseWithConstantOffset(KillingPtr, KillingOff, DL);

    // If the base pointers still differ, we have two completely different
    // stores.
    if (DeadBasePtr != KillingBasePtr)
      return OW_Unknown;

    // The killing access completely overlaps the dead store if and only if
    // both start and end of the dead one is "inside" the killing one:
    //    |<->|--dead--|<->|
    //    |-----killing------|
    // Accesses may overlap if and only if start of one of them is "inside"
    // another one:
    //    |<->|--dead--|<-------->|
    //    |-------killing--------|
    //           OR
    //    |-------dead-------|
    //    |<->|---killing---|<----->|
    //
    // We have to be careful here as *Off is signed while *.Size is unsigned.

    // Check if the dead access starts "not before" the killing one.
    if (DeadOff >= KillingOff) {
      // If the dead access ends "not after" the killing access then the
      // dead one is completely overwritten by the killing one.
      if (uint64_t(DeadOff - KillingOff) + DeadSize <= KillingSize)
        return OW_Complete;
      // If start of the dead access is "before" end of the killing access
      // then accesses overlap.
      else if ((uint64_t)(DeadOff - KillingOff) < KillingSize)
        return OW_MaybePartial;
    }
    // If start of the killing access is "before" end of the dead access then
    // accesses overlap.
    else if ((uint64_t)(KillingOff - DeadOff) < DeadSize) {
      return OW_MaybePartial;
    }

    // Can reach here only if accesses are known not to overlap.
    return OW_None;
  }

  bool isInvisibleToCallerAfterRet(const Value *V) {
    if (isa<AllocaInst>(V))
      return true;
    auto I = InvisibleToCallerAfterRet.insert({V, false});
    if (I.second) {
      if (!isInvisibleToCallerOnUnwind(V)) {
        I.first->second = false;
      } else if (isNoAliasCall(V)) {
        I.first->second = !PointerMayBeCaptured(V, true, false);
      }
    }
    return I.first->second;
  }

  bool isInvisibleToCallerOnUnwind(const Value *V) {
    bool RequiresNoCaptureBeforeUnwind;
    if (!isNotVisibleOnUnwind(V, RequiresNoCaptureBeforeUnwind))
      return false;
    if (!RequiresNoCaptureBeforeUnwind)
      return true;

    auto I = CapturedBeforeReturn.insert({V, true});
    if (I.second)
      // NOTE: This could be made more precise by PointerMayBeCapturedBefore
      // with the killing MemoryDef. But we refrain from doing so for now to
      // limit compile-time and this does not cause any changes to the number
      // of stores removed on a large test set in practice.
      I.first->second = PointerMayBeCaptured(V, false, true);
    return !I.first->second;
  }

  std::optional<MemoryLocation> getLocForWrite(Instruction *I) const {
    if (!I->mayWriteToMemory())
      return std::nullopt;

    if (auto *CB = dyn_cast<CallBase>(I))
      return MemoryLocation::getForDest(CB, TLI);

    return MemoryLocation::getOrNone(I);
  }

  /// Assuming this instruction has a dead analyzable write, can we delete
  /// this instruction?
  bool isRemovable(Instruction *I) {
    assert(getLocForWrite(I) && "Must have analyzable write");

    // Don't remove volatile/atomic stores.
    if (StoreInst *SI = dyn_cast<StoreInst>(I))
      return SI->isUnordered();

    if (auto *CB = dyn_cast<CallBase>(I)) {
      // Don't remove volatile memory intrinsics.
      if (auto *MI = dyn_cast<MemIntrinsic>(CB))
        return !MI->isVolatile();

      // Never remove dead lifetime intrinsics, e.g. because they are followed
      // by a free.
      if (CB->isLifetimeStartOrEnd())
        return false;

      return CB->use_empty() && CB->willReturn() && CB->doesNotThrow() &&
             !CB->isTerminator();
    }

    return false;
  }

  /// Returns true if \p UseInst completely overwrites \p DefLoc
  /// (stored by \p DefInst).
  bool isCompleteOverwrite(const MemoryLocation &DefLoc, Instruction *DefInst,
                           Instruction *UseInst) {
    // UseInst has a MemoryDef associated in MemorySSA. It's possible for a
    // MemoryDef to not write to memory, e.g. a volatile load is modeled as a
    // MemoryDef.
    if (!UseInst->mayWriteToMemory())
      return false;

    if (auto *CB = dyn_cast<CallBase>(UseInst))
      if (CB->onlyAccessesInaccessibleMemory())
        return false;

    int64_t InstWriteOffset, DepWriteOffset;
    if (auto CC = getLocForWrite(UseInst))
      return isOverwrite(UseInst, DefInst, *CC, DefLoc, InstWriteOffset,
                         DepWriteOffset) == OW_Complete;
    return false;
  }

  /// Returns true if \p Def is not read before returning from the function.
  bool isWriteAtEndOfFunction(MemoryDef *Def, const MemoryLocation &DefLoc) {
    LLVM_DEBUG(dbgs() << "  Check if def " << *Def << " ("
                      << *Def->getMemoryInst()
                      << ") is at the end the function \n");
    SmallVector<MemoryAccess *, 4> WorkList;
    SmallPtrSet<MemoryAccess *, 8> Visited;

    pushMemUses(Def, WorkList, Visited);
    for (unsigned I = 0; I < WorkList.size(); I++) {
      if (WorkList.size() >= MemorySSAScanLimit) {
        LLVM_DEBUG(dbgs() << "  ... hit exploration limit.\n");
        return false;
      }

      MemoryAccess *UseAccess = WorkList[I];
      if (isa<MemoryPhi>(UseAccess)) {
        // AliasAnalysis does not account for loops. Limit elimination to
        // candidates for which we can guarantee they always store to the same
        // memory location.
        if (!isGuaranteedLoopInvariant(DefLoc.Ptr))
          return false;

        pushMemUses(cast<MemoryPhi>(UseAccess), WorkList, Visited);
        continue;
      }
      // TODO: Checking for aliasing is expensive. Consider reducing the amount
      // of times this is called and/or caching it.
      Instruction *UseInst = cast<MemoryUseOrDef>(UseAccess)->getMemoryInst();
      if (isReadClobber(DefLoc, UseInst)) {
        LLVM_DEBUG(dbgs() << "  ... hit read clobber " << *UseInst << ".\n");
        return false;
      }

      if (MemoryDef *UseDef = dyn_cast<MemoryDef>(UseAccess))
        pushMemUses(UseDef, WorkList, Visited);
    }
    return true;
  }

  /// If \p I is a memory  terminator like llvm.lifetime.end or free, return a
  /// pair with the MemoryLocation terminated by \p I and a boolean flag
  /// indicating whether \p I is a free-like call.
  std::optional<std::pair<MemoryLocation, bool>>
  getLocForTerminator(Instruction *I) const {
    uint64_t Len;
    Value *Ptr;
    if (match(I, m_Intrinsic<Intrinsic::lifetime_end>(m_ConstantInt(Len),
                                                      m_Value(Ptr))))
      return {std::make_pair(MemoryLocation(Ptr, Len), false)};

    if (auto *CB = dyn_cast<CallBase>(I)) {
      if (Value *FreedOp = getFreedOperand(CB, &TLI))
        return {std::make_pair(MemoryLocation::getAfter(FreedOp), true)};
    }

    return std::nullopt;
  }

  /// Returns true if \p I is a memory terminator instruction like
  /// llvm.lifetime.end or free.
  bool isMemTerminatorInst(Instruction *I) const {
    auto *CB = dyn_cast<CallBase>(I);
    return CB && (CB->getIntrinsicID() == Intrinsic::lifetime_end ||
                  getFreedOperand(CB, &TLI) != nullptr);
  }

  /// Returns true if \p MaybeTerm is a memory terminator for \p Loc from
  /// instruction \p AccessI.
  bool isMemTerminator(const MemoryLocation &Loc, Instruction *AccessI,
                       Instruction *MaybeTerm) {
    std::optional<std::pair<MemoryLocation, bool>> MaybeTermLoc =
        getLocForTerminator(MaybeTerm);

    if (!MaybeTermLoc)
      return false;

    // If the terminator is a free-like call, all accesses to the underlying
    // object can be considered terminated.
    if (getUnderlyingObject(Loc.Ptr) !=
        getUnderlyingObject(MaybeTermLoc->first.Ptr))
      return false;

    auto TermLoc = MaybeTermLoc->first;
    if (MaybeTermLoc->second) {
      const Value *LocUO = getUnderlyingObject(Loc.Ptr);
      return BatchAA.isMustAlias(TermLoc.Ptr, LocUO);
    }
    int64_t InstWriteOffset = 0;
    int64_t DepWriteOffset = 0;
    return isOverwrite(MaybeTerm, AccessI, TermLoc, Loc, InstWriteOffset,
                       DepWriteOffset) == OW_Complete;
  }

  // Returns true if \p Use may read from \p DefLoc.
  bool isReadClobber(const MemoryLocation &DefLoc, Instruction *UseInst) {
    if (isNoopIntrinsic(UseInst))
      return false;

    // Monotonic or weaker atomic stores can be re-ordered and do not need to be
    // treated as read clobber.
    if (auto SI = dyn_cast<StoreInst>(UseInst))
      return isStrongerThan(SI->getOrdering(), AtomicOrdering::Monotonic);

    if (!UseInst->mayReadFromMemory())
      return false;

    if (auto *CB = dyn_cast<CallBase>(UseInst))
      if (CB->onlyAccessesInaccessibleMemory())
        return false;

    return isRefSet(BatchAA.getModRefInfo(UseInst, DefLoc));
  }

  /// Returns true if a dependency between \p Current and \p KillingDef is
  /// guaranteed to be loop invariant for the loops that they are in. Either
  /// because they are known to be in the same block, in the same loop level or
  /// by guaranteeing that \p CurrentLoc only references a single MemoryLocation
  /// during execution of the containing function.
  bool isGuaranteedLoopIndependent(const Instruction *Current,
                                   const Instruction *KillingDef,
                                   const MemoryLocation &CurrentLoc) {
    // If the dependency is within the same block or loop level (being careful
    // of irreducible loops), we know that AA will return a valid result for the
    // memory dependency. (Both at the function level, outside of any loop,
    // would also be valid but we currently disable that to limit compile time).
    if (Current->getParent() == KillingDef->getParent())
      return true;
    const Loop *CurrentLI = LI.getLoopFor(Current->getParent());
    if (!ContainsIrreducibleLoops && CurrentLI &&
        CurrentLI == LI.getLoopFor(KillingDef->getParent()))
      return true;
    // Otherwise check the memory location is invariant to any loops.
    return isGuaranteedLoopInvariant(CurrentLoc.Ptr);
  }

  /// Returns true if \p Ptr is guaranteed to be loop invariant for any possible
  /// loop. In particular, this guarantees that it only references a single
  /// MemoryLocation during execution of the containing function.
  bool isGuaranteedLoopInvariant(const Value *Ptr) {
    Ptr = Ptr->stripPointerCasts();
    if (auto *GEP = dyn_cast<GEPOperator>(Ptr))
      if (GEP->hasAllConstantIndices())
        Ptr = GEP->getPointerOperand()->stripPointerCasts();

    if (auto *I = dyn_cast<Instruction>(Ptr)) {
      return I->getParent()->isEntryBlock() ||
             (!ContainsIrreducibleLoops && !LI.getLoopFor(I->getParent()));
    }
    return true;
  }

  // Find a MemoryDef writing to \p KillingLoc and dominating \p StartAccess,
  // with no read access between them or on any other path to a function exit
  // block if \p KillingLoc is not accessible after the function returns. If
  // there is no such MemoryDef, return std::nullopt. The returned value may not
  // (completely) overwrite \p KillingLoc. Currently we bail out when we
  // encounter an aliasing MemoryUse (read).
  std::optional<MemoryAccess *>
  getDomMemoryDef(MemoryDef *KillingDef, MemoryAccess *StartAccess,
                  const MemoryLocation &KillingLoc, const Value *KillingUndObj,
                  unsigned &ScanLimit, unsigned &WalkerStepLimit,
                  bool IsMemTerm, unsigned &PartialLimit) {
    if (ScanLimit == 0 || WalkerStepLimit == 0) {
      LLVM_DEBUG(dbgs() << "\n    ...  hit scan limit\n");
      return std::nullopt;
    }

    MemoryAccess *Current = StartAccess;
    Instruction *KillingI = KillingDef->getMemoryInst();
    LLVM_DEBUG(dbgs() << "  trying to get dominating access\n");

    // Only optimize defining access of KillingDef when directly starting at its
    // defining access. The defining access also must only access KillingLoc. At
    // the moment we only support instructions with a single write location, so
    // it should be sufficient to disable optimizations for instructions that
    // also read from memory.
    bool CanOptimize = OptimizeMemorySSA &&
                       KillingDef->getDefiningAccess() == StartAccess &&
                       !KillingI->mayReadFromMemory();

    // Find the next clobbering Mod access for DefLoc, starting at StartAccess.
    std::optional<MemoryLocation> CurrentLoc;
    for (;; Current = cast<MemoryDef>(Current)->getDefiningAccess()) {
      LLVM_DEBUG({
        dbgs() << "   visiting " << *Current;
        if (!MSSA.isLiveOnEntryDef(Current) && isa<MemoryUseOrDef>(Current))
          dbgs() << " (" << *cast<MemoryUseOrDef>(Current)->getMemoryInst()
                 << ")";
        dbgs() << "\n";
      });

      // Reached TOP.
      if (MSSA.isLiveOnEntryDef(Current)) {
        LLVM_DEBUG(dbgs() << "   ...  found LiveOnEntryDef\n");
        if (CanOptimize && Current != KillingDef->getDefiningAccess())
          // The first clobbering def is... none.
          KillingDef->setOptimized(Current);
        return std::nullopt;
      }

      // Cost of a step. Accesses in the same block are more likely to be valid
      // candidates for elimination, hence consider them cheaper.
      unsigned StepCost = KillingDef->getBlock() == Current->getBlock()
                              ? MemorySSASameBBStepCost
                              : MemorySSAOtherBBStepCost;
      if (WalkerStepLimit <= StepCost) {
        LLVM_DEBUG(dbgs() << "   ...  hit walker step limit\n");
        return std::nullopt;
      }
      WalkerStepLimit -= StepCost;

      // Return for MemoryPhis. They cannot be eliminated directly and the
      // caller is responsible for traversing them.
      if (isa<MemoryPhi>(Current)) {
        LLVM_DEBUG(dbgs() << "   ...  found MemoryPhi\n");
        return Current;
      }

      // Below, check if CurrentDef is a valid candidate to be eliminated by
      // KillingDef. If it is not, check the next candidate.
      MemoryDef *CurrentDef = cast<MemoryDef>(Current);
      Instruction *CurrentI = CurrentDef->getMemoryInst();

      if (canSkipDef(CurrentDef, !isInvisibleToCallerOnUnwind(KillingUndObj))) {
        CanOptimize = false;
        continue;
      }

      // Before we try to remove anything, check for any extra throwing
      // instructions that block us from DSEing
      if (mayThrowBetween(KillingI, CurrentI, KillingUndObj)) {
        LLVM_DEBUG(dbgs() << "  ... skip, may throw!\n");
        return std::nullopt;
      }

      // Check for anything that looks like it will be a barrier to further
      // removal
      if (isDSEBarrier(KillingUndObj, CurrentI)) {
        LLVM_DEBUG(dbgs() << "  ... skip, barrier\n");
        return std::nullopt;
      }

      // If Current is known to be on path that reads DefLoc or is a read
      // clobber, bail out, as the path is not profitable. We skip this check
      // for intrinsic calls, because the code knows how to handle memcpy
      // intrinsics.
      if (!isa<IntrinsicInst>(CurrentI) && isReadClobber(KillingLoc, CurrentI))
        return std::nullopt;

      // Quick check if there are direct uses that are read-clobbers.
      if (any_of(Current->uses(), [this, &KillingLoc, StartAccess](Use &U) {
            if (auto *UseOrDef = dyn_cast<MemoryUseOrDef>(U.getUser()))
              return !MSSA.dominates(StartAccess, UseOrDef) &&
                     isReadClobber(KillingLoc, UseOrDef->getMemoryInst());
            return false;
          })) {
        LLVM_DEBUG(dbgs() << "   ...  found a read clobber\n");
        return std::nullopt;
      }

      // If Current does not have an analyzable write location or is not
      // removable, skip it.
      CurrentLoc = getLocForWrite(CurrentI);
      if (!CurrentLoc || !isRemovable(CurrentI)) {
        CanOptimize = false;
        continue;
      }

      // AliasAnalysis does not account for loops. Limit elimination to
      // candidates for which we can guarantee they always store to the same
      // memory location and not located in different loops.
      if (!isGuaranteedLoopIndependent(CurrentI, KillingI, *CurrentLoc)) {
        LLVM_DEBUG(dbgs() << "  ... not guaranteed loop independent\n");
        CanOptimize = false;
        continue;
      }

      if (IsMemTerm) {
        // If the killing def is a memory terminator (e.g. lifetime.end), check
        // the next candidate if the current Current does not write the same
        // underlying object as the terminator.
        if (!isMemTerminator(*CurrentLoc, CurrentI, KillingI)) {
          CanOptimize = false;
          continue;
        }
      } else {
        int64_t KillingOffset = 0;
        int64_t DeadOffset = 0;
        auto OR = isOverwrite(KillingI, CurrentI, KillingLoc, *CurrentLoc,
                              KillingOffset, DeadOffset);
        if (CanOptimize) {
          // CurrentDef is the earliest write clobber of KillingDef. Use it as
          // optimized access. Do not optimize if CurrentDef is already the
          // defining access of KillingDef.
          if (CurrentDef != KillingDef->getDefiningAccess() &&
              (OR == OW_Complete || OR == OW_MaybePartial))
            KillingDef->setOptimized(CurrentDef);

          // Once a may-aliasing def is encountered do not set an optimized
          // access.
          if (OR != OW_None)
            CanOptimize = false;
        }

        // If Current does not write to the same object as KillingDef, check
        // the next candidate.
        if (OR == OW_Unknown || OR == OW_None)
          continue;
        else if (OR == OW_MaybePartial) {
          // If KillingDef only partially overwrites Current, check the next
          // candidate if the partial step limit is exceeded. This aggressively
          // limits the number of candidates for partial store elimination,
          // which are less likely to be removable in the end.
          if (PartialLimit <= 1) {
            WalkerStepLimit -= 1;
            LLVM_DEBUG(dbgs() << "   ... reached partial limit ... continue with next access\n");
            continue;
          }
          PartialLimit -= 1;
        }
      }
      break;
    };

    // Accesses to objects accessible after the function returns can only be
    // eliminated if the access is dead along all paths to the exit. Collect
    // the blocks with killing (=completely overwriting MemoryDefs) and check if
    // they cover all paths from MaybeDeadAccess to any function exit.
    SmallPtrSet<Instruction *, 16> KillingDefs;
    KillingDefs.insert(KillingDef->getMemoryInst());
    MemoryAccess *MaybeDeadAccess = Current;
    MemoryLocation MaybeDeadLoc = *CurrentLoc;
    Instruction *MaybeDeadI = cast<MemoryDef>(MaybeDeadAccess)->getMemoryInst();
    LLVM_DEBUG(dbgs() << "  Checking for reads of " << *MaybeDeadAccess << " ("
                      << *MaybeDeadI << ")\n");

    SmallVector<MemoryAccess *, 32> WorkList;
    SmallPtrSet<MemoryAccess *, 32> Visited;
    pushMemUses(MaybeDeadAccess, WorkList, Visited);

    // Check if DeadDef may be read.
    for (unsigned I = 0; I < WorkList.size(); I++) {
      MemoryAccess *UseAccess = WorkList[I];

      LLVM_DEBUG(dbgs() << "   " << *UseAccess);
      // Bail out if the number of accesses to check exceeds the scan limit.
      if (ScanLimit < (WorkList.size() - I)) {
        LLVM_DEBUG(dbgs() << "\n    ...  hit scan limit\n");
        return std::nullopt;
      }
      --ScanLimit;
      NumDomMemDefChecks++;

      if (isa<MemoryPhi>(UseAccess)) {
        if (any_of(KillingDefs, [this, UseAccess](Instruction *KI) {
              return DT.properlyDominates(KI->getParent(),
                                          UseAccess->getBlock());
            })) {
          LLVM_DEBUG(dbgs() << " ... skipping, dominated by killing block\n");
          continue;
        }
        LLVM_DEBUG(dbgs() << "\n    ... adding PHI uses\n");
        pushMemUses(UseAccess, WorkList, Visited);
        continue;
      }

      Instruction *UseInst = cast<MemoryUseOrDef>(UseAccess)->getMemoryInst();
      LLVM_DEBUG(dbgs() << " (" << *UseInst << ")\n");

      if (any_of(KillingDefs, [this, UseInst](Instruction *KI) {
            return DT.dominates(KI, UseInst);
          })) {
        LLVM_DEBUG(dbgs() << " ... skipping, dominated by killing def\n");
        continue;
      }

      // A memory terminator kills all preceeding MemoryDefs and all succeeding
      // MemoryAccesses. We do not have to check it's users.
      if (isMemTerminator(MaybeDeadLoc, MaybeDeadI, UseInst)) {
        LLVM_DEBUG(
            dbgs()
            << " ... skipping, memterminator invalidates following accesses\n");
        continue;
      }

      if (isNoopIntrinsic(cast<MemoryUseOrDef>(UseAccess)->getMemoryInst())) {
        LLVM_DEBUG(dbgs() << "    ... adding uses of intrinsic\n");
        pushMemUses(UseAccess, WorkList, Visited);
        continue;
      }

      if (UseInst->mayThrow() && !isInvisibleToCallerOnUnwind(KillingUndObj)) {
        LLVM_DEBUG(dbgs() << "  ... found throwing instruction\n");
        return std::nullopt;
      }

      // Uses which may read the original MemoryDef mean we cannot eliminate the
      // original MD. Stop walk.
      if (isReadClobber(MaybeDeadLoc, UseInst)) {
        LLVM_DEBUG(dbgs() << "    ... found read clobber\n");
        return std::nullopt;
      }

      // If this worklist walks back to the original memory access (and the
      // pointer is not guarenteed loop invariant) then we cannot assume that a
      // store kills itself.
      if (MaybeDeadAccess == UseAccess &&
          !isGuaranteedLoopInvariant(MaybeDeadLoc.Ptr)) {
        LLVM_DEBUG(dbgs() << "    ... found not loop invariant self access\n");
        return std::nullopt;
      }
      // Otherwise, for the KillingDef and MaybeDeadAccess we only have to check
      // if it reads the memory location.
      // TODO: It would probably be better to check for self-reads before
      // calling the function.
      if (KillingDef == UseAccess || MaybeDeadAccess == UseAccess) {
        LLVM_DEBUG(dbgs() << "    ... skipping killing def/dom access\n");
        continue;
      }

      // Check all uses for MemoryDefs, except for defs completely overwriting
      // the original location. Otherwise we have to check uses of *all*
      // MemoryDefs we discover, including non-aliasing ones. Otherwise we might
      // miss cases like the following
      //   1 = Def(LoE) ; <----- DeadDef stores [0,1]
      //   2 = Def(1)   ; (2, 1) = NoAlias,   stores [2,3]
      //   Use(2)       ; MayAlias 2 *and* 1, loads [0, 3].
      //                  (The Use points to the *first* Def it may alias)
      //   3 = Def(1)   ; <---- Current  (3, 2) = NoAlias, (3,1) = MayAlias,
      //                  stores [0,1]
      if (MemoryDef *UseDef = dyn_cast<MemoryDef>(UseAccess)) {
        if (isCompleteOverwrite(MaybeDeadLoc, MaybeDeadI, UseInst)) {
          BasicBlock *MaybeKillingBlock = UseInst->getParent();
          if (PostOrderNumbers.find(MaybeKillingBlock)->second <
              PostOrderNumbers.find(MaybeDeadAccess->getBlock())->second) {
            if (!isInvisibleToCallerAfterRet(KillingUndObj)) {
              LLVM_DEBUG(dbgs()
                         << "    ... found killing def " << *UseInst << "\n");
              KillingDefs.insert(UseInst);
            }
          } else {
            LLVM_DEBUG(dbgs()
                       << "    ... found preceeding def " << *UseInst << "\n");
            return std::nullopt;
          }
        } else
          pushMemUses(UseDef, WorkList, Visited);
      }
    }

    // For accesses to locations visible after the function returns, make sure
    // that the location is dead (=overwritten) along all paths from
    // MaybeDeadAccess to the exit.
    if (!isInvisibleToCallerAfterRet(KillingUndObj)) {
      SmallPtrSet<BasicBlock *, 16> KillingBlocks;
      for (Instruction *KD : KillingDefs)
        KillingBlocks.insert(KD->getParent());
      assert(!KillingBlocks.empty() &&
             "Expected at least a single killing block");

      // Find the common post-dominator of all killing blocks.
      BasicBlock *CommonPred = *KillingBlocks.begin();
      for (BasicBlock *BB : llvm::drop_begin(KillingBlocks)) {
        if (!CommonPred)
          break;
        CommonPred = PDT.findNearestCommonDominator(CommonPred, BB);
      }

      // If the common post-dominator does not post-dominate MaybeDeadAccess,
      // there is a path from MaybeDeadAccess to an exit not going through a
      // killing block.
      if (!PDT.dominates(CommonPred, MaybeDeadAccess->getBlock())) {
        if (!AnyUnreachableExit)
          return std::nullopt;

        // Fall back to CFG scan starting at all non-unreachable roots if not
        // all paths to the exit go through CommonPred.
        CommonPred = nullptr;
      }

      // If CommonPred itself is in the set of killing blocks, we're done.
      if (KillingBlocks.count(CommonPred))
        return {MaybeDeadAccess};

      SetVector<BasicBlock *> WorkList;
      // If CommonPred is null, there are multiple exits from the function.
      // They all have to be added to the worklist.
      if (CommonPred)
        WorkList.insert(CommonPred);
      else
        for (BasicBlock *R : PDT.roots()) {
          if (!isa<UnreachableInst>(R->getTerminator()))
            WorkList.insert(R);
        }

      NumCFGTries++;
      // Check if all paths starting from an exit node go through one of the
      // killing blocks before reaching MaybeDeadAccess.
      for (unsigned I = 0; I < WorkList.size(); I++) {
        NumCFGChecks++;
        BasicBlock *Current = WorkList[I];
        if (KillingBlocks.count(Current))
          continue;
        if (Current == MaybeDeadAccess->getBlock())
          return std::nullopt;

        // MaybeDeadAccess is reachable from the entry, so we don't have to
        // explore unreachable blocks further.
        if (!DT.isReachableFromEntry(Current))
          continue;

        for (BasicBlock *Pred : predecessors(Current))
          WorkList.insert(Pred);

        if (WorkList.size() >= MemorySSAPathCheckLimit)
          return std::nullopt;
      }
      NumCFGSuccess++;
    }

    // No aliasing MemoryUses of MaybeDeadAccess found, MaybeDeadAccess is
    // potentially dead.
    return {MaybeDeadAccess};
  }

  /// Delete dead memory defs and recursively add their operands to ToRemove if
  /// they became dead.
  void
  deleteDeadInstruction(Instruction *SI,
                        SmallPtrSetImpl<MemoryAccess *> *Deleted = nullptr) {
    MemorySSAUpdater Updater(&MSSA);
    SmallVector<Instruction *, 32> NowDeadInsts;
    NowDeadInsts.push_back(SI);
    --NumFastOther;

    while (!NowDeadInsts.empty()) {
      Instruction *DeadInst = NowDeadInsts.pop_back_val();
      ++NumFastOther;

      // Try to preserve debug information attached to the dead instruction.
      salvageDebugInfo(*DeadInst);
      salvageKnowledge(DeadInst);

      // Remove the Instruction from MSSA.
      MemoryAccess *MA = MSSA.getMemoryAccess(DeadInst);
      bool IsMemDef = MA && isa<MemoryDef>(MA);
      if (MA) {
        if (IsMemDef) {
          auto *MD = cast<MemoryDef>(MA);
          SkipStores.insert(MD);
          if (Deleted)
            Deleted->insert(MD);
          if (auto *SI = dyn_cast<StoreInst>(MD->getMemoryInst())) {
            if (SI->getValueOperand()->getType()->isPointerTy()) {
              const Value *UO = getUnderlyingObject(SI->getValueOperand());
              if (CapturedBeforeReturn.erase(UO))
                ShouldIterateEndOfFunctionDSE = true;
              InvisibleToCallerAfterRet.erase(UO);
            }
          }
        }

        Updater.removeMemoryAccess(MA);
      }

      auto I = IOLs.find(DeadInst->getParent());
      if (I != IOLs.end())
        I->second.erase(DeadInst);
      // Remove its operands
      for (Use &O : DeadInst->operands())
        if (Instruction *OpI = dyn_cast<Instruction>(O)) {
          O.set(PoisonValue::get(O->getType()));
          if (isInstructionTriviallyDead(OpI, &TLI))
            NowDeadInsts.push_back(OpI);
        }

      EI.removeInstruction(DeadInst);
      // Remove memory defs directly if they don't produce results, but only
      // queue other dead instructions for later removal. They may have been
      // used as memory locations that have been cached by BatchAA. Removing
      // them here may lead to newly created instructions to be allocated at the
      // same address, yielding stale cache entries.
      if (IsMemDef && DeadInst->getType()->isVoidTy())
        DeadInst->eraseFromParent();
      else
        ToRemove.push_back(DeadInst);
    }
  }

  // Check for any extra throws between \p KillingI and \p DeadI that block
  // DSE.  This only checks extra maythrows (those that aren't MemoryDef's).
  // MemoryDef that may throw are handled during the walk from one def to the
  // next.
  bool mayThrowBetween(Instruction *KillingI, Instruction *DeadI,
                       const Value *KillingUndObj) {
    // First see if we can ignore it by using the fact that KillingI is an
    // alloca/alloca like object that is not visible to the caller during
    // execution of the function.
    if (KillingUndObj && isInvisibleToCallerOnUnwind(KillingUndObj))
      return false;

    if (KillingI->getParent() == DeadI->getParent())
      return ThrowingBlocks.count(KillingI->getParent());
    return !ThrowingBlocks.empty();
  }

  // Check if \p DeadI acts as a DSE barrier for \p KillingI. The following
  // instructions act as barriers:
  //  * A memory instruction that may throw and \p KillingI accesses a non-stack
  //  object.
  //  * Atomic stores stronger that monotonic.
  bool isDSEBarrier(const Value *KillingUndObj, Instruction *DeadI) {
    // If DeadI may throw it acts as a barrier, unless we are to an
    // alloca/alloca like object that does not escape.
    if (DeadI->mayThrow() && !isInvisibleToCallerOnUnwind(KillingUndObj))
      return true;

    // If DeadI is an atomic load/store stronger than monotonic, do not try to
    // eliminate/reorder it.
    if (DeadI->isAtomic()) {
      if (auto *LI = dyn_cast<LoadInst>(DeadI))
        return isStrongerThanMonotonic(LI->getOrdering());
      if (auto *SI = dyn_cast<StoreInst>(DeadI))
        return isStrongerThanMonotonic(SI->getOrdering());
      if (auto *ARMW = dyn_cast<AtomicRMWInst>(DeadI))
        return isStrongerThanMonotonic(ARMW->getOrdering());
      if (auto *CmpXchg = dyn_cast<AtomicCmpXchgInst>(DeadI))
        return isStrongerThanMonotonic(CmpXchg->getSuccessOrdering()) ||
               isStrongerThanMonotonic(CmpXchg->getFailureOrdering());
      llvm_unreachable("other instructions should be skipped in MemorySSA");
    }
    return false;
  }

  /// Eliminate writes to objects that are not visible in the caller and are not
  /// accessed before returning from the function.
  bool eliminateDeadWritesAtEndOfFunction() {
    bool MadeChange = false;
    LLVM_DEBUG(
        dbgs()
        << "Trying to eliminate MemoryDefs at the end of the function\n");
    do {
      ShouldIterateEndOfFunctionDSE = false;
      for (MemoryDef *Def : llvm::reverse(MemDefs)) {
        if (SkipStores.contains(Def))
          continue;

        Instruction *DefI = Def->getMemoryInst();
        auto DefLoc = getLocForWrite(DefI);
        if (!DefLoc || !isRemovable(DefI)) {
          LLVM_DEBUG(dbgs() << "  ... could not get location for write or "
                               "instruction not removable.\n");
          continue;
        }

        // NOTE: Currently eliminating writes at the end of a function is
        // limited to MemoryDefs with a single underlying object, to save
        // compile-time. In practice it appears the case with multiple
        // underlying objects is very uncommon. If it turns out to be important,
        // we can use getUnderlyingObjects here instead.
        const Value *UO = getUnderlyingObject(DefLoc->Ptr);
        if (!isInvisibleToCallerAfterRet(UO))
          continue;

        if (isWriteAtEndOfFunction(Def, *DefLoc)) {
          // See through pointer-to-pointer bitcasts
          LLVM_DEBUG(dbgs() << "   ... MemoryDef is not accessed until the end "
                               "of the function\n");
          deleteDeadInstruction(DefI);
          ++NumFastStores;
          MadeChange = true;
        }
      }
    } while (ShouldIterateEndOfFunctionDSE);
    return MadeChange;
  }

  /// If we have a zero initializing memset following a call to malloc,
  /// try folding it into a call to calloc.
  bool tryFoldIntoCalloc(MemoryDef *Def, const Value *DefUO) {
    Instruction *DefI = Def->getMemoryInst();
    MemSetInst *MemSet = dyn_cast<MemSetInst>(DefI);
    if (!MemSet)
      // TODO: Could handle zero store to small allocation as well.
      return false;
    Constant *StoredConstant = dyn_cast<Constant>(MemSet->getValue());
    if (!StoredConstant || !StoredConstant->isNullValue())
      return false;

    if (!isRemovable(DefI))
      // The memset might be volatile..
      return false;

    if (F.hasFnAttribute(Attribute::SanitizeMemory) ||
        F.hasFnAttribute(Attribute::SanitizeAddress) ||
        F.hasFnAttribute(Attribute::SanitizeHWAddress) ||
        F.getName() == "calloc")
      return false;
    auto *Malloc = const_cast<CallInst *>(dyn_cast<CallInst>(DefUO));
    if (!Malloc)
      return false;
    auto *InnerCallee = Malloc->getCalledFunction();
    if (!InnerCallee)
      return false;
    LibFunc Func;
    if (!TLI.getLibFunc(*InnerCallee, Func) || !TLI.has(Func) ||
        Func != LibFunc_malloc)
      return false;
    // Gracefully handle malloc with unexpected memory attributes.
    auto *MallocDef = dyn_cast_or_null<MemoryDef>(MSSA.getMemoryAccess(Malloc));
    if (!MallocDef)
      return false;

    auto shouldCreateCalloc = [](CallInst *Malloc, CallInst *Memset) {
      // Check for br(icmp ptr, null), truebb, falsebb) pattern at the end
      // of malloc block
      auto *MallocBB = Malloc->getParent(),
        *MemsetBB = Memset->getParent();
      if (MallocBB == MemsetBB)
        return true;
      auto *Ptr = Memset->getArgOperand(0);
      auto *TI = MallocBB->getTerminator();
      ICmpInst::Predicate Pred;
      BasicBlock *TrueBB, *FalseBB;
      if (!match(TI, m_Br(m_ICmp(Pred, m_Specific(Ptr), m_Zero()), TrueBB,
                          FalseBB)))
        return false;
      if (Pred != ICmpInst::ICMP_EQ || MemsetBB != FalseBB)
        return false;
      return true;
    };

    if (Malloc->getOperand(0) != MemSet->getLength())
      return false;
    if (!shouldCreateCalloc(Malloc, MemSet) ||
        !DT.dominates(Malloc, MemSet) ||
        !memoryIsNotModifiedBetween(Malloc, MemSet, BatchAA, DL, &DT))
      return false;
    IRBuilder<> IRB(Malloc);
    Type *SizeTTy = Malloc->getArgOperand(0)->getType();
    auto *Calloc = emitCalloc(ConstantInt::get(SizeTTy, 1),
                              Malloc->getArgOperand(0), IRB, TLI);
    if (!Calloc)
      return false;

    MemorySSAUpdater Updater(&MSSA);
    auto *NewAccess =
      Updater.createMemoryAccessAfter(cast<Instruction>(Calloc), nullptr,
                                      MallocDef);
    auto *NewAccessMD = cast<MemoryDef>(NewAccess);
    Updater.insertDef(NewAccessMD, /*RenameUses=*/true);
    Malloc->replaceAllUsesWith(Calloc);
    deleteDeadInstruction(Malloc);
    return true;
  }

  // Check if there is a dominating condition, that implies that the value
  // being stored in a ptr is already present in the ptr.
  bool dominatingConditionImpliesValue(MemoryDef *Def) {
    auto *StoreI = cast<StoreInst>(Def->getMemoryInst());
    BasicBlock *StoreBB = StoreI->getParent();
    Value *StorePtr = StoreI->getPointerOperand();
    Value *StoreVal = StoreI->getValueOperand();

    DomTreeNode *IDom = DT.getNode(StoreBB)->getIDom();
    if (!IDom)
      return false;

    auto *BI = dyn_cast<BranchInst>(IDom->getBlock()->getTerminator());
    if (!BI || !BI->isConditional())
      return false;

    // In case both blocks are the same, it is not possible to determine
    // if optimization is possible. (We would not want to optimize a store
    // in the FalseBB if condition is true and vice versa.)
    if (BI->getSuccessor(0) == BI->getSuccessor(1))
      return false;

    Instruction *ICmpL;
    ICmpInst::Predicate Pred;
    if (!match(BI->getCondition(),
               m_c_ICmp(Pred,
                        m_CombineAnd(m_Load(m_Specific(StorePtr)),
                                     m_Instruction(ICmpL)),
                        m_Specific(StoreVal))) ||
        !ICmpInst::isEquality(Pred))
      return false;

    // In case the else blocks also branches to the if block or the other way
    // around it is not possible to determine if the optimization is possible.
    if (Pred == ICmpInst::ICMP_EQ &&
        !DT.dominates(BasicBlockEdge(BI->getParent(), BI->getSuccessor(0)),
                      StoreBB))
      return false;

    if (Pred == ICmpInst::ICMP_NE &&
        !DT.dominates(BasicBlockEdge(BI->getParent(), BI->getSuccessor(1)),
                      StoreBB))
      return false;

    MemoryAccess *LoadAcc = MSSA.getMemoryAccess(ICmpL);
    MemoryAccess *ClobAcc =
        MSSA.getSkipSelfWalker()->getClobberingMemoryAccess(Def, BatchAA);

    return MSSA.dominates(ClobAcc, LoadAcc);
  }

  /// \returns true if \p Def is a no-op store, either because it
  /// directly stores back a loaded value or stores zero to a calloced object.
  bool storeIsNoop(MemoryDef *Def, const Value *DefUO) {
    Instruction *DefI = Def->getMemoryInst();
    StoreInst *Store = dyn_cast<StoreInst>(DefI);
    MemSetInst *MemSet = dyn_cast<MemSetInst>(DefI);
    Constant *StoredConstant = nullptr;
    if (Store)
      StoredConstant = dyn_cast<Constant>(Store->getOperand(0));
    else if (MemSet)
      StoredConstant = dyn_cast<Constant>(MemSet->getValue());
    else
      return false;

    if (!isRemovable(DefI))
      return false;

    if (StoredConstant) {
      Constant *InitC =
          getInitialValueOfAllocation(DefUO, &TLI, StoredConstant->getType());
      // If the clobbering access is LiveOnEntry, no instructions between them
      // can modify the memory location.
      if (InitC && InitC == StoredConstant)
        return MSSA.isLiveOnEntryDef(
            MSSA.getSkipSelfWalker()->getClobberingMemoryAccess(Def, BatchAA));
    }

    if (!Store)
      return false;

    if (dominatingConditionImpliesValue(Def))
      return true;

    if (auto *LoadI = dyn_cast<LoadInst>(Store->getOperand(0))) {
      if (LoadI->getPointerOperand() == Store->getOperand(1)) {
        // Get the defining access for the load.
        auto *LoadAccess = MSSA.getMemoryAccess(LoadI)->getDefiningAccess();
        // Fast path: the defining accesses are the same.
        if (LoadAccess == Def->getDefiningAccess())
          return true;

        // Look through phi accesses. Recursively scan all phi accesses by
        // adding them to a worklist. Bail when we run into a memory def that
        // does not match LoadAccess.
        SetVector<MemoryAccess *> ToCheck;
        MemoryAccess *Current =
            MSSA.getWalker()->getClobberingMemoryAccess(Def, BatchAA);
        // We don't want to bail when we run into the store memory def. But,
        // the phi access may point to it. So, pretend like we've already
        // checked it.
        ToCheck.insert(Def);
        ToCheck.insert(Current);
        // Start at current (1) to simulate already having checked Def.
        for (unsigned I = 1; I < ToCheck.size(); ++I) {
          Current = ToCheck[I];
          if (auto PhiAccess = dyn_cast<MemoryPhi>(Current)) {
            // Check all the operands.
            for (auto &Use : PhiAccess->incoming_values())
              ToCheck.insert(cast<MemoryAccess>(&Use));
            continue;
          }

          // If we found a memory def, bail. This happens when we have an
          // unrelated write in between an otherwise noop store.
          assert(isa<MemoryDef>(Current) &&
                 "Only MemoryDefs should reach here.");
          // TODO: Skip no alias MemoryDefs that have no aliasing reads.
          // We are searching for the definition of the store's destination.
          // So, if that is the same definition as the load, then this is a
          // noop. Otherwise, fail.
          if (LoadAccess != Current)
            return false;
        }
        return true;
      }
    }

    return false;
  }

  bool removePartiallyOverlappedStores(InstOverlapIntervalsTy &IOL) {
    bool Changed = false;
    for (auto OI : IOL) {
      Instruction *DeadI = OI.first;
      MemoryLocation Loc = *getLocForWrite(DeadI);
      assert(isRemovable(DeadI) && "Expect only removable instruction");

      const Value *Ptr = Loc.Ptr->stripPointerCasts();
      int64_t DeadStart = 0;
      uint64_t DeadSize = Loc.Size.getValue();
      GetPointerBaseWithConstantOffset(Ptr, DeadStart, DL);
      OverlapIntervalsTy &IntervalMap = OI.second;
      Changed |= tryToShortenEnd(DeadI, IntervalMap, DeadStart, DeadSize);
      if (IntervalMap.empty())
        continue;
      Changed |= tryToShortenBegin(DeadI, IntervalMap, DeadStart, DeadSize);
    }
    return Changed;
  }

  /// Eliminates writes to locations where the value that is being written
  /// is already stored at the same location.
  bool eliminateRedundantStoresOfExistingValues() {
    bool MadeChange = false;
    LLVM_DEBUG(dbgs() << "Trying to eliminate MemoryDefs that write the "
                         "already existing value\n");
    for (auto *Def : MemDefs) {
      if (SkipStores.contains(Def) || MSSA.isLiveOnEntryDef(Def))
        continue;

      Instruction *DefInst = Def->getMemoryInst();
      auto MaybeDefLoc = getLocForWrite(DefInst);
      if (!MaybeDefLoc || !isRemovable(DefInst))
        continue;

      MemoryDef *UpperDef;
      // To conserve compile-time, we avoid walking to the next clobbering def.
      // Instead, we just try to get the optimized access, if it exists. DSE
      // will try to optimize defs during the earlier traversal.
      if (Def->isOptimized())
        UpperDef = dyn_cast<MemoryDef>(Def->getOptimized());
      else
        UpperDef = dyn_cast<MemoryDef>(Def->getDefiningAccess());
      if (!UpperDef || MSSA.isLiveOnEntryDef(UpperDef))
        continue;

      Instruction *UpperInst = UpperDef->getMemoryInst();
      auto IsRedundantStore = [&]() {
        if (DefInst->isIdenticalTo(UpperInst))
          return true;
        if (auto *MemSetI = dyn_cast<MemSetInst>(UpperInst)) {
          if (auto *SI = dyn_cast<StoreInst>(DefInst)) {
            // MemSetInst must have a write location.
            auto UpperLoc = getLocForWrite(UpperInst);
            if (!UpperLoc)
              return false;
            int64_t InstWriteOffset = 0;
            int64_t DepWriteOffset = 0;
            auto OR = isOverwrite(UpperInst, DefInst, *UpperLoc, *MaybeDefLoc,
                                  InstWriteOffset, DepWriteOffset);
            Value *StoredByte = isBytewiseValue(SI->getValueOperand(), DL);
            return StoredByte && StoredByte == MemSetI->getOperand(1) &&
                   OR == OW_Complete;
          }
        }
        return false;
      };

      if (!IsRedundantStore() || isReadClobber(*MaybeDefLoc, DefInst))
        continue;
      LLVM_DEBUG(dbgs() << "DSE: Remove No-Op Store:\n  DEAD: " << *DefInst
                        << '\n');
      deleteDeadInstruction(DefInst);
      NumRedundantStores++;
      MadeChange = true;
    }
    return MadeChange;
  }
};

static bool eliminateDeadStores(Function &F, AliasAnalysis &AA, MemorySSA &MSSA,
                                DominatorTree &DT, PostDominatorTree &PDT,
                                const TargetLibraryInfo &TLI,
                                const LoopInfo &LI) {
  bool MadeChange = false;

  DSEState State(F, AA, MSSA, DT, PDT, TLI, LI);
  // For each store:
  for (unsigned I = 0; I < State.MemDefs.size(); I++) {
    MemoryDef *KillingDef = State.MemDefs[I];
    if (State.SkipStores.count(KillingDef))
      continue;
    Instruction *KillingI = KillingDef->getMemoryInst();

    std::optional<MemoryLocation> MaybeKillingLoc;
    if (State.isMemTerminatorInst(KillingI)) {
      if (auto KillingLoc = State.getLocForTerminator(KillingI))
        MaybeKillingLoc = KillingLoc->first;
    } else {
      MaybeKillingLoc = State.getLocForWrite(KillingI);
    }

    if (!MaybeKillingLoc) {
      LLVM_DEBUG(dbgs() << "Failed to find analyzable write location for "
                        << *KillingI << "\n");
      continue;
    }
    MemoryLocation KillingLoc = *MaybeKillingLoc;
    assert(KillingLoc.Ptr && "KillingLoc should not be null");
    const Value *KillingUndObj = getUnderlyingObject(KillingLoc.Ptr);
    LLVM_DEBUG(dbgs() << "Trying to eliminate MemoryDefs killed by "
                      << *KillingDef << " (" << *KillingI << ")\n");

    unsigned ScanLimit = MemorySSAScanLimit;
    unsigned WalkerStepLimit = MemorySSAUpwardsStepLimit;
    unsigned PartialLimit = MemorySSAPartialStoreLimit;
    // Worklist of MemoryAccesses that may be killed by KillingDef.
    SmallSetVector<MemoryAccess *, 8> ToCheck;
    // Track MemoryAccesses that have been deleted in the loop below, so we can
    // skip them. Don't use SkipStores for this, which may contain reused
    // MemoryAccess addresses.
    SmallPtrSet<MemoryAccess *, 8> Deleted;
    [[maybe_unused]] unsigned OrigNumSkipStores = State.SkipStores.size();
    ToCheck.insert(KillingDef->getDefiningAccess());

    bool Shortend = false;
    bool IsMemTerm = State.isMemTerminatorInst(KillingI);
    // Check if MemoryAccesses in the worklist are killed by KillingDef.
    for (unsigned I = 0; I < ToCheck.size(); I++) {
      MemoryAccess *Current = ToCheck[I];
      if (Deleted.contains(Current))
        continue;

      std::optional<MemoryAccess *> MaybeDeadAccess = State.getDomMemoryDef(
          KillingDef, Current, KillingLoc, KillingUndObj, ScanLimit,
          WalkerStepLimit, IsMemTerm, PartialLimit);

      if (!MaybeDeadAccess) {
        LLVM_DEBUG(dbgs() << "  finished walk\n");
        continue;
      }

      MemoryAccess *DeadAccess = *MaybeDeadAccess;
      LLVM_DEBUG(dbgs() << " Checking if we can kill " << *DeadAccess);
      if (isa<MemoryPhi>(DeadAccess)) {
        LLVM_DEBUG(dbgs() << "\n  ... adding incoming values to worklist\n");
        for (Value *V : cast<MemoryPhi>(DeadAccess)->incoming_values()) {
          MemoryAccess *IncomingAccess = cast<MemoryAccess>(V);
          BasicBlock *IncomingBlock = IncomingAccess->getBlock();
          BasicBlock *PhiBlock = DeadAccess->getBlock();

          // We only consider incoming MemoryAccesses that come before the
          // MemoryPhi. Otherwise we could discover candidates that do not
          // strictly dominate our starting def.
          if (State.PostOrderNumbers[IncomingBlock] >
              State.PostOrderNumbers[PhiBlock])
            ToCheck.insert(IncomingAccess);
        }
        continue;
      }
      auto *DeadDefAccess = cast<MemoryDef>(DeadAccess);
      Instruction *DeadI = DeadDefAccess->getMemoryInst();
      LLVM_DEBUG(dbgs() << " (" << *DeadI << ")\n");
      ToCheck.insert(DeadDefAccess->getDefiningAccess());
      NumGetDomMemoryDefPassed++;

      if (!DebugCounter::shouldExecute(MemorySSACounter))
        continue;

      MemoryLocation DeadLoc = *State.getLocForWrite(DeadI);

      if (IsMemTerm) {
        const Value *DeadUndObj = getUnderlyingObject(DeadLoc.Ptr);
        if (KillingUndObj != DeadUndObj)
          continue;
        LLVM_DEBUG(dbgs() << "DSE: Remove Dead Store:\n  DEAD: " << *DeadI
                          << "\n  KILLER: " << *KillingI << '\n');
        State.deleteDeadInstruction(DeadI, &Deleted);
        ++NumFastStores;
        MadeChange = true;
      } else {
        // Check if DeadI overwrites KillingI.
        int64_t KillingOffset = 0;
        int64_t DeadOffset = 0;
        OverwriteResult OR = State.isOverwrite(
            KillingI, DeadI, KillingLoc, DeadLoc, KillingOffset, DeadOffset);
        if (OR == OW_MaybePartial) {
          auto Iter = State.IOLs.insert(
              std::make_pair<BasicBlock *, InstOverlapIntervalsTy>(
                  DeadI->getParent(), InstOverlapIntervalsTy()));
          auto &IOL = Iter.first->second;
          OR = isPartialOverwrite(KillingLoc, DeadLoc, KillingOffset,
                                  DeadOffset, DeadI, IOL);
        }

        if (EnablePartialStoreMerging && OR == OW_PartialEarlierWithFullLater) {
          auto *DeadSI = dyn_cast<StoreInst>(DeadI);
          auto *KillingSI = dyn_cast<StoreInst>(KillingI);
          // We are re-using tryToMergePartialOverlappingStores, which requires
          // DeadSI to dominate KillingSI.
          // TODO: implement tryToMergeParialOverlappingStores using MemorySSA.
          if (DeadSI && KillingSI && DT.dominates(DeadSI, KillingSI)) {
            if (Constant *Merged = tryToMergePartialOverlappingStores(
                    KillingSI, DeadSI, KillingOffset, DeadOffset, State.DL,
                    State.BatchAA, &DT)) {

              // Update stored value of earlier store to merged constant.
              DeadSI->setOperand(0, Merged);
              ++NumModifiedStores;
              MadeChange = true;

              Shortend = true;
              // Remove killing store and remove any outstanding overlap
              // intervals for the updated store.
              State.deleteDeadInstruction(KillingSI, &Deleted);
              auto I = State.IOLs.find(DeadSI->getParent());
              if (I != State.IOLs.end())
                I->second.erase(DeadSI);
              break;
            }
          }
        }

        if (OR == OW_Complete) {
          LLVM_DEBUG(dbgs() << "DSE: Remove Dead Store:\n  DEAD: " << *DeadI
                            << "\n  KILLER: " << *KillingI << '\n');
          State.deleteDeadInstruction(DeadI, &Deleted);
          ++NumFastStores;
          MadeChange = true;
        }
      }
    }

    assert(State.SkipStores.size() - OrigNumSkipStores == Deleted.size() &&
           "SkipStores and Deleted out of sync?");

    // Check if the store is a no-op.
    if (!Shortend && State.storeIsNoop(KillingDef, KillingUndObj)) {
      LLVM_DEBUG(dbgs() << "DSE: Remove No-Op Store:\n  DEAD: " << *KillingI
                        << '\n');
      State.deleteDeadInstruction(KillingI);
      NumRedundantStores++;
      MadeChange = true;
      continue;
    }

    // Can we form a calloc from a memset/malloc pair?
    if (!Shortend && State.tryFoldIntoCalloc(KillingDef, KillingUndObj)) {
      LLVM_DEBUG(dbgs() << "DSE: Remove memset after forming calloc:\n"
                        << "  DEAD: " << *KillingI << '\n');
      State.deleteDeadInstruction(KillingI);
      MadeChange = true;
      continue;
    }
  }

  if (EnablePartialOverwriteTracking)
    for (auto &KV : State.IOLs)
      MadeChange |= State.removePartiallyOverlappedStores(KV.second);

  MadeChange |= State.eliminateRedundantStoresOfExistingValues();
  MadeChange |= State.eliminateDeadWritesAtEndOfFunction();

  while (!State.ToRemove.empty()) {
    Instruction *DeadInst = State.ToRemove.pop_back_val();
    DeadInst->eraseFromParent();
  }

  return MadeChange;
}
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// DSE Pass
//===----------------------------------------------------------------------===//
PreservedAnalyses DSEPass::run(Function &F, FunctionAnalysisManager &AM) {
  AliasAnalysis &AA = AM.getResult<AAManager>(F);
  const TargetLibraryInfo &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  MemorySSA &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();
  PostDominatorTree &PDT = AM.getResult<PostDominatorTreeAnalysis>(F);
  LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

  bool Changed = eliminateDeadStores(F, AA, MSSA, DT, PDT, TLI, LI);

#ifdef LLVM_ENABLE_STATS
  if (AreStatisticsEnabled())
    for (auto &I : instructions(F))
      NumRemainingStores += isa<StoreInst>(&I);
#endif

  if (!Changed)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<MemorySSAAnalysis>();
  PA.preserve<LoopAnalysis>();
  return PA;
}
