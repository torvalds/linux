//===- MemCpyOptimizer.cpp - Optimize use of memcpy and friends -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs various transformations related to eliminating memcpy
// calls, or transforming sets of stores into memset's.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/MemCpyOptimizer.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/Analysis/MemorySSAUpdater.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "memcpyopt"

static cl::opt<bool> EnableMemCpyOptWithoutLibcalls(
    "enable-memcpyopt-without-libcalls", cl::Hidden,
    cl::desc("Enable memcpyopt even when libcalls are disabled"));

STATISTIC(NumMemCpyInstr, "Number of memcpy instructions deleted");
STATISTIC(NumMemSetInfer, "Number of memsets inferred");
STATISTIC(NumMoveToCpy, "Number of memmoves converted to memcpy");
STATISTIC(NumCpyToSet, "Number of memcpys converted to memset");
STATISTIC(NumCallSlot, "Number of call slot optimizations performed");
STATISTIC(NumStackMove, "Number of stack-move optimizations performed");

namespace {

/// Represents a range of memset'd bytes with the ByteVal value.
/// This allows us to analyze stores like:
///   store 0 -> P+1
///   store 0 -> P+0
///   store 0 -> P+3
///   store 0 -> P+2
/// which sometimes happens with stores to arrays of structs etc.  When we see
/// the first store, we make a range [1, 2).  The second store extends the range
/// to [0, 2).  The third makes a new range [2, 3).  The fourth store joins the
/// two ranges into [0, 3) which is memset'able.
struct MemsetRange {
  // Start/End - A semi range that describes the span that this range covers.
  // The range is closed at the start and open at the end: [Start, End).
  int64_t Start, End;

  /// StartPtr - The getelementptr instruction that points to the start of the
  /// range.
  Value *StartPtr;

  /// Alignment - The known alignment of the first store.
  MaybeAlign Alignment;

  /// TheStores - The actual stores that make up this range.
  SmallVector<Instruction *, 16> TheStores;

  bool isProfitableToUseMemset(const DataLayout &DL) const;
};

} // end anonymous namespace

bool MemsetRange::isProfitableToUseMemset(const DataLayout &DL) const {
  // If we found more than 4 stores to merge or 16 bytes, use memset.
  if (TheStores.size() >= 4 || End - Start >= 16)
    return true;

  // If there is nothing to merge, don't do anything.
  if (TheStores.size() < 2)
    return false;

  // If any of the stores are a memset, then it is always good to extend the
  // memset.
  for (Instruction *SI : TheStores)
    if (!isa<StoreInst>(SI))
      return true;

  // Assume that the code generator is capable of merging pairs of stores
  // together if it wants to.
  if (TheStores.size() == 2)
    return false;

  // If we have fewer than 8 stores, it can still be worthwhile to do this.
  // For example, merging 4 i8 stores into an i32 store is useful almost always.
  // However, merging 2 32-bit stores isn't useful on a 32-bit architecture (the
  // memset will be split into 2 32-bit stores anyway) and doing so can
  // pessimize the llvm optimizer.
  //
  // Since we don't have perfect knowledge here, make some assumptions: assume
  // the maximum GPR width is the same size as the largest legal integer
  // size. If so, check to see whether we will end up actually reducing the
  // number of stores used.
  unsigned Bytes = unsigned(End - Start);
  unsigned MaxIntSize = DL.getLargestLegalIntTypeSizeInBits() / 8;
  if (MaxIntSize == 0)
    MaxIntSize = 1;
  unsigned NumPointerStores = Bytes / MaxIntSize;

  // Assume the remaining bytes if any are done a byte at a time.
  unsigned NumByteStores = Bytes % MaxIntSize;

  // If we will reduce the # stores (according to this heuristic), do the
  // transformation.  This encourages merging 4 x i8 -> i32 and 2 x i16 -> i32
  // etc.
  return TheStores.size() > NumPointerStores + NumByteStores;
}

namespace {

class MemsetRanges {
  using range_iterator = SmallVectorImpl<MemsetRange>::iterator;

  /// A sorted list of the memset ranges.
  SmallVector<MemsetRange, 8> Ranges;

  const DataLayout &DL;

public:
  MemsetRanges(const DataLayout &DL) : DL(DL) {}

  using const_iterator = SmallVectorImpl<MemsetRange>::const_iterator;

  const_iterator begin() const { return Ranges.begin(); }
  const_iterator end() const { return Ranges.end(); }
  bool empty() const { return Ranges.empty(); }

  void addInst(int64_t OffsetFromFirst, Instruction *Inst) {
    if (auto *SI = dyn_cast<StoreInst>(Inst))
      addStore(OffsetFromFirst, SI);
    else
      addMemSet(OffsetFromFirst, cast<MemSetInst>(Inst));
  }

  void addStore(int64_t OffsetFromFirst, StoreInst *SI) {
    TypeSize StoreSize = DL.getTypeStoreSize(SI->getOperand(0)->getType());
    assert(!StoreSize.isScalable() && "Can't track scalable-typed stores");
    addRange(OffsetFromFirst, StoreSize.getFixedValue(),
             SI->getPointerOperand(), SI->getAlign(), SI);
  }

  void addMemSet(int64_t OffsetFromFirst, MemSetInst *MSI) {
    int64_t Size = cast<ConstantInt>(MSI->getLength())->getZExtValue();
    addRange(OffsetFromFirst, Size, MSI->getDest(), MSI->getDestAlign(), MSI);
  }

  void addRange(int64_t Start, int64_t Size, Value *Ptr, MaybeAlign Alignment,
                Instruction *Inst);
};

} // end anonymous namespace

/// Add a new store to the MemsetRanges data structure.  This adds a
/// new range for the specified store at the specified offset, merging into
/// existing ranges as appropriate.
void MemsetRanges::addRange(int64_t Start, int64_t Size, Value *Ptr,
                            MaybeAlign Alignment, Instruction *Inst) {
  int64_t End = Start + Size;

  range_iterator I = partition_point(
      Ranges, [=](const MemsetRange &O) { return O.End < Start; });

  // We now know that I == E, in which case we didn't find anything to merge
  // with, or that Start <= I->End.  If End < I->Start or I == E, then we need
  // to insert a new range.  Handle this now.
  if (I == Ranges.end() || End < I->Start) {
    MemsetRange &R = *Ranges.insert(I, MemsetRange());
    R.Start = Start;
    R.End = End;
    R.StartPtr = Ptr;
    R.Alignment = Alignment;
    R.TheStores.push_back(Inst);
    return;
  }

  // This store overlaps with I, add it.
  I->TheStores.push_back(Inst);

  // At this point, we may have an interval that completely contains our store.
  // If so, just add it to the interval and return.
  if (I->Start <= Start && I->End >= End)
    return;

  // Now we know that Start <= I->End and End >= I->Start so the range overlaps
  // but is not entirely contained within the range.

  // See if the range extends the start of the range.  In this case, it couldn't
  // possibly cause it to join the prior range, because otherwise we would have
  // stopped on *it*.
  if (Start < I->Start) {
    I->Start = Start;
    I->StartPtr = Ptr;
    I->Alignment = Alignment;
  }

  // Now we know that Start <= I->End and Start >= I->Start (so the startpoint
  // is in or right at the end of I), and that End >= I->Start.  Extend I out to
  // End.
  if (End > I->End) {
    I->End = End;
    range_iterator NextI = I;
    while (++NextI != Ranges.end() && End >= NextI->Start) {
      // Merge the range in.
      I->TheStores.append(NextI->TheStores.begin(), NextI->TheStores.end());
      if (NextI->End > I->End)
        I->End = NextI->End;
      Ranges.erase(NextI);
      NextI = I;
    }
  }
}

//===----------------------------------------------------------------------===//
//                         MemCpyOptLegacyPass Pass
//===----------------------------------------------------------------------===//

// Check that V is either not accessible by the caller, or unwinding cannot
// occur between Start and End.
static bool mayBeVisibleThroughUnwinding(Value *V, Instruction *Start,
                                         Instruction *End) {
  assert(Start->getParent() == End->getParent() && "Must be in same block");
  // Function can't unwind, so it also can't be visible through unwinding.
  if (Start->getFunction()->doesNotThrow())
    return false;

  // Object is not visible on unwind.
  // TODO: Support RequiresNoCaptureBeforeUnwind case.
  bool RequiresNoCaptureBeforeUnwind;
  if (isNotVisibleOnUnwind(getUnderlyingObject(V),
                           RequiresNoCaptureBeforeUnwind) &&
      !RequiresNoCaptureBeforeUnwind)
    return false;

  // Check whether there are any unwinding instructions in the range.
  return any_of(make_range(Start->getIterator(), End->getIterator()),
                [](const Instruction &I) { return I.mayThrow(); });
}

void MemCpyOptPass::eraseInstruction(Instruction *I) {
  MSSAU->removeMemoryAccess(I);
  I->eraseFromParent();
}

// Check for mod or ref of Loc between Start and End, excluding both boundaries.
// Start and End must be in the same block.
// If SkippedLifetimeStart is provided, skip over one clobbering lifetime.start
// intrinsic and store it inside SkippedLifetimeStart.
static bool accessedBetween(BatchAAResults &AA, MemoryLocation Loc,
                            const MemoryUseOrDef *Start,
                            const MemoryUseOrDef *End,
                            Instruction **SkippedLifetimeStart = nullptr) {
  assert(Start->getBlock() == End->getBlock() && "Only local supported");
  for (const MemoryAccess &MA :
       make_range(++Start->getIterator(), End->getIterator())) {
    Instruction *I = cast<MemoryUseOrDef>(MA).getMemoryInst();
    if (isModOrRefSet(AA.getModRefInfo(I, Loc))) {
      auto *II = dyn_cast<IntrinsicInst>(I);
      if (II && II->getIntrinsicID() == Intrinsic::lifetime_start &&
          SkippedLifetimeStart && !*SkippedLifetimeStart) {
        *SkippedLifetimeStart = I;
        continue;
      }

      return true;
    }
  }
  return false;
}

// Check for mod of Loc between Start and End, excluding both boundaries.
// Start and End can be in different blocks.
static bool writtenBetween(MemorySSA *MSSA, BatchAAResults &AA,
                           MemoryLocation Loc, const MemoryUseOrDef *Start,
                           const MemoryUseOrDef *End) {
  if (isa<MemoryUse>(End)) {
    // For MemoryUses, getClobberingMemoryAccess may skip non-clobbering writes.
    // Manually check read accesses between Start and End, if they are in the
    // same block, for clobbers. Otherwise assume Loc is clobbered.
    return Start->getBlock() != End->getBlock() ||
           any_of(
               make_range(std::next(Start->getIterator()), End->getIterator()),
               [&AA, Loc](const MemoryAccess &Acc) {
                 if (isa<MemoryUse>(&Acc))
                   return false;
                 Instruction *AccInst =
                     cast<MemoryUseOrDef>(&Acc)->getMemoryInst();
                 return isModSet(AA.getModRefInfo(AccInst, Loc));
               });
  }

  // TODO: Only walk until we hit Start.
  MemoryAccess *Clobber = MSSA->getWalker()->getClobberingMemoryAccess(
      End->getDefiningAccess(), Loc, AA);
  return !MSSA->dominates(Clobber, Start);
}

// Update AA metadata
static void combineAAMetadata(Instruction *ReplInst, Instruction *I) {
  // FIXME: MD_tbaa_struct and MD_mem_parallel_loop_access should also be
  // handled here, but combineMetadata doesn't support them yet
  unsigned KnownIDs[] = {LLVMContext::MD_tbaa, LLVMContext::MD_alias_scope,
                         LLVMContext::MD_noalias,
                         LLVMContext::MD_invariant_group,
                         LLVMContext::MD_access_group};
  combineMetadata(ReplInst, I, KnownIDs, true);
}

/// When scanning forward over instructions, we look for some other patterns to
/// fold away. In particular, this looks for stores to neighboring locations of
/// memory. If it sees enough consecutive ones, it attempts to merge them
/// together into a memcpy/memset.
Instruction *MemCpyOptPass::tryMergingIntoMemset(Instruction *StartInst,
                                                 Value *StartPtr,
                                                 Value *ByteVal) {
  const DataLayout &DL = StartInst->getDataLayout();

  // We can't track scalable types
  if (auto *SI = dyn_cast<StoreInst>(StartInst))
    if (DL.getTypeStoreSize(SI->getOperand(0)->getType()).isScalable())
      return nullptr;

  // Okay, so we now have a single store that can be splatable.  Scan to find
  // all subsequent stores of the same value to offset from the same pointer.
  // Join these together into ranges, so we can decide whether contiguous blocks
  // are stored.
  MemsetRanges Ranges(DL);

  BasicBlock::iterator BI(StartInst);

  // Keeps track of the last memory use or def before the insertion point for
  // the new memset. The new MemoryDef for the inserted memsets will be inserted
  // after MemInsertPoint.
  MemoryUseOrDef *MemInsertPoint = nullptr;
  for (++BI; !BI->isTerminator(); ++BI) {
    auto *CurrentAcc = cast_or_null<MemoryUseOrDef>(
        MSSAU->getMemorySSA()->getMemoryAccess(&*BI));
    if (CurrentAcc)
      MemInsertPoint = CurrentAcc;

    // Calls that only access inaccessible memory do not block merging
    // accessible stores.
    if (auto *CB = dyn_cast<CallBase>(BI)) {
      if (CB->onlyAccessesInaccessibleMemory())
        continue;
    }

    if (!isa<StoreInst>(BI) && !isa<MemSetInst>(BI)) {
      // If the instruction is readnone, ignore it, otherwise bail out.  We
      // don't even allow readonly here because we don't want something like:
      // A[1] = 2; strlen(A); A[2] = 2; -> memcpy(A, ...); strlen(A).
      if (BI->mayWriteToMemory() || BI->mayReadFromMemory())
        break;
      continue;
    }

    if (auto *NextStore = dyn_cast<StoreInst>(BI)) {
      // If this is a store, see if we can merge it in.
      if (!NextStore->isSimple())
        break;

      Value *StoredVal = NextStore->getValueOperand();

      // Don't convert stores of non-integral pointer types to memsets (which
      // stores integers).
      if (DL.isNonIntegralPointerType(StoredVal->getType()->getScalarType()))
        break;

      // We can't track ranges involving scalable types.
      if (DL.getTypeStoreSize(StoredVal->getType()).isScalable())
        break;

      // Check to see if this stored value is of the same byte-splattable value.
      Value *StoredByte = isBytewiseValue(StoredVal, DL);
      if (isa<UndefValue>(ByteVal) && StoredByte)
        ByteVal = StoredByte;
      if (ByteVal != StoredByte)
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      std::optional<int64_t> Offset =
          NextStore->getPointerOperand()->getPointerOffsetFrom(StartPtr, DL);
      if (!Offset)
        break;

      Ranges.addStore(*Offset, NextStore);
    } else {
      auto *MSI = cast<MemSetInst>(BI);

      if (MSI->isVolatile() || ByteVal != MSI->getValue() ||
          !isa<ConstantInt>(MSI->getLength()))
        break;

      // Check to see if this store is to a constant offset from the start ptr.
      std::optional<int64_t> Offset =
          MSI->getDest()->getPointerOffsetFrom(StartPtr, DL);
      if (!Offset)
        break;

      Ranges.addMemSet(*Offset, MSI);
    }
  }

  // If we have no ranges, then we just had a single store with nothing that
  // could be merged in.  This is a very common case of course.
  if (Ranges.empty())
    return nullptr;

  // If we had at least one store that could be merged in, add the starting
  // store as well.  We try to avoid this unless there is at least something
  // interesting as a small compile-time optimization.
  Ranges.addInst(0, StartInst);

  // If we create any memsets, we put it right before the first instruction that
  // isn't part of the memset block.  This ensure that the memset is dominated
  // by any addressing instruction needed by the start of the block.
  IRBuilder<> Builder(&*BI);

  // Now that we have full information about ranges, loop over the ranges and
  // emit memset's for anything big enough to be worthwhile.
  Instruction *AMemSet = nullptr;
  for (const MemsetRange &Range : Ranges) {
    if (Range.TheStores.size() == 1)
      continue;

    // If it is profitable to lower this range to memset, do so now.
    if (!Range.isProfitableToUseMemset(DL))
      continue;

    // Otherwise, we do want to transform this!  Create a new memset.
    // Get the starting pointer of the block.
    StartPtr = Range.StartPtr;

    AMemSet = Builder.CreateMemSet(StartPtr, ByteVal, Range.End - Range.Start,
                                   Range.Alignment);
    AMemSet->mergeDIAssignID(Range.TheStores);

    LLVM_DEBUG(dbgs() << "Replace stores:\n"; for (Instruction *SI
                                                   : Range.TheStores) dbgs()
                                              << *SI << '\n';
               dbgs() << "With: " << *AMemSet << '\n');
    if (!Range.TheStores.empty())
      AMemSet->setDebugLoc(Range.TheStores[0]->getDebugLoc());

    auto *NewDef = cast<MemoryDef>(
        MemInsertPoint->getMemoryInst() == &*BI
            ? MSSAU->createMemoryAccessBefore(AMemSet, nullptr, MemInsertPoint)
            : MSSAU->createMemoryAccessAfter(AMemSet, nullptr, MemInsertPoint));
    MSSAU->insertDef(NewDef, /*RenameUses=*/true);
    MemInsertPoint = NewDef;

    // Zap all the stores.
    for (Instruction *SI : Range.TheStores)
      eraseInstruction(SI);

    ++NumMemSetInfer;
  }

  return AMemSet;
}

// This method try to lift a store instruction before position P.
// It will lift the store and its argument + that anything that
// may alias with these.
// The method returns true if it was successful.
bool MemCpyOptPass::moveUp(StoreInst *SI, Instruction *P, const LoadInst *LI) {
  // If the store alias this position, early bail out.
  MemoryLocation StoreLoc = MemoryLocation::get(SI);
  if (isModOrRefSet(AA->getModRefInfo(P, StoreLoc)))
    return false;

  // Keep track of the arguments of all instruction we plan to lift
  // so we can make sure to lift them as well if appropriate.
  DenseSet<Instruction *> Args;
  auto AddArg = [&](Value *Arg) {
    auto *I = dyn_cast<Instruction>(Arg);
    if (I && I->getParent() == SI->getParent()) {
      // Cannot hoist user of P above P
      if (I == P)
        return false;
      Args.insert(I);
    }
    return true;
  };
  if (!AddArg(SI->getPointerOperand()))
    return false;

  // Instruction to lift before P.
  SmallVector<Instruction *, 8> ToLift{SI};

  // Memory locations of lifted instructions.
  SmallVector<MemoryLocation, 8> MemLocs{StoreLoc};

  // Lifted calls.
  SmallVector<const CallBase *, 8> Calls;

  const MemoryLocation LoadLoc = MemoryLocation::get(LI);

  for (auto I = --SI->getIterator(), E = P->getIterator(); I != E; --I) {
    auto *C = &*I;

    // Make sure hoisting does not perform a store that was not guaranteed to
    // happen.
    if (!isGuaranteedToTransferExecutionToSuccessor(C))
      return false;

    bool MayAlias = isModOrRefSet(AA->getModRefInfo(C, std::nullopt));

    bool NeedLift = false;
    if (Args.erase(C))
      NeedLift = true;
    else if (MayAlias) {
      NeedLift = llvm::any_of(MemLocs, [C, this](const MemoryLocation &ML) {
        return isModOrRefSet(AA->getModRefInfo(C, ML));
      });

      if (!NeedLift)
        NeedLift = llvm::any_of(Calls, [C, this](const CallBase *Call) {
          return isModOrRefSet(AA->getModRefInfo(C, Call));
        });
    }

    if (!NeedLift)
      continue;

    if (MayAlias) {
      // Since LI is implicitly moved downwards past the lifted instructions,
      // none of them may modify its source.
      if (isModSet(AA->getModRefInfo(C, LoadLoc)))
        return false;
      else if (const auto *Call = dyn_cast<CallBase>(C)) {
        // If we can't lift this before P, it's game over.
        if (isModOrRefSet(AA->getModRefInfo(P, Call)))
          return false;

        Calls.push_back(Call);
      } else if (isa<LoadInst>(C) || isa<StoreInst>(C) || isa<VAArgInst>(C)) {
        // If we can't lift this before P, it's game over.
        auto ML = MemoryLocation::get(C);
        if (isModOrRefSet(AA->getModRefInfo(P, ML)))
          return false;

        MemLocs.push_back(ML);
      } else
        // We don't know how to lift this instruction.
        return false;
    }

    ToLift.push_back(C);
    for (Value *Op : C->operands())
      if (!AddArg(Op))
        return false;
  }

  // Find MSSA insertion point. Normally P will always have a corresponding
  // memory access before which we can insert. However, with non-standard AA
  // pipelines, there may be a mismatch between AA and MSSA, in which case we
  // will scan for a memory access before P. In either case, we know for sure
  // that at least the load will have a memory access.
  // TODO: Simplify this once P will be determined by MSSA, in which case the
  // discrepancy can no longer occur.
  MemoryUseOrDef *MemInsertPoint = nullptr;
  if (MemoryUseOrDef *MA = MSSAU->getMemorySSA()->getMemoryAccess(P)) {
    MemInsertPoint = cast<MemoryUseOrDef>(--MA->getIterator());
  } else {
    const Instruction *ConstP = P;
    for (const Instruction &I : make_range(++ConstP->getReverseIterator(),
                                           ++LI->getReverseIterator())) {
      if (MemoryUseOrDef *MA = MSSAU->getMemorySSA()->getMemoryAccess(&I)) {
        MemInsertPoint = MA;
        break;
      }
    }
  }

  // We made it, we need to lift.
  for (auto *I : llvm::reverse(ToLift)) {
    LLVM_DEBUG(dbgs() << "Lifting " << *I << " before " << *P << "\n");
    I->moveBefore(P);
    assert(MemInsertPoint && "Must have found insert point");
    if (MemoryUseOrDef *MA = MSSAU->getMemorySSA()->getMemoryAccess(I)) {
      MSSAU->moveAfter(MA, MemInsertPoint);
      MemInsertPoint = MA;
    }
  }

  return true;
}

bool MemCpyOptPass::processStoreOfLoad(StoreInst *SI, LoadInst *LI,
                                       const DataLayout &DL,
                                       BasicBlock::iterator &BBI) {
  if (!LI->isSimple() || !LI->hasOneUse() || LI->getParent() != SI->getParent())
    return false;

  auto *T = LI->getType();
  // Don't introduce calls to memcpy/memmove intrinsics out of thin air if
  // the corresponding libcalls are not available.
  // TODO: We should really distinguish between libcall availability and
  // our ability to introduce intrinsics.
  if (T->isAggregateType() &&
      (EnableMemCpyOptWithoutLibcalls ||
       (TLI->has(LibFunc_memcpy) && TLI->has(LibFunc_memmove)))) {
    MemoryLocation LoadLoc = MemoryLocation::get(LI);

    // We use alias analysis to check if an instruction may store to
    // the memory we load from in between the load and the store. If
    // such an instruction is found, we try to promote there instead
    // of at the store position.
    // TODO: Can use MSSA for this.
    Instruction *P = SI;
    for (auto &I : make_range(++LI->getIterator(), SI->getIterator())) {
      if (isModSet(AA->getModRefInfo(&I, LoadLoc))) {
        P = &I;
        break;
      }
    }

    // We found an instruction that may write to the loaded memory.
    // We can try to promote at this position instead of the store
    // position if nothing aliases the store memory after this and the store
    // destination is not in the range.
    if (P && P != SI) {
      if (!moveUp(SI, P, LI))
        P = nullptr;
    }

    // If a valid insertion position is found, then we can promote
    // the load/store pair to a memcpy.
    if (P) {
      // If we load from memory that may alias the memory we store to,
      // memmove must be used to preserve semantic. If not, memcpy can
      // be used. Also, if we load from constant memory, memcpy can be used
      // as the constant memory won't be modified.
      bool UseMemMove = false;
      if (isModSet(AA->getModRefInfo(SI, LoadLoc)))
        UseMemMove = true;

      IRBuilder<> Builder(P);
      Value *Size =
          Builder.CreateTypeSize(Builder.getInt64Ty(), DL.getTypeStoreSize(T));
      Instruction *M;
      if (UseMemMove)
        M = Builder.CreateMemMove(SI->getPointerOperand(), SI->getAlign(),
                                  LI->getPointerOperand(), LI->getAlign(),
                                  Size);
      else
        M = Builder.CreateMemCpy(SI->getPointerOperand(), SI->getAlign(),
                                 LI->getPointerOperand(), LI->getAlign(), Size);
      M->copyMetadata(*SI, LLVMContext::MD_DIAssignID);

      LLVM_DEBUG(dbgs() << "Promoting " << *LI << " to " << *SI << " => " << *M
                        << "\n");

      auto *LastDef =
          cast<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(SI));
      auto *NewAccess = MSSAU->createMemoryAccessAfter(M, nullptr, LastDef);
      MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/true);

      eraseInstruction(SI);
      eraseInstruction(LI);
      ++NumMemCpyInstr;

      // Make sure we do not invalidate the iterator.
      BBI = M->getIterator();
      return true;
    }
  }

  // Detect cases where we're performing call slot forwarding, but
  // happen to be using a load-store pair to implement it, rather than
  // a memcpy.
  BatchAAResults BAA(*AA);
  auto GetCall = [&]() -> CallInst * {
    // We defer this expensive clobber walk until the cheap checks
    // have been done on the source inside performCallSlotOptzn.
    if (auto *LoadClobber = dyn_cast<MemoryUseOrDef>(
            MSSA->getWalker()->getClobberingMemoryAccess(LI, BAA)))
      return dyn_cast_or_null<CallInst>(LoadClobber->getMemoryInst());
    return nullptr;
  };

  bool Changed = performCallSlotOptzn(
      LI, SI, SI->getPointerOperand()->stripPointerCasts(),
      LI->getPointerOperand()->stripPointerCasts(),
      DL.getTypeStoreSize(SI->getOperand(0)->getType()),
      std::min(SI->getAlign(), LI->getAlign()), BAA, GetCall);
  if (Changed) {
    eraseInstruction(SI);
    eraseInstruction(LI);
    ++NumMemCpyInstr;
    return true;
  }

  // If this is a load-store pair from a stack slot to a stack slot, we
  // might be able to perform the stack-move optimization just as we do for
  // memcpys from an alloca to an alloca.
  if (auto *DestAlloca = dyn_cast<AllocaInst>(SI->getPointerOperand())) {
    if (auto *SrcAlloca = dyn_cast<AllocaInst>(LI->getPointerOperand())) {
      if (performStackMoveOptzn(LI, SI, DestAlloca, SrcAlloca,
                                DL.getTypeStoreSize(T), BAA)) {
        // Avoid invalidating the iterator.
        BBI = SI->getNextNonDebugInstruction()->getIterator();
        eraseInstruction(SI);
        eraseInstruction(LI);
        ++NumMemCpyInstr;
        return true;
      }
    }
  }

  return false;
}

bool MemCpyOptPass::processStore(StoreInst *SI, BasicBlock::iterator &BBI) {
  if (!SI->isSimple())
    return false;

  // Avoid merging nontemporal stores since the resulting
  // memcpy/memset would not be able to preserve the nontemporal hint.
  // In theory we could teach how to propagate the !nontemporal metadata to
  // memset calls. However, that change would force the backend to
  // conservatively expand !nontemporal memset calls back to sequences of
  // store instructions (effectively undoing the merging).
  if (SI->getMetadata(LLVMContext::MD_nontemporal))
    return false;

  const DataLayout &DL = SI->getDataLayout();

  Value *StoredVal = SI->getValueOperand();

  // Not all the transforms below are correct for non-integral pointers, bail
  // until we've audited the individual pieces.
  if (DL.isNonIntegralPointerType(StoredVal->getType()->getScalarType()))
    return false;

  // Load to store forwarding can be interpreted as memcpy.
  if (auto *LI = dyn_cast<LoadInst>(StoredVal))
    return processStoreOfLoad(SI, LI, DL, BBI);

  // The following code creates memset intrinsics out of thin air. Don't do
  // this if the corresponding libfunc is not available.
  // TODO: We should really distinguish between libcall availability and
  // our ability to introduce intrinsics.
  if (!(TLI->has(LibFunc_memset) || EnableMemCpyOptWithoutLibcalls))
    return false;

  // There are two cases that are interesting for this code to handle: memcpy
  // and memset.  Right now we only handle memset.

  // Ensure that the value being stored is something that can be memset'able a
  // byte at a time like "0" or "-1" or any width, as well as things like
  // 0xA0A0A0A0 and 0.0.
  auto *V = SI->getOperand(0);
  if (Value *ByteVal = isBytewiseValue(V, DL)) {
    if (Instruction *I =
            tryMergingIntoMemset(SI, SI->getPointerOperand(), ByteVal)) {
      BBI = I->getIterator(); // Don't invalidate iterator.
      return true;
    }

    // If we have an aggregate, we try to promote it to memset regardless
    // of opportunity for merging as it can expose optimization opportunities
    // in subsequent passes.
    auto *T = V->getType();
    if (T->isAggregateType()) {
      uint64_t Size = DL.getTypeStoreSize(T);
      IRBuilder<> Builder(SI);
      auto *M = Builder.CreateMemSet(SI->getPointerOperand(), ByteVal, Size,
                                     SI->getAlign());
      M->copyMetadata(*SI, LLVMContext::MD_DIAssignID);

      LLVM_DEBUG(dbgs() << "Promoting " << *SI << " to " << *M << "\n");

      // The newly inserted memset is immediately overwritten by the original
      // store, so we do not need to rename uses.
      auto *StoreDef = cast<MemoryDef>(MSSA->getMemoryAccess(SI));
      auto *NewAccess = MSSAU->createMemoryAccessBefore(M, nullptr, StoreDef);
      MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/false);

      eraseInstruction(SI);
      NumMemSetInfer++;

      // Make sure we do not invalidate the iterator.
      BBI = M->getIterator();
      return true;
    }
  }

  return false;
}

bool MemCpyOptPass::processMemSet(MemSetInst *MSI, BasicBlock::iterator &BBI) {
  // See if there is another memset or store neighboring this memset which
  // allows us to widen out the memset to do a single larger store.
  if (isa<ConstantInt>(MSI->getLength()) && !MSI->isVolatile())
    if (Instruction *I =
            tryMergingIntoMemset(MSI, MSI->getDest(), MSI->getValue())) {
      BBI = I->getIterator(); // Don't invalidate iterator.
      return true;
    }
  return false;
}

/// Takes a memcpy and a call that it depends on,
/// and checks for the possibility of a call slot optimization by having
/// the call write its result directly into the destination of the memcpy.
bool MemCpyOptPass::performCallSlotOptzn(Instruction *cpyLoad,
                                         Instruction *cpyStore, Value *cpyDest,
                                         Value *cpySrc, TypeSize cpySize,
                                         Align cpyDestAlign,
                                         BatchAAResults &BAA,
                                         std::function<CallInst *()> GetC) {
  // The general transformation to keep in mind is
  //
  //   call @func(..., src, ...)
  //   memcpy(dest, src, ...)
  //
  // ->
  //
  //   memcpy(dest, src, ...)
  //   call @func(..., dest, ...)
  //
  // Since moving the memcpy is technically awkward, we additionally check that
  // src only holds uninitialized values at the moment of the call, meaning that
  // the memcpy can be discarded rather than moved.

  // We can't optimize scalable types.
  if (cpySize.isScalable())
    return false;

  // Require that src be an alloca.  This simplifies the reasoning considerably.
  auto *srcAlloca = dyn_cast<AllocaInst>(cpySrc);
  if (!srcAlloca)
    return false;

  ConstantInt *srcArraySize = dyn_cast<ConstantInt>(srcAlloca->getArraySize());
  if (!srcArraySize)
    return false;

  const DataLayout &DL = cpyLoad->getDataLayout();
  TypeSize SrcAllocaSize = DL.getTypeAllocSize(srcAlloca->getAllocatedType());
  // We can't optimize scalable types.
  if (SrcAllocaSize.isScalable())
    return false;
  uint64_t srcSize = SrcAllocaSize * srcArraySize->getZExtValue();

  if (cpySize < srcSize)
    return false;

  CallInst *C = GetC();
  if (!C)
    return false;

  // Lifetime marks shouldn't be operated on.
  if (Function *F = C->getCalledFunction())
    if (F->isIntrinsic() && F->getIntrinsicID() == Intrinsic::lifetime_start)
      return false;

  if (C->getParent() != cpyStore->getParent()) {
    LLVM_DEBUG(dbgs() << "Call Slot: block local restriction\n");
    return false;
  }

  MemoryLocation DestLoc =
      isa<StoreInst>(cpyStore)
          ? MemoryLocation::get(cpyStore)
          : MemoryLocation::getForDest(cast<MemCpyInst>(cpyStore));

  // Check that nothing touches the dest of the copy between
  // the call and the store/memcpy.
  Instruction *SkippedLifetimeStart = nullptr;
  if (accessedBetween(BAA, DestLoc, MSSA->getMemoryAccess(C),
                      MSSA->getMemoryAccess(cpyStore), &SkippedLifetimeStart)) {
    LLVM_DEBUG(dbgs() << "Call Slot: Dest pointer modified after call\n");
    return false;
  }

  // If we need to move a lifetime.start above the call, make sure that we can
  // actually do so. If the argument is bitcasted for example, we would have to
  // move the bitcast as well, which we don't handle.
  if (SkippedLifetimeStart) {
    auto *LifetimeArg =
        dyn_cast<Instruction>(SkippedLifetimeStart->getOperand(1));
    if (LifetimeArg && LifetimeArg->getParent() == C->getParent() &&
        C->comesBefore(LifetimeArg))
      return false;
  }

  // Check that storing to the first srcSize bytes of dest will not cause a
  // trap or data race.
  bool ExplicitlyDereferenceableOnly;
  if (!isWritableObject(getUnderlyingObject(cpyDest),
                        ExplicitlyDereferenceableOnly) ||
      !isDereferenceableAndAlignedPointer(cpyDest, Align(1), APInt(64, cpySize),
                                          DL, C, AC, DT)) {
    LLVM_DEBUG(dbgs() << "Call Slot: Dest pointer not dereferenceable\n");
    return false;
  }

  // Make sure that nothing can observe cpyDest being written early. There are
  // a number of cases to consider:
  //  1. cpyDest cannot be accessed between C and cpyStore as a precondition of
  //     the transform.
  //  2. C itself may not access cpyDest (prior to the transform). This is
  //     checked further below.
  //  3. If cpyDest is accessible to the caller of this function (potentially
  //     captured and not based on an alloca), we need to ensure that we cannot
  //     unwind between C and cpyStore. This is checked here.
  //  4. If cpyDest is potentially captured, there may be accesses to it from
  //     another thread. In this case, we need to check that cpyStore is
  //     guaranteed to be executed if C is. As it is a non-atomic access, it
  //     renders accesses from other threads undefined.
  //     TODO: This is currently not checked.
  if (mayBeVisibleThroughUnwinding(cpyDest, C, cpyStore)) {
    LLVM_DEBUG(dbgs() << "Call Slot: Dest may be visible through unwinding\n");
    return false;
  }

  // Check that dest points to memory that is at least as aligned as src.
  Align srcAlign = srcAlloca->getAlign();
  bool isDestSufficientlyAligned = srcAlign <= cpyDestAlign;
  // If dest is not aligned enough and we can't increase its alignment then
  // bail out.
  if (!isDestSufficientlyAligned && !isa<AllocaInst>(cpyDest)) {
    LLVM_DEBUG(dbgs() << "Call Slot: Dest not sufficiently aligned\n");
    return false;
  }

  // Check that src is not accessed except via the call and the memcpy.  This
  // guarantees that it holds only undefined values when passed in (so the final
  // memcpy can be dropped), that it is not read or written between the call and
  // the memcpy, and that writing beyond the end of it is undefined.
  SmallVector<User *, 8> srcUseList(srcAlloca->users());
  while (!srcUseList.empty()) {
    User *U = srcUseList.pop_back_val();

    if (isa<BitCastInst>(U) || isa<AddrSpaceCastInst>(U)) {
      append_range(srcUseList, U->users());
      continue;
    }
    if (const auto *G = dyn_cast<GetElementPtrInst>(U);
        G && G->hasAllZeroIndices()) {
      append_range(srcUseList, U->users());
      continue;
    }
    if (const auto *IT = dyn_cast<IntrinsicInst>(U))
      if (IT->isLifetimeStartOrEnd())
        continue;

    if (U != C && U != cpyLoad) {
      LLVM_DEBUG(dbgs() << "Call slot: Source accessed by " << *U << "\n");
      return false;
    }
  }

  // Check whether src is captured by the called function, in which case there
  // may be further indirect uses of src.
  bool SrcIsCaptured = any_of(C->args(), [&](Use &U) {
    return U->stripPointerCasts() == cpySrc &&
           !C->doesNotCapture(C->getArgOperandNo(&U));
  });

  // If src is captured, then check whether there are any potential uses of
  // src through the captured pointer before the lifetime of src ends, either
  // due to a lifetime.end or a return from the function.
  if (SrcIsCaptured) {
    // Check that dest is not captured before/at the call. We have already
    // checked that src is not captured before it. If either had been captured,
    // then the call might be comparing the argument against the captured dest
    // or src pointer.
    Value *DestObj = getUnderlyingObject(cpyDest);
    if (!isIdentifiedFunctionLocal(DestObj) ||
        PointerMayBeCapturedBefore(DestObj, /* ReturnCaptures */ true,
                                   /* StoreCaptures */ true, C, DT,
                                   /* IncludeI */ true))
      return false;

    MemoryLocation SrcLoc =
        MemoryLocation(srcAlloca, LocationSize::precise(srcSize));
    for (Instruction &I :
         make_range(++C->getIterator(), C->getParent()->end())) {
      // Lifetime of srcAlloca ends at lifetime.end.
      if (auto *II = dyn_cast<IntrinsicInst>(&I)) {
        if (II->getIntrinsicID() == Intrinsic::lifetime_end &&
            II->getArgOperand(1)->stripPointerCasts() == srcAlloca &&
            cast<ConstantInt>(II->getArgOperand(0))->uge(srcSize))
          break;
      }

      // Lifetime of srcAlloca ends at return.
      if (isa<ReturnInst>(&I))
        break;

      // Ignore the direct read of src in the load.
      if (&I == cpyLoad)
        continue;

      // Check whether this instruction may mod/ref src through the captured
      // pointer (we have already any direct mod/refs in the loop above).
      // Also bail if we hit a terminator, as we don't want to scan into other
      // blocks.
      if (isModOrRefSet(BAA.getModRefInfo(&I, SrcLoc)) || I.isTerminator())
        return false;
    }
  }

  // Since we're changing the parameter to the callsite, we need to make sure
  // that what would be the new parameter dominates the callsite.
  bool NeedMoveGEP = false;
  if (!DT->dominates(cpyDest, C)) {
    // Support moving a constant index GEP before the call.
    auto *GEP = dyn_cast<GetElementPtrInst>(cpyDest);
    if (GEP && GEP->hasAllConstantIndices() &&
        DT->dominates(GEP->getPointerOperand(), C))
      NeedMoveGEP = true;
    else
      return false;
  }

  // In addition to knowing that the call does not access src in some
  // unexpected manner, for example via a global, which we deduce from
  // the use analysis, we also need to know that it does not sneakily
  // access dest.  We rely on AA to figure this out for us.
  MemoryLocation DestWithSrcSize(cpyDest, LocationSize::precise(srcSize));
  ModRefInfo MR = BAA.getModRefInfo(C, DestWithSrcSize);
  // If necessary, perform additional analysis.
  if (isModOrRefSet(MR))
    MR = BAA.callCapturesBefore(C, DestWithSrcSize, DT);
  if (isModOrRefSet(MR))
    return false;

  // We can't create address space casts here because we don't know if they're
  // safe for the target.
  if (cpySrc->getType() != cpyDest->getType())
    return false;
  for (unsigned ArgI = 0; ArgI < C->arg_size(); ++ArgI)
    if (C->getArgOperand(ArgI)->stripPointerCasts() == cpySrc &&
        cpySrc->getType() != C->getArgOperand(ArgI)->getType())
      return false;

  // All the checks have passed, so do the transformation.
  bool changedArgument = false;
  for (unsigned ArgI = 0; ArgI < C->arg_size(); ++ArgI)
    if (C->getArgOperand(ArgI)->stripPointerCasts() == cpySrc) {
      changedArgument = true;
      C->setArgOperand(ArgI, cpyDest);
    }

  if (!changedArgument)
    return false;

  // If the destination wasn't sufficiently aligned then increase its alignment.
  if (!isDestSufficientlyAligned) {
    assert(isa<AllocaInst>(cpyDest) && "Can only increase alloca alignment!");
    cast<AllocaInst>(cpyDest)->setAlignment(srcAlign);
  }

  if (NeedMoveGEP) {
    auto *GEP = dyn_cast<GetElementPtrInst>(cpyDest);
    GEP->moveBefore(C);
  }

  if (SkippedLifetimeStart) {
    SkippedLifetimeStart->moveBefore(C);
    MSSAU->moveBefore(MSSA->getMemoryAccess(SkippedLifetimeStart),
                      MSSA->getMemoryAccess(C));
  }

  combineAAMetadata(C, cpyLoad);
  if (cpyLoad != cpyStore)
    combineAAMetadata(C, cpyStore);

  ++NumCallSlot;
  return true;
}

/// We've found that the (upward scanning) memory dependence of memcpy 'M' is
/// the memcpy 'MDep'. Try to simplify M to copy from MDep's input if we can.
bool MemCpyOptPass::processMemCpyMemCpyDependence(MemCpyInst *M,
                                                  MemCpyInst *MDep,
                                                  BatchAAResults &BAA) {
  // If dep instruction is reading from our current input, then it is a noop
  // transfer and substituting the input won't change this instruction. Just
  // ignore the input and let someone else zap MDep. This handles cases like:
  //    memcpy(a <- a)
  //    memcpy(b <- a)
  if (M->getSource() == MDep->getSource())
    return false;

  // We can only optimize non-volatile memcpy's.
  if (MDep->isVolatile())
    return false;

  int64_t MForwardOffset = 0;
  const DataLayout &DL = M->getModule()->getDataLayout();
  // We can only transforms memcpy's where the dest of one is the source of the
  // other, or they have an offset in a range.
  if (M->getSource() != MDep->getDest()) {
    std::optional<int64_t> Offset =
        M->getSource()->getPointerOffsetFrom(MDep->getDest(), DL);
    if (!Offset || *Offset < 0)
      return false;
    MForwardOffset = *Offset;
  }

  // The length of the memcpy's must be the same, or the preceding one
  // must be larger than the following one.
  if (MForwardOffset != 0 || MDep->getLength() != M->getLength()) {
    auto *MDepLen = dyn_cast<ConstantInt>(MDep->getLength());
    auto *MLen = dyn_cast<ConstantInt>(M->getLength());
    if (!MDepLen || !MLen ||
        MDepLen->getZExtValue() < MLen->getZExtValue() + MForwardOffset)
      return false;
  }

  IRBuilder<> Builder(M);
  auto *CopySource = MDep->getSource();
  Instruction *NewCopySource = nullptr;
  auto CleanupOnRet = llvm::make_scope_exit([&NewCopySource] {
    if (NewCopySource && NewCopySource->use_empty())
      // Safety: It's safe here because we will only allocate more instructions
      // after finishing all BatchAA queries, but we have to be careful if we
      // want to do something like this in another place. Then we'd probably
      // have to delay instruction removal until all transforms on an
      // instruction finished.
      NewCopySource->eraseFromParent();
  });
  MaybeAlign CopySourceAlign = MDep->getSourceAlign();
  // We just need to calculate the actual size of the copy.
  auto MCopyLoc = MemoryLocation::getForSource(MDep).getWithNewSize(
      MemoryLocation::getForSource(M).Size);

  // When the forwarding offset is greater than 0, we transform
  //    memcpy(d1 <- s1)
  //    memcpy(d2 <- d1+o)
  // to
  //    memcpy(d2 <- s1+o)
  if (MForwardOffset > 0) {
    // The copy destination of `M` maybe can serve as the source of copying.
    std::optional<int64_t> MDestOffset =
        M->getRawDest()->getPointerOffsetFrom(MDep->getRawSource(), DL);
    if (MDestOffset == MForwardOffset)
      CopySource = M->getDest();
    else {
      CopySource = Builder.CreateInBoundsPtrAdd(
          CopySource, Builder.getInt64(MForwardOffset));
      NewCopySource = dyn_cast<Instruction>(CopySource);
    }
    // We need to update `MCopyLoc` if an offset exists.
    MCopyLoc = MCopyLoc.getWithNewPtr(CopySource);
    if (CopySourceAlign)
      CopySourceAlign = commonAlignment(*CopySourceAlign, MForwardOffset);
  }

  // Verify that the copied-from memory doesn't change in between the two
  // transfers.  For example, in:
  //    memcpy(a <- b)
  //    *b = 42;
  //    memcpy(c <- a)
  // It would be invalid to transform the second memcpy into memcpy(c <- b).
  //
  // TODO: If the code between M and MDep is transparent to the destination "c",
  // then we could still perform the xform by moving M up to the first memcpy.
  if (writtenBetween(MSSA, BAA, MCopyLoc, MSSA->getMemoryAccess(MDep),
                     MSSA->getMemoryAccess(M)))
    return false;

  // No need to create `memcpy(a <- a)`.
  if (BAA.isMustAlias(M->getDest(), CopySource)) {
    // Remove the instruction we're replacing.
    eraseInstruction(M);
    ++NumMemCpyInstr;
    return true;
  }

  // If the dest of the second might alias the source of the first, then the
  // source and dest might overlap. In addition, if the source of the first
  // points to constant memory, they won't overlap by definition. Otherwise, we
  // still want to eliminate the intermediate value, but we have to generate a
  // memmove instead of memcpy.
  bool UseMemMove = false;
  if (isModSet(BAA.getModRefInfo(M, MemoryLocation::getForSource(MDep)))) {
    // Don't convert llvm.memcpy.inline into memmove because memmove can be
    // lowered as a call, and that is not allowed for llvm.memcpy.inline (and
    // there is no inline version of llvm.memmove)
    if (isa<MemCpyInlineInst>(M))
      return false;
    UseMemMove = true;
  }

  // If all checks passed, then we can transform M.
  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Forwarding memcpy->memcpy src:\n"
                    << *MDep << '\n'
                    << *M << '\n');

  // TODO: Is this worth it if we're creating a less aligned memcpy? For
  // example we could be moving from movaps -> movq on x86.
  Instruction *NewM;
  if (UseMemMove)
    NewM =
        Builder.CreateMemMove(M->getDest(), M->getDestAlign(), CopySource,
                              CopySourceAlign, M->getLength(), M->isVolatile());
  else if (isa<MemCpyInlineInst>(M)) {
    // llvm.memcpy may be promoted to llvm.memcpy.inline, but the converse is
    // never allowed since that would allow the latter to be lowered as a call
    // to an external function.
    NewM = Builder.CreateMemCpyInline(M->getDest(), M->getDestAlign(),
                                      CopySource, CopySourceAlign,
                                      M->getLength(), M->isVolatile());
  } else
    NewM =
        Builder.CreateMemCpy(M->getDest(), M->getDestAlign(), CopySource,
                             CopySourceAlign, M->getLength(), M->isVolatile());
  NewM->copyMetadata(*M, LLVMContext::MD_DIAssignID);

  assert(isa<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(M)));
  auto *LastDef = cast<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(M));
  auto *NewAccess = MSSAU->createMemoryAccessAfter(NewM, nullptr, LastDef);
  MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/true);

  // Remove the instruction we're replacing.
  eraseInstruction(M);
  ++NumMemCpyInstr;
  return true;
}

/// We've found that the (upward scanning) memory dependence of \p MemCpy is
/// \p MemSet.  Try to simplify \p MemSet to only set the trailing bytes that
/// weren't copied over by \p MemCpy.
///
/// In other words, transform:
/// \code
///   memset(dst, c, dst_size);
///   ...
///   memcpy(dst, src, src_size);
/// \endcode
/// into:
/// \code
///   ...
///   memset(dst + src_size, c, dst_size <= src_size ? 0 : dst_size - src_size);
///   memcpy(dst, src, src_size);
/// \endcode
///
/// The memset is sunk to just before the memcpy to ensure that src_size is
/// present when emitting the simplified memset.
bool MemCpyOptPass::processMemSetMemCpyDependence(MemCpyInst *MemCpy,
                                                  MemSetInst *MemSet,
                                                  BatchAAResults &BAA) {
  // We can only transform memset/memcpy with the same destination.
  if (!BAA.isMustAlias(MemSet->getDest(), MemCpy->getDest()))
    return false;

  // Don't perform the transform if src_size may be zero. In that case, the
  // transform is essentially a complex no-op and may lead to an infinite
  // loop if BasicAA is smart enough to understand that dst and dst + src_size
  // are still MustAlias after the transform.
  Value *SrcSize = MemCpy->getLength();
  if (!isKnownNonZero(SrcSize,
                      SimplifyQuery(MemCpy->getDataLayout(), DT, AC, MemCpy)))
    return false;

  // Check that src and dst of the memcpy aren't the same. While memcpy
  // operands cannot partially overlap, exact equality is allowed.
  if (isModSet(BAA.getModRefInfo(MemCpy, MemoryLocation::getForSource(MemCpy))))
    return false;

  // We know that dst up to src_size is not written. We now need to make sure
  // that dst up to dst_size is not accessed. (If we did not move the memset,
  // checking for reads would be sufficient.)
  if (accessedBetween(BAA, MemoryLocation::getForDest(MemSet),
                      MSSA->getMemoryAccess(MemSet),
                      MSSA->getMemoryAccess(MemCpy)))
    return false;

  // Use the same i8* dest as the memcpy, killing the memset dest if different.
  Value *Dest = MemCpy->getRawDest();
  Value *DestSize = MemSet->getLength();

  if (mayBeVisibleThroughUnwinding(Dest, MemSet, MemCpy))
    return false;

  // If the sizes are the same, simply drop the memset instead of generating
  // a replacement with zero size.
  if (DestSize == SrcSize) {
    eraseInstruction(MemSet);
    return true;
  }

  // By default, create an unaligned memset.
  Align Alignment = Align(1);
  // If Dest is aligned, and SrcSize is constant, use the minimum alignment
  // of the sum.
  const Align DestAlign = std::max(MemSet->getDestAlign().valueOrOne(),
                                   MemCpy->getDestAlign().valueOrOne());
  if (DestAlign > 1)
    if (auto *SrcSizeC = dyn_cast<ConstantInt>(SrcSize))
      Alignment = commonAlignment(DestAlign, SrcSizeC->getZExtValue());

  IRBuilder<> Builder(MemCpy);

  // Preserve the debug location of the old memset for the code emitted here
  // related to the new memset. This is correct according to the rules in
  // https://llvm.org/docs/HowToUpdateDebugInfo.html about "when to preserve an
  // instruction location", given that we move the memset within the basic
  // block.
  assert(MemSet->getParent() == MemCpy->getParent() &&
         "Preserving debug location based on moving memset within BB.");
  Builder.SetCurrentDebugLocation(MemSet->getDebugLoc());

  // If the sizes have different types, zext the smaller one.
  if (DestSize->getType() != SrcSize->getType()) {
    if (DestSize->getType()->getIntegerBitWidth() >
        SrcSize->getType()->getIntegerBitWidth())
      SrcSize = Builder.CreateZExt(SrcSize, DestSize->getType());
    else
      DestSize = Builder.CreateZExt(DestSize, SrcSize->getType());
  }

  Value *Ule = Builder.CreateICmpULE(DestSize, SrcSize);
  Value *SizeDiff = Builder.CreateSub(DestSize, SrcSize);
  Value *MemsetLen = Builder.CreateSelect(
      Ule, ConstantInt::getNullValue(DestSize->getType()), SizeDiff);
  Instruction *NewMemSet =
      Builder.CreateMemSet(Builder.CreatePtrAdd(Dest, SrcSize),
                           MemSet->getOperand(1), MemsetLen, Alignment);

  assert(isa<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(MemCpy)) &&
         "MemCpy must be a MemoryDef");
  // The new memset is inserted before the memcpy, and it is known that the
  // memcpy's defining access is the memset about to be removed.
  auto *LastDef =
      cast<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(MemCpy));
  auto *NewAccess =
      MSSAU->createMemoryAccessBefore(NewMemSet, nullptr, LastDef);
  MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/true);

  eraseInstruction(MemSet);
  return true;
}

/// Determine whether the instruction has undefined content for the given Size,
/// either because it was freshly alloca'd or started its lifetime.
static bool hasUndefContents(MemorySSA *MSSA, BatchAAResults &AA, Value *V,
                             MemoryDef *Def, Value *Size) {
  if (MSSA->isLiveOnEntryDef(Def))
    return isa<AllocaInst>(getUnderlyingObject(V));

  if (auto *II = dyn_cast_or_null<IntrinsicInst>(Def->getMemoryInst())) {
    if (II->getIntrinsicID() == Intrinsic::lifetime_start) {
      auto *LTSize = cast<ConstantInt>(II->getArgOperand(0));

      if (auto *CSize = dyn_cast<ConstantInt>(Size)) {
        if (AA.isMustAlias(V, II->getArgOperand(1)) &&
            LTSize->getZExtValue() >= CSize->getZExtValue())
          return true;
      }

      // If the lifetime.start covers a whole alloca (as it almost always
      // does) and we're querying a pointer based on that alloca, then we know
      // the memory is definitely undef, regardless of how exactly we alias.
      // The size also doesn't matter, as an out-of-bounds access would be UB.
      if (auto *Alloca = dyn_cast<AllocaInst>(getUnderlyingObject(V))) {
        if (getUnderlyingObject(II->getArgOperand(1)) == Alloca) {
          const DataLayout &DL = Alloca->getDataLayout();
          if (std::optional<TypeSize> AllocaSize =
                  Alloca->getAllocationSize(DL))
            if (*AllocaSize == LTSize->getValue())
              return true;
        }
      }
    }
  }

  return false;
}

/// Transform memcpy to memset when its source was just memset.
/// In other words, turn:
/// \code
///   memset(dst1, c, dst1_size);
///   memcpy(dst2, dst1, dst2_size);
/// \endcode
/// into:
/// \code
///   memset(dst1, c, dst1_size);
///   memset(dst2, c, dst2_size);
/// \endcode
/// When dst2_size <= dst1_size.
bool MemCpyOptPass::performMemCpyToMemSetOptzn(MemCpyInst *MemCpy,
                                               MemSetInst *MemSet,
                                               BatchAAResults &BAA) {
  // Make sure that memcpy(..., memset(...), ...), that is we are memsetting and
  // memcpying from the same address. Otherwise it is hard to reason about.
  if (!BAA.isMustAlias(MemSet->getRawDest(), MemCpy->getRawSource()))
    return false;

  Value *MemSetSize = MemSet->getLength();
  Value *CopySize = MemCpy->getLength();

  if (MemSetSize != CopySize) {
    // Make sure the memcpy doesn't read any more than what the memset wrote.
    // Don't worry about sizes larger than i64.

    // A known memset size is required.
    auto *CMemSetSize = dyn_cast<ConstantInt>(MemSetSize);
    if (!CMemSetSize)
      return false;

    // A known memcpy size is also required.
    auto *CCopySize = dyn_cast<ConstantInt>(CopySize);
    if (!CCopySize)
      return false;
    if (CCopySize->getZExtValue() > CMemSetSize->getZExtValue()) {
      // If the memcpy is larger than the memset, but the memory was undef prior
      // to the memset, we can just ignore the tail. Technically we're only
      // interested in the bytes from MemSetSize..CopySize here, but as we can't
      // easily represent this location, we use the full 0..CopySize range.
      MemoryLocation MemCpyLoc = MemoryLocation::getForSource(MemCpy);
      bool CanReduceSize = false;
      MemoryUseOrDef *MemSetAccess = MSSA->getMemoryAccess(MemSet);
      MemoryAccess *Clobber = MSSA->getWalker()->getClobberingMemoryAccess(
          MemSetAccess->getDefiningAccess(), MemCpyLoc, BAA);
      if (auto *MD = dyn_cast<MemoryDef>(Clobber))
        if (hasUndefContents(MSSA, BAA, MemCpy->getSource(), MD, CopySize))
          CanReduceSize = true;

      if (!CanReduceSize)
        return false;
      CopySize = MemSetSize;
    }
  }

  IRBuilder<> Builder(MemCpy);
  Instruction *NewM =
      Builder.CreateMemSet(MemCpy->getRawDest(), MemSet->getOperand(1),
                           CopySize, MemCpy->getDestAlign());
  auto *LastDef =
      cast<MemoryDef>(MSSAU->getMemorySSA()->getMemoryAccess(MemCpy));
  auto *NewAccess = MSSAU->createMemoryAccessAfter(NewM, nullptr, LastDef);
  MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/true);

  return true;
}

// Attempts to optimize the pattern whereby memory is copied from an alloca to
// another alloca, where the two allocas don't have conflicting mod/ref. If
// successful, the two allocas can be merged into one and the transfer can be
// deleted. This pattern is generated frequently in Rust, due to the ubiquity of
// move operations in that language.
//
// Once we determine that the optimization is safe to perform, we replace all
// uses of the destination alloca with the source alloca. We also "shrink wrap"
// the lifetime markers of the single merged alloca to before the first use
// and after the last use. Note that the "shrink wrapping" procedure is a safe
// transformation only because we restrict the scope of this optimization to
// allocas that aren't captured.
bool MemCpyOptPass::performStackMoveOptzn(Instruction *Load, Instruction *Store,
                                          AllocaInst *DestAlloca,
                                          AllocaInst *SrcAlloca, TypeSize Size,
                                          BatchAAResults &BAA) {
  LLVM_DEBUG(dbgs() << "Stack Move: Attempting to optimize:\n"
                    << *Store << "\n");

  // Make sure the two allocas are in the same address space.
  if (SrcAlloca->getAddressSpace() != DestAlloca->getAddressSpace()) {
    LLVM_DEBUG(dbgs() << "Stack Move: Address space mismatch\n");
    return false;
  }

  // Check that copy is full with static size.
  const DataLayout &DL = DestAlloca->getDataLayout();
  std::optional<TypeSize> SrcSize = SrcAlloca->getAllocationSize(DL);
  if (!SrcSize || Size != *SrcSize) {
    LLVM_DEBUG(dbgs() << "Stack Move: Source alloca size mismatch\n");
    return false;
  }
  std::optional<TypeSize> DestSize = DestAlloca->getAllocationSize(DL);
  if (!DestSize || Size != *DestSize) {
    LLVM_DEBUG(dbgs() << "Stack Move: Destination alloca size mismatch\n");
    return false;
  }

  if (!SrcAlloca->isStaticAlloca() || !DestAlloca->isStaticAlloca())
    return false;

  // Check that src and dest are never captured, unescaped allocas. Also
  // find the nearest common dominator and postdominator for all users in
  // order to shrink wrap the lifetimes, and instructions with noalias metadata
  // to remove them.

  SmallVector<Instruction *, 4> LifetimeMarkers;
  SmallSet<Instruction *, 4> NoAliasInstrs;
  bool SrcNotDom = false;

  // Recursively track the user and check whether modified alias exist.
  auto IsDereferenceableOrNull = [](Value *V, const DataLayout &DL) -> bool {
    bool CanBeNull, CanBeFreed;
    return V->getPointerDereferenceableBytes(DL, CanBeNull, CanBeFreed);
  };

  auto CaptureTrackingWithModRef =
      [&](Instruction *AI,
          function_ref<bool(Instruction *)> ModRefCallback) -> bool {
    SmallVector<Instruction *, 8> Worklist;
    Worklist.push_back(AI);
    unsigned MaxUsesToExplore = getDefaultMaxUsesToExploreForCaptureTracking();
    Worklist.reserve(MaxUsesToExplore);
    SmallSet<const Use *, 20> Visited;
    while (!Worklist.empty()) {
      Instruction *I = Worklist.back();
      Worklist.pop_back();
      for (const Use &U : I->uses()) {
        auto *UI = cast<Instruction>(U.getUser());
        // If any use that isn't dominated by SrcAlloca exists, we move src
        // alloca to the entry before the transformation.
        if (!DT->dominates(SrcAlloca, UI))
          SrcNotDom = true;

        if (Visited.size() >= MaxUsesToExplore) {
          LLVM_DEBUG(
              dbgs()
              << "Stack Move: Exceeded max uses to see ModRef, bailing\n");
          return false;
        }
        if (!Visited.insert(&U).second)
          continue;
        switch (DetermineUseCaptureKind(U, IsDereferenceableOrNull)) {
        case UseCaptureKind::MAY_CAPTURE:
          return false;
        case UseCaptureKind::PASSTHROUGH:
          // Instructions cannot have non-instruction users.
          Worklist.push_back(UI);
          continue;
        case UseCaptureKind::NO_CAPTURE: {
          if (UI->isLifetimeStartOrEnd()) {
            // We note the locations of these intrinsic calls so that we can
            // delete them later if the optimization succeeds, this is safe
            // since both llvm.lifetime.start and llvm.lifetime.end intrinsics
            // practically fill all the bytes of the alloca with an undefined
            // value, although conceptually marked as alive/dead.
            int64_t Size = cast<ConstantInt>(UI->getOperand(0))->getSExtValue();
            if (Size < 0 || Size == DestSize) {
              LifetimeMarkers.push_back(UI);
              continue;
            }
          }
          if (UI->hasMetadata(LLVMContext::MD_noalias))
            NoAliasInstrs.insert(UI);
          if (!ModRefCallback(UI))
            return false;
        }
        }
      }
    }
    return true;
  };

  // Check that dest has no Mod/Ref, from the alloca to the Store, except full
  // size lifetime intrinsics. And collect modref inst for the reachability
  // check.
  ModRefInfo DestModRef = ModRefInfo::NoModRef;
  MemoryLocation DestLoc(DestAlloca, LocationSize::precise(Size));
  SmallVector<BasicBlock *, 8> ReachabilityWorklist;
  auto DestModRefCallback = [&](Instruction *UI) -> bool {
    // We don't care about the store itself.
    if (UI == Store)
      return true;
    ModRefInfo Res = BAA.getModRefInfo(UI, DestLoc);
    DestModRef |= Res;
    if (isModOrRefSet(Res)) {
      // Instructions reachability checks.
      // FIXME: adding the Instruction version isPotentiallyReachableFromMany on
      // lib/Analysis/CFG.cpp (currently only for BasicBlocks) might be helpful.
      if (UI->getParent() == Store->getParent()) {
        // The same block case is special because it's the only time we're
        // looking within a single block to see which instruction comes first.
        // Once we start looking at multiple blocks, the first instruction of
        // the block is reachable, so we only need to determine reachability
        // between whole blocks.
        BasicBlock *BB = UI->getParent();

        // If A comes before B, then B is definitively reachable from A.
        if (UI->comesBefore(Store))
          return false;

        // If the user's parent block is entry, no predecessor exists.
        if (BB->isEntryBlock())
          return true;

        // Otherwise, continue doing the normal per-BB CFG walk.
        ReachabilityWorklist.append(succ_begin(BB), succ_end(BB));
      } else {
        ReachabilityWorklist.push_back(UI->getParent());
      }
    }
    return true;
  };

  if (!CaptureTrackingWithModRef(DestAlloca, DestModRefCallback))
    return false;
  // Bailout if Dest may have any ModRef before Store.
  if (!ReachabilityWorklist.empty() &&
      isPotentiallyReachableFromMany(ReachabilityWorklist, Store->getParent(),
                                     nullptr, DT, nullptr))
    return false;

  // Check that, from after the Load to the end of the BB,
  //   - if the dest has any Mod, src has no Ref, and
  //   - if the dest has any Ref, src has no Mod except full-sized lifetimes.
  MemoryLocation SrcLoc(SrcAlloca, LocationSize::precise(Size));

  auto SrcModRefCallback = [&](Instruction *UI) -> bool {
    // Any ModRef post-dominated by Load doesn't matter, also Load and Store
    // themselves can be ignored.
    if (PDT->dominates(Load, UI) || UI == Load || UI == Store)
      return true;
    ModRefInfo Res = BAA.getModRefInfo(UI, SrcLoc);
    if ((isModSet(DestModRef) && isRefSet(Res)) ||
        (isRefSet(DestModRef) && isModSet(Res)))
      return false;

    return true;
  };

  if (!CaptureTrackingWithModRef(SrcAlloca, SrcModRefCallback))
    return false;

  // We can do the transformation. First, move the SrcAlloca to the start of the
  // BB.
  if (SrcNotDom)
    SrcAlloca->moveBefore(*SrcAlloca->getParent(),
                          SrcAlloca->getParent()->getFirstInsertionPt());
  // Align the allocas appropriately.
  SrcAlloca->setAlignment(
      std::max(SrcAlloca->getAlign(), DestAlloca->getAlign()));

  // Merge the two allocas.
  DestAlloca->replaceAllUsesWith(SrcAlloca);
  eraseInstruction(DestAlloca);

  // Drop metadata on the source alloca.
  SrcAlloca->dropUnknownNonDebugMetadata();

  // TODO: Reconstruct merged lifetime markers.
  // Remove all other lifetime markers. if the original lifetime intrinsics
  // exists.
  if (!LifetimeMarkers.empty()) {
    for (Instruction *I : LifetimeMarkers)
      eraseInstruction(I);
  }

  // As this transformation can cause memory accesses that didn't previously
  // alias to begin to alias one another, we remove !noalias metadata from any
  // uses of either alloca. This is conservative, but more precision doesn't
  // seem worthwhile right now.
  for (Instruction *I : NoAliasInstrs)
    I->setMetadata(LLVMContext::MD_noalias, nullptr);

  LLVM_DEBUG(dbgs() << "Stack Move: Performed staack-move optimization\n");
  NumStackMove++;
  return true;
}

static bool isZeroSize(Value *Size) {
  if (auto *I = dyn_cast<Instruction>(Size))
    if (auto *Res = simplifyInstruction(I, I->getDataLayout()))
      Size = Res;
  // Treat undef/poison size like zero.
  if (auto *C = dyn_cast<Constant>(Size))
    return isa<UndefValue>(C) || C->isNullValue();
  return false;
}

/// Perform simplification of memcpy's.  If we have memcpy A
/// which copies X to Y, and memcpy B which copies Y to Z, then we can rewrite
/// B to be a memcpy from X to Z (or potentially a memmove, depending on
/// circumstances). This allows later passes to remove the first memcpy
/// altogether.
bool MemCpyOptPass::processMemCpy(MemCpyInst *M, BasicBlock::iterator &BBI) {
  // We can only optimize non-volatile memcpy's.
  if (M->isVolatile())
    return false;

  // If the source and destination of the memcpy are the same, then zap it.
  if (M->getSource() == M->getDest()) {
    ++BBI;
    eraseInstruction(M);
    return true;
  }

  // If the size is zero, remove the memcpy.
  if (isZeroSize(M->getLength())) {
    ++BBI;
    eraseInstruction(M);
    return true;
  }

  MemoryUseOrDef *MA = MSSA->getMemoryAccess(M);
  if (!MA)
    // Degenerate case: memcpy marked as not accessing memory.
    return false;

  // If copying from a constant, try to turn the memcpy into a memset.
  if (auto *GV = dyn_cast<GlobalVariable>(M->getSource()))
    if (GV->isConstant() && GV->hasDefinitiveInitializer())
      if (Value *ByteVal = isBytewiseValue(GV->getInitializer(),
                                           M->getDataLayout())) {
        IRBuilder<> Builder(M);
        Instruction *NewM = Builder.CreateMemSet(
            M->getRawDest(), ByteVal, M->getLength(), M->getDestAlign(), false);
        auto *LastDef = cast<MemoryDef>(MA);
        auto *NewAccess =
            MSSAU->createMemoryAccessAfter(NewM, nullptr, LastDef);
        MSSAU->insertDef(cast<MemoryDef>(NewAccess), /*RenameUses=*/true);

        eraseInstruction(M);
        ++NumCpyToSet;
        return true;
      }

  BatchAAResults BAA(*AA);
  // FIXME: Not using getClobberingMemoryAccess() here due to PR54682.
  MemoryAccess *AnyClobber = MA->getDefiningAccess();
  MemoryLocation DestLoc = MemoryLocation::getForDest(M);
  const MemoryAccess *DestClobber =
      MSSA->getWalker()->getClobberingMemoryAccess(AnyClobber, DestLoc, BAA);

  // Try to turn a partially redundant memset + memcpy into
  // smaller memset + memcpy.  We don't need the memcpy size for this.
  // The memcpy must post-dom the memset, so limit this to the same basic
  // block. A non-local generalization is likely not worthwhile.
  if (auto *MD = dyn_cast<MemoryDef>(DestClobber))
    if (auto *MDep = dyn_cast_or_null<MemSetInst>(MD->getMemoryInst()))
      if (DestClobber->getBlock() == M->getParent())
        if (processMemSetMemCpyDependence(M, MDep, BAA))
          return true;

  MemoryAccess *SrcClobber = MSSA->getWalker()->getClobberingMemoryAccess(
      AnyClobber, MemoryLocation::getForSource(M), BAA);

  // There are five possible optimizations we can do for memcpy:
  //   a) memcpy-memcpy xform which exposes redundance for DSE.
  //   b) call-memcpy xform for return slot optimization.
  //   c) memcpy from freshly alloca'd space or space that has just started
  //      its lifetime copies undefined data, and we can therefore eliminate
  //      the memcpy in favor of the data that was already at the destination.
  //   d) memcpy from a just-memset'd source can be turned into memset.
  //   e) elimination of memcpy via stack-move optimization.
  if (auto *MD = dyn_cast<MemoryDef>(SrcClobber)) {
    if (Instruction *MI = MD->getMemoryInst()) {
      if (auto *CopySize = dyn_cast<ConstantInt>(M->getLength())) {
        if (auto *C = dyn_cast<CallInst>(MI)) {
          if (performCallSlotOptzn(M, M, M->getDest(), M->getSource(),
                                   TypeSize::getFixed(CopySize->getZExtValue()),
                                   M->getDestAlign().valueOrOne(), BAA,
                                   [C]() -> CallInst * { return C; })) {
            LLVM_DEBUG(dbgs() << "Performed call slot optimization:\n"
                              << "    call: " << *C << "\n"
                              << "    memcpy: " << *M << "\n");
            eraseInstruction(M);
            ++NumMemCpyInstr;
            return true;
          }
        }
      }
      if (auto *MDep = dyn_cast<MemCpyInst>(MI))
        if (processMemCpyMemCpyDependence(M, MDep, BAA))
          return true;
      if (auto *MDep = dyn_cast<MemSetInst>(MI)) {
        if (performMemCpyToMemSetOptzn(M, MDep, BAA)) {
          LLVM_DEBUG(dbgs() << "Converted memcpy to memset\n");
          eraseInstruction(M);
          ++NumCpyToSet;
          return true;
        }
      }
    }

    if (hasUndefContents(MSSA, BAA, M->getSource(), MD, M->getLength())) {
      LLVM_DEBUG(dbgs() << "Removed memcpy from undef\n");
      eraseInstruction(M);
      ++NumMemCpyInstr;
      return true;
    }
  }

  // If the transfer is from a stack slot to a stack slot, then we may be able
  // to perform the stack-move optimization. See the comments in
  // performStackMoveOptzn() for more details.
  auto *DestAlloca = dyn_cast<AllocaInst>(M->getDest());
  if (!DestAlloca)
    return false;
  auto *SrcAlloca = dyn_cast<AllocaInst>(M->getSource());
  if (!SrcAlloca)
    return false;
  ConstantInt *Len = dyn_cast<ConstantInt>(M->getLength());
  if (Len == nullptr)
    return false;
  if (performStackMoveOptzn(M, M, DestAlloca, SrcAlloca,
                            TypeSize::getFixed(Len->getZExtValue()), BAA)) {
    // Avoid invalidating the iterator.
    BBI = M->getNextNonDebugInstruction()->getIterator();
    eraseInstruction(M);
    ++NumMemCpyInstr;
    return true;
  }

  return false;
}

/// Transforms memmove calls to memcpy calls when the src/dst are guaranteed
/// not to alias.
bool MemCpyOptPass::processMemMove(MemMoveInst *M) {
  // See if the source could be modified by this memmove potentially.
  if (isModSet(AA->getModRefInfo(M, MemoryLocation::getForSource(M))))
    return false;

  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Optimizing memmove -> memcpy: " << *M
                    << "\n");

  // If not, then we know we can transform this.
  Type *ArgTys[3] = {M->getRawDest()->getType(), M->getRawSource()->getType(),
                     M->getLength()->getType()};
  M->setCalledFunction(
      Intrinsic::getDeclaration(M->getModule(), Intrinsic::memcpy, ArgTys));

  // For MemorySSA nothing really changes (except that memcpy may imply stricter
  // aliasing guarantees).

  ++NumMoveToCpy;
  return true;
}

/// This is called on every byval argument in call sites.
bool MemCpyOptPass::processByValArgument(CallBase &CB, unsigned ArgNo) {
  const DataLayout &DL = CB.getDataLayout();
  // Find out what feeds this byval argument.
  Value *ByValArg = CB.getArgOperand(ArgNo);
  Type *ByValTy = CB.getParamByValType(ArgNo);
  TypeSize ByValSize = DL.getTypeAllocSize(ByValTy);
  MemoryLocation Loc(ByValArg, LocationSize::precise(ByValSize));
  MemoryUseOrDef *CallAccess = MSSA->getMemoryAccess(&CB);
  if (!CallAccess)
    return false;
  MemCpyInst *MDep = nullptr;
  BatchAAResults BAA(*AA);
  MemoryAccess *Clobber = MSSA->getWalker()->getClobberingMemoryAccess(
      CallAccess->getDefiningAccess(), Loc, BAA);
  if (auto *MD = dyn_cast<MemoryDef>(Clobber))
    MDep = dyn_cast_or_null<MemCpyInst>(MD->getMemoryInst());

  // If the byval argument isn't fed by a memcpy, ignore it.  If it is fed by
  // a memcpy, see if we can byval from the source of the memcpy instead of the
  // result.
  if (!MDep || MDep->isVolatile() ||
      ByValArg->stripPointerCasts() != MDep->getDest())
    return false;

  // The length of the memcpy must be larger or equal to the size of the byval.
  auto *C1 = dyn_cast<ConstantInt>(MDep->getLength());
  if (!C1 || !TypeSize::isKnownGE(
                 TypeSize::getFixed(C1->getValue().getZExtValue()), ByValSize))
    return false;

  // Get the alignment of the byval.  If the call doesn't specify the alignment,
  // then it is some target specific value that we can't know.
  MaybeAlign ByValAlign = CB.getParamAlign(ArgNo);
  if (!ByValAlign)
    return false;

  // If it is greater than the memcpy, then we check to see if we can force the
  // source of the memcpy to the alignment we need.  If we fail, we bail out.
  MaybeAlign MemDepAlign = MDep->getSourceAlign();
  if ((!MemDepAlign || *MemDepAlign < *ByValAlign) &&
      getOrEnforceKnownAlignment(MDep->getSource(), ByValAlign, DL, &CB, AC,
                                 DT) < *ByValAlign)
    return false;

  // The type of the memcpy source must match the byval argument
  if (MDep->getSource()->getType() != ByValArg->getType())
    return false;

  // Verify that the copied-from memory doesn't change in between the memcpy and
  // the byval call.
  //    memcpy(a <- b)
  //    *b = 42;
  //    foo(*a)
  // It would be invalid to transform the second memcpy into foo(*b).
  if (writtenBetween(MSSA, BAA, MemoryLocation::getForSource(MDep),
                     MSSA->getMemoryAccess(MDep), CallAccess))
    return false;

  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Forwarding memcpy to byval:\n"
                    << "  " << *MDep << "\n"
                    << "  " << CB << "\n");

  // Otherwise we're good!  Update the byval argument.
  combineAAMetadata(&CB, MDep);
  CB.setArgOperand(ArgNo, MDep->getSource());
  ++NumMemCpyInstr;
  return true;
}

/// This is called on memcpy dest pointer arguments attributed as immutable
/// during call. Try to use memcpy source directly if all of the following
/// conditions are satisfied.
/// 1. The memcpy dst is neither modified during the call nor captured by the
/// call. (if readonly, noalias, nocapture attributes on call-site.)
/// 2. The memcpy dst is an alloca with known alignment & size.
///     2-1. The memcpy length == the alloca size which ensures that the new
///     pointer is dereferenceable for the required range
///     2-2. The src pointer has alignment >= the alloca alignment or can be
///     enforced so.
/// 3. The memcpy dst and src is not modified between the memcpy and the call.
/// (if MSSA clobber check is safe.)
/// 4. The memcpy src is not modified during the call. (ModRef check shows no
/// Mod.)
bool MemCpyOptPass::processImmutArgument(CallBase &CB, unsigned ArgNo) {
  // 1. Ensure passed argument is immutable during call.
  if (!(CB.paramHasAttr(ArgNo, Attribute::NoAlias) &&
        CB.paramHasAttr(ArgNo, Attribute::NoCapture)))
    return false;
  const DataLayout &DL = CB.getDataLayout();
  Value *ImmutArg = CB.getArgOperand(ArgNo);

  // 2. Check that arg is alloca
  // TODO: Even if the arg gets back to branches, we can remove memcpy if all
  // the alloca alignments can be enforced to source alignment.
  auto *AI = dyn_cast<AllocaInst>(ImmutArg->stripPointerCasts());
  if (!AI)
    return false;

  std::optional<TypeSize> AllocaSize = AI->getAllocationSize(DL);
  // Can't handle unknown size alloca.
  // (e.g. Variable Length Array, Scalable Vector)
  if (!AllocaSize || AllocaSize->isScalable())
    return false;
  MemoryLocation Loc(ImmutArg, LocationSize::precise(*AllocaSize));
  MemoryUseOrDef *CallAccess = MSSA->getMemoryAccess(&CB);
  if (!CallAccess)
    return false;

  MemCpyInst *MDep = nullptr;
  BatchAAResults BAA(*AA);
  MemoryAccess *Clobber = MSSA->getWalker()->getClobberingMemoryAccess(
      CallAccess->getDefiningAccess(), Loc, BAA);
  if (auto *MD = dyn_cast<MemoryDef>(Clobber))
    MDep = dyn_cast_or_null<MemCpyInst>(MD->getMemoryInst());

  // If the immut argument isn't fed by a memcpy, ignore it.  If it is fed by
  // a memcpy, check that the arg equals the memcpy dest.
  if (!MDep || MDep->isVolatile() || AI != MDep->getDest())
    return false;

  // The type of the memcpy source must match the immut argument
  if (MDep->getSource()->getType() != ImmutArg->getType())
    return false;

  // 2-1. The length of the memcpy must be equal to the size of the alloca.
  auto *MDepLen = dyn_cast<ConstantInt>(MDep->getLength());
  if (!MDepLen || AllocaSize != MDepLen->getValue())
    return false;

  // 2-2. the memcpy source align must be larger than or equal the alloca's
  // align. If not so, we check to see if we can force the source of the memcpy
  // to the alignment we need. If we fail, we bail out.
  Align MemDepAlign = MDep->getSourceAlign().valueOrOne();
  Align AllocaAlign = AI->getAlign();
  if (MemDepAlign < AllocaAlign &&
      getOrEnforceKnownAlignment(MDep->getSource(), AllocaAlign, DL, &CB, AC,
                                 DT) < AllocaAlign)
    return false;

  // 3. Verify that the source doesn't change in between the memcpy and
  // the call.
  //    memcpy(a <- b)
  //    *b = 42;
  //    foo(*a)
  // It would be invalid to transform the second memcpy into foo(*b).
  if (writtenBetween(MSSA, BAA, MemoryLocation::getForSource(MDep),
                     MSSA->getMemoryAccess(MDep), CallAccess))
    return false;

  // 4. The memcpy src must not be modified during the call.
  if (isModSet(AA->getModRefInfo(&CB, MemoryLocation::getForSource(MDep))))
    return false;

  LLVM_DEBUG(dbgs() << "MemCpyOptPass: Forwarding memcpy to Immut src:\n"
                    << "  " << *MDep << "\n"
                    << "  " << CB << "\n");

  // Otherwise we're good!  Update the immut argument.
  combineAAMetadata(&CB, MDep);
  CB.setArgOperand(ArgNo, MDep->getSource());
  ++NumMemCpyInstr;
  return true;
}

/// Executes one iteration of MemCpyOptPass.
bool MemCpyOptPass::iterateOnFunction(Function &F) {
  bool MadeChange = false;

  // Walk all instruction in the function.
  for (BasicBlock &BB : F) {
    // Skip unreachable blocks. For example processStore assumes that an
    // instruction in a BB can't be dominated by a later instruction in the
    // same BB (which is a scenario that can happen for an unreachable BB that
    // has itself as a predecessor).
    if (!DT->isReachableFromEntry(&BB))
      continue;

    for (BasicBlock::iterator BI = BB.begin(), BE = BB.end(); BI != BE;) {
      // Avoid invalidating the iterator.
      Instruction *I = &*BI++;

      bool RepeatInstruction = false;

      if (auto *SI = dyn_cast<StoreInst>(I))
        MadeChange |= processStore(SI, BI);
      else if (auto *M = dyn_cast<MemSetInst>(I))
        RepeatInstruction = processMemSet(M, BI);
      else if (auto *M = dyn_cast<MemCpyInst>(I))
        RepeatInstruction = processMemCpy(M, BI);
      else if (auto *M = dyn_cast<MemMoveInst>(I))
        RepeatInstruction = processMemMove(M);
      else if (auto *CB = dyn_cast<CallBase>(I)) {
        for (unsigned i = 0, e = CB->arg_size(); i != e; ++i) {
          if (CB->isByValArgument(i))
            MadeChange |= processByValArgument(*CB, i);
          else if (CB->onlyReadsMemory(i))
            MadeChange |= processImmutArgument(*CB, i);
        }
      }

      // Reprocess the instruction if desired.
      if (RepeatInstruction) {
        if (BI != BB.begin())
          --BI;
        MadeChange = true;
      }
    }
  }

  return MadeChange;
}

PreservedAnalyses MemCpyOptPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto &TLI = AM.getResult<TargetLibraryAnalysis>(F);
  auto *AA = &AM.getResult<AAManager>(F);
  auto *AC = &AM.getResult<AssumptionAnalysis>(F);
  auto *DT = &AM.getResult<DominatorTreeAnalysis>(F);
  auto *PDT = &AM.getResult<PostDominatorTreeAnalysis>(F);
  auto *MSSA = &AM.getResult<MemorySSAAnalysis>(F);

  bool MadeChange = runImpl(F, &TLI, AA, AC, DT, PDT, &MSSA->getMSSA());
  if (!MadeChange)
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserveSet<CFGAnalyses>();
  PA.preserve<MemorySSAAnalysis>();
  return PA;
}

bool MemCpyOptPass::runImpl(Function &F, TargetLibraryInfo *TLI_,
                            AliasAnalysis *AA_, AssumptionCache *AC_,
                            DominatorTree *DT_, PostDominatorTree *PDT_,
                            MemorySSA *MSSA_) {
  bool MadeChange = false;
  TLI = TLI_;
  AA = AA_;
  AC = AC_;
  DT = DT_;
  PDT = PDT_;
  MSSA = MSSA_;
  MemorySSAUpdater MSSAU_(MSSA_);
  MSSAU = &MSSAU_;

  while (true) {
    if (!iterateOnFunction(F))
      break;
    MadeChange = true;
  }

  if (VerifyMemorySSA)
    MSSA_->verifyMemorySSA();

  return MadeChange;
}
