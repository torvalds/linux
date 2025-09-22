//===- SROA.cpp - Scalar Replacement Of Aggregates ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This transformation implements the well known scalar replacement of
/// aggregates transformation. It tries to identify promotable elements of an
/// aggregate alloca, and promote them to registers. It will also try to
/// convert uses of an element (or set of elements) of an alloca into a vector
/// or bitfield-style integer scalar if appropriate.
///
/// It works to do this with minimal slicing of the alloca so that regions
/// which are merely transferred in and out of external memory remain unchanged
/// and are not decomposed to scalar code.
///
/// Because this also performs alloca promotion, it can be thought of as also
/// serving the purpose of SSA formation. The algorithm iterates on the
/// function until all opportunities for promotion have been realized.
///
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/SROA.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/DomTreeUpdater.h"
#include "llvm/Analysis/GlobalsModRef.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/PtrUseVisitor.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantFolder.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace llvm;

#define DEBUG_TYPE "sroa"

STATISTIC(NumAllocasAnalyzed, "Number of allocas analyzed for replacement");
STATISTIC(NumAllocaPartitions, "Number of alloca partitions formed");
STATISTIC(MaxPartitionsPerAlloca, "Maximum number of partitions per alloca");
STATISTIC(NumAllocaPartitionUses, "Number of alloca partition uses rewritten");
STATISTIC(MaxUsesPerAllocaPartition, "Maximum number of uses of a partition");
STATISTIC(NumNewAllocas, "Number of new, smaller allocas introduced");
STATISTIC(NumPromoted, "Number of allocas promoted to SSA values");
STATISTIC(NumLoadsSpeculated, "Number of loads speculated to allow promotion");
STATISTIC(NumLoadsPredicated,
          "Number of loads rewritten into predicated loads to allow promotion");
STATISTIC(
    NumStoresPredicated,
    "Number of stores rewritten into predicated loads to allow promotion");
STATISTIC(NumDeleted, "Number of instructions deleted");
STATISTIC(NumVectorized, "Number of vectorized aggregates");

/// Disable running mem2reg during SROA in order to test or debug SROA.
static cl::opt<bool> SROASkipMem2Reg("sroa-skip-mem2reg", cl::init(false),
                                     cl::Hidden);
namespace {

class AllocaSliceRewriter;
class AllocaSlices;
class Partition;

class SelectHandSpeculativity {
  unsigned char Storage = 0; // None are speculatable by default.
  using TrueVal = Bitfield::Element<bool, 0, 1>;  // Low 0'th bit.
  using FalseVal = Bitfield::Element<bool, 1, 1>; // Low 1'th bit.
public:
  SelectHandSpeculativity() = default;
  SelectHandSpeculativity &setAsSpeculatable(bool isTrueVal);
  bool isSpeculatable(bool isTrueVal) const;
  bool areAllSpeculatable() const;
  bool areAnySpeculatable() const;
  bool areNoneSpeculatable() const;
  // For interop as int half of PointerIntPair.
  explicit operator intptr_t() const { return static_cast<intptr_t>(Storage); }
  explicit SelectHandSpeculativity(intptr_t Storage_) : Storage(Storage_) {}
};
static_assert(sizeof(SelectHandSpeculativity) == sizeof(unsigned char));

using PossiblySpeculatableLoad =
    PointerIntPair<LoadInst *, 2, SelectHandSpeculativity>;
using UnspeculatableStore = StoreInst *;
using RewriteableMemOp =
    std::variant<PossiblySpeculatableLoad, UnspeculatableStore>;
using RewriteableMemOps = SmallVector<RewriteableMemOp, 2>;

/// An optimization pass providing Scalar Replacement of Aggregates.
///
/// This pass takes allocations which can be completely analyzed (that is, they
/// don't escape) and tries to turn them into scalar SSA values. There are
/// a few steps to this process.
///
/// 1) It takes allocations of aggregates and analyzes the ways in which they
///    are used to try to split them into smaller allocations, ideally of
///    a single scalar data type. It will split up memcpy and memset accesses
///    as necessary and try to isolate individual scalar accesses.
/// 2) It will transform accesses into forms which are suitable for SSA value
///    promotion. This can be replacing a memset with a scalar store of an
///    integer value, or it can involve speculating operations on a PHI or
///    select to be a PHI or select of the results.
/// 3) Finally, this will try to detect a pattern of accesses which map cleanly
///    onto insert and extract operations on a vector value, and convert them to
///    this form. By doing so, it will enable promotion of vector aggregates to
///    SSA vector values.
class SROA {
  LLVMContext *const C;
  DomTreeUpdater *const DTU;
  AssumptionCache *const AC;
  const bool PreserveCFG;

  /// Worklist of alloca instructions to simplify.
  ///
  /// Each alloca in the function is added to this. Each new alloca formed gets
  /// added to it as well to recursively simplify unless that alloca can be
  /// directly promoted. Finally, each time we rewrite a use of an alloca other
  /// the one being actively rewritten, we add it back onto the list if not
  /// already present to ensure it is re-visited.
  SmallSetVector<AllocaInst *, 16> Worklist;

  /// A collection of instructions to delete.
  /// We try to batch deletions to simplify code and make things a bit more
  /// efficient. We also make sure there is no dangling pointers.
  SmallVector<WeakVH, 8> DeadInsts;

  /// Post-promotion worklist.
  ///
  /// Sometimes we discover an alloca which has a high probability of becoming
  /// viable for SROA after a round of promotion takes place. In those cases,
  /// the alloca is enqueued here for re-processing.
  ///
  /// Note that we have to be very careful to clear allocas out of this list in
  /// the event they are deleted.
  SmallSetVector<AllocaInst *, 16> PostPromotionWorklist;

  /// A collection of alloca instructions we can directly promote.
  std::vector<AllocaInst *> PromotableAllocas;

  /// A worklist of PHIs to speculate prior to promoting allocas.
  ///
  /// All of these PHIs have been checked for the safety of speculation and by
  /// being speculated will allow promoting allocas currently in the promotable
  /// queue.
  SmallSetVector<PHINode *, 8> SpeculatablePHIs;

  /// A worklist of select instructions to rewrite prior to promoting
  /// allocas.
  SmallMapVector<SelectInst *, RewriteableMemOps, 8> SelectsToRewrite;

  /// Select instructions that use an alloca and are subsequently loaded can be
  /// rewritten to load both input pointers and then select between the result,
  /// allowing the load of the alloca to be promoted.
  /// From this:
  ///   %P2 = select i1 %cond, ptr %Alloca, ptr %Other
  ///   %V = load <type>, ptr %P2
  /// to:
  ///   %V1 = load <type>, ptr %Alloca      -> will be mem2reg'd
  ///   %V2 = load <type>, ptr %Other
  ///   %V = select i1 %cond, <type> %V1, <type> %V2
  ///
  /// We can do this to a select if its only uses are loads
  /// and if either the operand to the select can be loaded unconditionally,
  ///        or if we are allowed to perform CFG modifications.
  /// If found an intervening bitcast with a single use of the load,
  /// allow the promotion.
  static std::optional<RewriteableMemOps>
  isSafeSelectToSpeculate(SelectInst &SI, bool PreserveCFG);

public:
  SROA(LLVMContext *C, DomTreeUpdater *DTU, AssumptionCache *AC,
       SROAOptions PreserveCFG_)
      : C(C), DTU(DTU), AC(AC),
        PreserveCFG(PreserveCFG_ == SROAOptions::PreserveCFG) {}

  /// Main run method used by both the SROAPass and by the legacy pass.
  std::pair<bool /*Changed*/, bool /*CFGChanged*/> runSROA(Function &F);

private:
  friend class AllocaSliceRewriter;

  bool presplitLoadsAndStores(AllocaInst &AI, AllocaSlices &AS);
  AllocaInst *rewritePartition(AllocaInst &AI, AllocaSlices &AS, Partition &P);
  bool splitAlloca(AllocaInst &AI, AllocaSlices &AS);
  std::pair<bool /*Changed*/, bool /*CFGChanged*/> runOnAlloca(AllocaInst &AI);
  void clobberUse(Use &U);
  bool deleteDeadInstructions(SmallPtrSetImpl<AllocaInst *> &DeletedAllocas);
  bool promoteAllocas(Function &F);
};

} // end anonymous namespace

/// Calculate the fragment of a variable to use when slicing a store
/// based on the slice dimensions, existing fragment, and base storage
/// fragment.
/// Results:
/// UseFrag - Use Target as the new fragment.
/// UseNoFrag - The new slice already covers the whole variable.
/// Skip - The new alloca slice doesn't include this variable.
/// FIXME: Can we use calculateFragmentIntersect instead?
namespace {
enum FragCalcResult { UseFrag, UseNoFrag, Skip };
}
static FragCalcResult
calculateFragment(DILocalVariable *Variable,
                  uint64_t NewStorageSliceOffsetInBits,
                  uint64_t NewStorageSliceSizeInBits,
                  std::optional<DIExpression::FragmentInfo> StorageFragment,
                  std::optional<DIExpression::FragmentInfo> CurrentFragment,
                  DIExpression::FragmentInfo &Target) {
  // If the base storage describes part of the variable apply the offset and
  // the size constraint.
  if (StorageFragment) {
    Target.SizeInBits =
        std::min(NewStorageSliceSizeInBits, StorageFragment->SizeInBits);
    Target.OffsetInBits =
        NewStorageSliceOffsetInBits + StorageFragment->OffsetInBits;
  } else {
    Target.SizeInBits = NewStorageSliceSizeInBits;
    Target.OffsetInBits = NewStorageSliceOffsetInBits;
  }

  // If this slice extracts the entirety of an independent variable from a
  // larger alloca, do not produce a fragment expression, as the variable is
  // not fragmented.
  if (!CurrentFragment) {
    if (auto Size = Variable->getSizeInBits()) {
      // Treat the current fragment as covering the whole variable.
      CurrentFragment = DIExpression::FragmentInfo(*Size, 0);
      if (Target == CurrentFragment)
        return UseNoFrag;
    }
  }

  // No additional work to do if there isn't a fragment already, or there is
  // but it already exactly describes the new assignment.
  if (!CurrentFragment || *CurrentFragment == Target)
    return UseFrag;

  // Reject the target fragment if it doesn't fit wholly within the current
  // fragment. TODO: We could instead chop up the target to fit in the case of
  // a partial overlap.
  if (Target.startInBits() < CurrentFragment->startInBits() ||
      Target.endInBits() > CurrentFragment->endInBits())
    return Skip;

  // Target fits within the current fragment, return it.
  return UseFrag;
}

static DebugVariable getAggregateVariable(DbgVariableIntrinsic *DVI) {
  return DebugVariable(DVI->getVariable(), std::nullopt,
                       DVI->getDebugLoc().getInlinedAt());
}
static DebugVariable getAggregateVariable(DbgVariableRecord *DVR) {
  return DebugVariable(DVR->getVariable(), std::nullopt,
                       DVR->getDebugLoc().getInlinedAt());
}

/// Helpers for handling new and old debug info modes in migrateDebugInfo.
/// These overloads unwrap a DbgInstPtr {Instruction* | DbgRecord*} union based
/// on the \p Unused parameter type.
DbgVariableRecord *UnwrapDbgInstPtr(DbgInstPtr P, DbgVariableRecord *Unused) {
  (void)Unused;
  return static_cast<DbgVariableRecord *>(cast<DbgRecord *>(P));
}
DbgAssignIntrinsic *UnwrapDbgInstPtr(DbgInstPtr P, DbgAssignIntrinsic *Unused) {
  (void)Unused;
  return static_cast<DbgAssignIntrinsic *>(cast<Instruction *>(P));
}

/// Find linked dbg.assign and generate a new one with the correct
/// FragmentInfo. Link Inst to the new dbg.assign.  If Value is nullptr the
/// value component is copied from the old dbg.assign to the new.
/// \param OldAlloca             Alloca for the variable before splitting.
/// \param IsSplit               True if the store (not necessarily alloca)
///                              is being split.
/// \param OldAllocaOffsetInBits Offset of the slice taken from OldAlloca.
/// \param SliceSizeInBits       New number of bits being written to.
/// \param OldInst               Instruction that is being split.
/// \param Inst                  New instruction performing this part of the
///                              split store.
/// \param Dest                  Store destination.
/// \param Value                 Stored value.
/// \param DL                    Datalayout.
static void migrateDebugInfo(AllocaInst *OldAlloca, bool IsSplit,
                             uint64_t OldAllocaOffsetInBits,
                             uint64_t SliceSizeInBits, Instruction *OldInst,
                             Instruction *Inst, Value *Dest, Value *Value,
                             const DataLayout &DL) {
  auto MarkerRange = at::getAssignmentMarkers(OldInst);
  auto DVRAssignMarkerRange = at::getDVRAssignmentMarkers(OldInst);
  // Nothing to do if OldInst has no linked dbg.assign intrinsics.
  if (MarkerRange.empty() && DVRAssignMarkerRange.empty())
    return;

  LLVM_DEBUG(dbgs() << "  migrateDebugInfo\n");
  LLVM_DEBUG(dbgs() << "    OldAlloca: " << *OldAlloca << "\n");
  LLVM_DEBUG(dbgs() << "    IsSplit: " << IsSplit << "\n");
  LLVM_DEBUG(dbgs() << "    OldAllocaOffsetInBits: " << OldAllocaOffsetInBits
                    << "\n");
  LLVM_DEBUG(dbgs() << "    SliceSizeInBits: " << SliceSizeInBits << "\n");
  LLVM_DEBUG(dbgs() << "    OldInst: " << *OldInst << "\n");
  LLVM_DEBUG(dbgs() << "    Inst: " << *Inst << "\n");
  LLVM_DEBUG(dbgs() << "    Dest: " << *Dest << "\n");
  if (Value)
    LLVM_DEBUG(dbgs() << "    Value: " << *Value << "\n");

  /// Map of aggregate variables to their fragment associated with OldAlloca.
  DenseMap<DebugVariable, std::optional<DIExpression::FragmentInfo>>
      BaseFragments;
  for (auto *DAI : at::getAssignmentMarkers(OldAlloca))
    BaseFragments[getAggregateVariable(DAI)] =
        DAI->getExpression()->getFragmentInfo();
  for (auto *DVR : at::getDVRAssignmentMarkers(OldAlloca))
    BaseFragments[getAggregateVariable(DVR)] =
        DVR->getExpression()->getFragmentInfo();

  // The new inst needs a DIAssignID unique metadata tag (if OldInst has
  // one). It shouldn't already have one: assert this assumption.
  assert(!Inst->getMetadata(LLVMContext::MD_DIAssignID));
  DIAssignID *NewID = nullptr;
  auto &Ctx = Inst->getContext();
  DIBuilder DIB(*OldInst->getModule(), /*AllowUnresolved*/ false);
  assert(OldAlloca->isStaticAlloca());

  auto MigrateDbgAssign = [&](auto *DbgAssign) {
    LLVM_DEBUG(dbgs() << "      existing dbg.assign is: " << *DbgAssign
                      << "\n");
    auto *Expr = DbgAssign->getExpression();
    bool SetKillLocation = false;

    if (IsSplit) {
      std::optional<DIExpression::FragmentInfo> BaseFragment;
      {
        auto R = BaseFragments.find(getAggregateVariable(DbgAssign));
        if (R == BaseFragments.end())
          return;
        BaseFragment = R->second;
      }
      std::optional<DIExpression::FragmentInfo> CurrentFragment =
          Expr->getFragmentInfo();
      DIExpression::FragmentInfo NewFragment;
      FragCalcResult Result = calculateFragment(
          DbgAssign->getVariable(), OldAllocaOffsetInBits, SliceSizeInBits,
          BaseFragment, CurrentFragment, NewFragment);

      if (Result == Skip)
        return;
      if (Result == UseFrag && !(NewFragment == CurrentFragment)) {
        if (CurrentFragment) {
          // Rewrite NewFragment to be relative to the existing one (this is
          // what createFragmentExpression wants).  CalculateFragment has
          // already resolved the size for us. FIXME: Should it return the
          // relative fragment too?
          NewFragment.OffsetInBits -= CurrentFragment->OffsetInBits;
        }
        // Add the new fragment info to the existing expression if possible.
        if (auto E = DIExpression::createFragmentExpression(
                Expr, NewFragment.OffsetInBits, NewFragment.SizeInBits)) {
          Expr = *E;
        } else {
          // Otherwise, add the new fragment info to an empty expression and
          // discard the value component of this dbg.assign as the value cannot
          // be computed with the new fragment.
          Expr = *DIExpression::createFragmentExpression(
              DIExpression::get(Expr->getContext(), std::nullopt),
              NewFragment.OffsetInBits, NewFragment.SizeInBits);
          SetKillLocation = true;
        }
      }
    }

    // If we haven't created a DIAssignID ID do that now and attach it to Inst.
    if (!NewID) {
      NewID = DIAssignID::getDistinct(Ctx);
      Inst->setMetadata(LLVMContext::MD_DIAssignID, NewID);
    }

    ::Value *NewValue = Value ? Value : DbgAssign->getValue();
    auto *NewAssign = UnwrapDbgInstPtr(
        DIB.insertDbgAssign(Inst, NewValue, DbgAssign->getVariable(), Expr,
                            Dest,
                            DIExpression::get(Expr->getContext(), std::nullopt),
                            DbgAssign->getDebugLoc()),
        DbgAssign);

    // If we've updated the value but the original dbg.assign has an arglist
    // then kill it now - we can't use the requested new value.
    // We can't replace the DIArgList with the new value as it'd leave
    // the DIExpression in an invalid state (DW_OP_LLVM_arg operands without
    // an arglist). And we can't keep the DIArgList in case the linked store
    // is being split - in which case the DIArgList + expression may no longer
    // be computing the correct value.
    // This should be a very rare situation as it requires the value being
    // stored to differ from the dbg.assign (i.e., the value has been
    // represented differently in the debug intrinsic for some reason).
    SetKillLocation |=
        Value && (DbgAssign->hasArgList() ||
                  !DbgAssign->getExpression()->isSingleLocationExpression());
    if (SetKillLocation)
      NewAssign->setKillLocation();

    // We could use more precision here at the cost of some additional (code)
    // complexity - if the original dbg.assign was adjacent to its store, we
    // could position this new dbg.assign adjacent to its store rather than the
    // old dbg.assgn. That would result in interleaved dbg.assigns rather than
    // what we get now:
    //    split store !1
    //    split store !2
    //    dbg.assign !1
    //    dbg.assign !2
    // This (current behaviour) results results in debug assignments being
    // noted as slightly offset (in code) from the store. In practice this
    // should have little effect on the debugging experience due to the fact
    // that all the split stores should get the same line number.
    NewAssign->moveBefore(DbgAssign);

    NewAssign->setDebugLoc(DbgAssign->getDebugLoc());
    LLVM_DEBUG(dbgs() << "Created new assign: " << *NewAssign << "\n");
  };

  for_each(MarkerRange, MigrateDbgAssign);
  for_each(DVRAssignMarkerRange, MigrateDbgAssign);
}

namespace {

/// A custom IRBuilder inserter which prefixes all names, but only in
/// Assert builds.
class IRBuilderPrefixedInserter final : public IRBuilderDefaultInserter {
  std::string Prefix;

  Twine getNameWithPrefix(const Twine &Name) const {
    return Name.isTriviallyEmpty() ? Name : Prefix + Name;
  }

public:
  void SetNamePrefix(const Twine &P) { Prefix = P.str(); }

  void InsertHelper(Instruction *I, const Twine &Name,
                    BasicBlock::iterator InsertPt) const override {
    IRBuilderDefaultInserter::InsertHelper(I, getNameWithPrefix(Name),
                                           InsertPt);
  }
};

/// Provide a type for IRBuilder that drops names in release builds.
using IRBuilderTy = IRBuilder<ConstantFolder, IRBuilderPrefixedInserter>;

/// A used slice of an alloca.
///
/// This structure represents a slice of an alloca used by some instruction. It
/// stores both the begin and end offsets of this use, a pointer to the use
/// itself, and a flag indicating whether we can classify the use as splittable
/// or not when forming partitions of the alloca.
class Slice {
  /// The beginning offset of the range.
  uint64_t BeginOffset = 0;

  /// The ending offset, not included in the range.
  uint64_t EndOffset = 0;

  /// Storage for both the use of this slice and whether it can be
  /// split.
  PointerIntPair<Use *, 1, bool> UseAndIsSplittable;

public:
  Slice() = default;

  Slice(uint64_t BeginOffset, uint64_t EndOffset, Use *U, bool IsSplittable)
      : BeginOffset(BeginOffset), EndOffset(EndOffset),
        UseAndIsSplittable(U, IsSplittable) {}

  uint64_t beginOffset() const { return BeginOffset; }
  uint64_t endOffset() const { return EndOffset; }

  bool isSplittable() const { return UseAndIsSplittable.getInt(); }
  void makeUnsplittable() { UseAndIsSplittable.setInt(false); }

  Use *getUse() const { return UseAndIsSplittable.getPointer(); }

  bool isDead() const { return getUse() == nullptr; }
  void kill() { UseAndIsSplittable.setPointer(nullptr); }

  /// Support for ordering ranges.
  ///
  /// This provides an ordering over ranges such that start offsets are
  /// always increasing, and within equal start offsets, the end offsets are
  /// decreasing. Thus the spanning range comes first in a cluster with the
  /// same start position.
  bool operator<(const Slice &RHS) const {
    if (beginOffset() < RHS.beginOffset())
      return true;
    if (beginOffset() > RHS.beginOffset())
      return false;
    if (isSplittable() != RHS.isSplittable())
      return !isSplittable();
    if (endOffset() > RHS.endOffset())
      return true;
    return false;
  }

  /// Support comparison with a single offset to allow binary searches.
  friend LLVM_ATTRIBUTE_UNUSED bool operator<(const Slice &LHS,
                                              uint64_t RHSOffset) {
    return LHS.beginOffset() < RHSOffset;
  }
  friend LLVM_ATTRIBUTE_UNUSED bool operator<(uint64_t LHSOffset,
                                              const Slice &RHS) {
    return LHSOffset < RHS.beginOffset();
  }

  bool operator==(const Slice &RHS) const {
    return isSplittable() == RHS.isSplittable() &&
           beginOffset() == RHS.beginOffset() && endOffset() == RHS.endOffset();
  }
  bool operator!=(const Slice &RHS) const { return !operator==(RHS); }
};

/// Representation of the alloca slices.
///
/// This class represents the slices of an alloca which are formed by its
/// various uses. If a pointer escapes, we can't fully build a representation
/// for the slices used and we reflect that in this structure. The uses are
/// stored, sorted by increasing beginning offset and with unsplittable slices
/// starting at a particular offset before splittable slices.
class AllocaSlices {
public:
  /// Construct the slices of a particular alloca.
  AllocaSlices(const DataLayout &DL, AllocaInst &AI);

  /// Test whether a pointer to the allocation escapes our analysis.
  ///
  /// If this is true, the slices are never fully built and should be
  /// ignored.
  bool isEscaped() const { return PointerEscapingInstr; }

  /// Support for iterating over the slices.
  /// @{
  using iterator = SmallVectorImpl<Slice>::iterator;
  using range = iterator_range<iterator>;

  iterator begin() { return Slices.begin(); }
  iterator end() { return Slices.end(); }

  using const_iterator = SmallVectorImpl<Slice>::const_iterator;
  using const_range = iterator_range<const_iterator>;

  const_iterator begin() const { return Slices.begin(); }
  const_iterator end() const { return Slices.end(); }
  /// @}

  /// Erase a range of slices.
  void erase(iterator Start, iterator Stop) { Slices.erase(Start, Stop); }

  /// Insert new slices for this alloca.
  ///
  /// This moves the slices into the alloca's slices collection, and re-sorts
  /// everything so that the usual ordering properties of the alloca's slices
  /// hold.
  void insert(ArrayRef<Slice> NewSlices) {
    int OldSize = Slices.size();
    Slices.append(NewSlices.begin(), NewSlices.end());
    auto SliceI = Slices.begin() + OldSize;
    std::stable_sort(SliceI, Slices.end());
    std::inplace_merge(Slices.begin(), SliceI, Slices.end());
  }

  // Forward declare the iterator and range accessor for walking the
  // partitions.
  class partition_iterator;
  iterator_range<partition_iterator> partitions();

  /// Access the dead users for this alloca.
  ArrayRef<Instruction *> getDeadUsers() const { return DeadUsers; }

  /// Access Uses that should be dropped if the alloca is promotable.
  ArrayRef<Use *> getDeadUsesIfPromotable() const {
    return DeadUseIfPromotable;
  }

  /// Access the dead operands referring to this alloca.
  ///
  /// These are operands which have cannot actually be used to refer to the
  /// alloca as they are outside its range and the user doesn't correct for
  /// that. These mostly consist of PHI node inputs and the like which we just
  /// need to replace with undef.
  ArrayRef<Use *> getDeadOperands() const { return DeadOperands; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void print(raw_ostream &OS, const_iterator I, StringRef Indent = "  ") const;
  void printSlice(raw_ostream &OS, const_iterator I,
                  StringRef Indent = "  ") const;
  void printUse(raw_ostream &OS, const_iterator I,
                StringRef Indent = "  ") const;
  void print(raw_ostream &OS) const;
  void dump(const_iterator I) const;
  void dump() const;
#endif

private:
  template <typename DerivedT, typename RetT = void> class BuilderBase;
  class SliceBuilder;

  friend class AllocaSlices::SliceBuilder;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Handle to alloca instruction to simplify method interfaces.
  AllocaInst &AI;
#endif

  /// The instruction responsible for this alloca not having a known set
  /// of slices.
  ///
  /// When an instruction (potentially) escapes the pointer to the alloca, we
  /// store a pointer to that here and abort trying to form slices of the
  /// alloca. This will be null if the alloca slices are analyzed successfully.
  Instruction *PointerEscapingInstr;

  /// The slices of the alloca.
  ///
  /// We store a vector of the slices formed by uses of the alloca here. This
  /// vector is sorted by increasing begin offset, and then the unsplittable
  /// slices before the splittable ones. See the Slice inner class for more
  /// details.
  SmallVector<Slice, 8> Slices;

  /// Instructions which will become dead if we rewrite the alloca.
  ///
  /// Note that these are not separated by slice. This is because we expect an
  /// alloca to be completely rewritten or not rewritten at all. If rewritten,
  /// all these instructions can simply be removed and replaced with poison as
  /// they come from outside of the allocated space.
  SmallVector<Instruction *, 8> DeadUsers;

  /// Uses which will become dead if can promote the alloca.
  SmallVector<Use *, 8> DeadUseIfPromotable;

  /// Operands which will become dead if we rewrite the alloca.
  ///
  /// These are operands that in their particular use can be replaced with
  /// poison when we rewrite the alloca. These show up in out-of-bounds inputs
  /// to PHI nodes and the like. They aren't entirely dead (there might be
  /// a GEP back into the bounds using it elsewhere) and nor is the PHI, but we
  /// want to swap this particular input for poison to simplify the use lists of
  /// the alloca.
  SmallVector<Use *, 8> DeadOperands;
};

/// A partition of the slices.
///
/// An ephemeral representation for a range of slices which can be viewed as
/// a partition of the alloca. This range represents a span of the alloca's
/// memory which cannot be split, and provides access to all of the slices
/// overlapping some part of the partition.
///
/// Objects of this type are produced by traversing the alloca's slices, but
/// are only ephemeral and not persistent.
class Partition {
private:
  friend class AllocaSlices;
  friend class AllocaSlices::partition_iterator;

  using iterator = AllocaSlices::iterator;

  /// The beginning and ending offsets of the alloca for this
  /// partition.
  uint64_t BeginOffset = 0, EndOffset = 0;

  /// The start and end iterators of this partition.
  iterator SI, SJ;

  /// A collection of split slice tails overlapping the partition.
  SmallVector<Slice *, 4> SplitTails;

  /// Raw constructor builds an empty partition starting and ending at
  /// the given iterator.
  Partition(iterator SI) : SI(SI), SJ(SI) {}

public:
  /// The start offset of this partition.
  ///
  /// All of the contained slices start at or after this offset.
  uint64_t beginOffset() const { return BeginOffset; }

  /// The end offset of this partition.
  ///
  /// All of the contained slices end at or before this offset.
  uint64_t endOffset() const { return EndOffset; }

  /// The size of the partition.
  ///
  /// Note that this can never be zero.
  uint64_t size() const {
    assert(BeginOffset < EndOffset && "Partitions must span some bytes!");
    return EndOffset - BeginOffset;
  }

  /// Test whether this partition contains no slices, and merely spans
  /// a region occupied by split slices.
  bool empty() const { return SI == SJ; }

  /// \name Iterate slices that start within the partition.
  /// These may be splittable or unsplittable. They have a begin offset >= the
  /// partition begin offset.
  /// @{
  // FIXME: We should probably define a "concat_iterator" helper and use that
  // to stitch together pointee_iterators over the split tails and the
  // contiguous iterators of the partition. That would give a much nicer
  // interface here. We could then additionally expose filtered iterators for
  // split, unsplit, and unsplittable splices based on the usage patterns.
  iterator begin() const { return SI; }
  iterator end() const { return SJ; }
  /// @}

  /// Get the sequence of split slice tails.
  ///
  /// These tails are of slices which start before this partition but are
  /// split and overlap into the partition. We accumulate these while forming
  /// partitions.
  ArrayRef<Slice *> splitSliceTails() const { return SplitTails; }
};

} // end anonymous namespace

/// An iterator over partitions of the alloca's slices.
///
/// This iterator implements the core algorithm for partitioning the alloca's
/// slices. It is a forward iterator as we don't support backtracking for
/// efficiency reasons, and re-use a single storage area to maintain the
/// current set of split slices.
///
/// It is templated on the slice iterator type to use so that it can operate
/// with either const or non-const slice iterators.
class AllocaSlices::partition_iterator
    : public iterator_facade_base<partition_iterator, std::forward_iterator_tag,
                                  Partition> {
  friend class AllocaSlices;

  /// Most of the state for walking the partitions is held in a class
  /// with a nice interface for examining them.
  Partition P;

  /// We need to keep the end of the slices to know when to stop.
  AllocaSlices::iterator SE;

  /// We also need to keep track of the maximum split end offset seen.
  /// FIXME: Do we really?
  uint64_t MaxSplitSliceEndOffset = 0;

  /// Sets the partition to be empty at given iterator, and sets the
  /// end iterator.
  partition_iterator(AllocaSlices::iterator SI, AllocaSlices::iterator SE)
      : P(SI), SE(SE) {
    // If not already at the end, advance our state to form the initial
    // partition.
    if (SI != SE)
      advance();
  }

  /// Advance the iterator to the next partition.
  ///
  /// Requires that the iterator not be at the end of the slices.
  void advance() {
    assert((P.SI != SE || !P.SplitTails.empty()) &&
           "Cannot advance past the end of the slices!");

    // Clear out any split uses which have ended.
    if (!P.SplitTails.empty()) {
      if (P.EndOffset >= MaxSplitSliceEndOffset) {
        // If we've finished all splits, this is easy.
        P.SplitTails.clear();
        MaxSplitSliceEndOffset = 0;
      } else {
        // Remove the uses which have ended in the prior partition. This
        // cannot change the max split slice end because we just checked that
        // the prior partition ended prior to that max.
        llvm::erase_if(P.SplitTails,
                       [&](Slice *S) { return S->endOffset() <= P.EndOffset; });
        assert(llvm::any_of(P.SplitTails,
                            [&](Slice *S) {
                              return S->endOffset() == MaxSplitSliceEndOffset;
                            }) &&
               "Could not find the current max split slice offset!");
        assert(llvm::all_of(P.SplitTails,
                            [&](Slice *S) {
                              return S->endOffset() <= MaxSplitSliceEndOffset;
                            }) &&
               "Max split slice end offset is not actually the max!");
      }
    }

    // If P.SI is already at the end, then we've cleared the split tail and
    // now have an end iterator.
    if (P.SI == SE) {
      assert(P.SplitTails.empty() && "Failed to clear the split slices!");
      return;
    }

    // If we had a non-empty partition previously, set up the state for
    // subsequent partitions.
    if (P.SI != P.SJ) {
      // Accumulate all the splittable slices which started in the old
      // partition into the split list.
      for (Slice &S : P)
        if (S.isSplittable() && S.endOffset() > P.EndOffset) {
          P.SplitTails.push_back(&S);
          MaxSplitSliceEndOffset =
              std::max(S.endOffset(), MaxSplitSliceEndOffset);
        }

      // Start from the end of the previous partition.
      P.SI = P.SJ;

      // If P.SI is now at the end, we at most have a tail of split slices.
      if (P.SI == SE) {
        P.BeginOffset = P.EndOffset;
        P.EndOffset = MaxSplitSliceEndOffset;
        return;
      }

      // If the we have split slices and the next slice is after a gap and is
      // not splittable immediately form an empty partition for the split
      // slices up until the next slice begins.
      if (!P.SplitTails.empty() && P.SI->beginOffset() != P.EndOffset &&
          !P.SI->isSplittable()) {
        P.BeginOffset = P.EndOffset;
        P.EndOffset = P.SI->beginOffset();
        return;
      }
    }

    // OK, we need to consume new slices. Set the end offset based on the
    // current slice, and step SJ past it. The beginning offset of the
    // partition is the beginning offset of the next slice unless we have
    // pre-existing split slices that are continuing, in which case we begin
    // at the prior end offset.
    P.BeginOffset = P.SplitTails.empty() ? P.SI->beginOffset() : P.EndOffset;
    P.EndOffset = P.SI->endOffset();
    ++P.SJ;

    // There are two strategies to form a partition based on whether the
    // partition starts with an unsplittable slice or a splittable slice.
    if (!P.SI->isSplittable()) {
      // When we're forming an unsplittable region, it must always start at
      // the first slice and will extend through its end.
      assert(P.BeginOffset == P.SI->beginOffset());

      // Form a partition including all of the overlapping slices with this
      // unsplittable slice.
      while (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset) {
        if (!P.SJ->isSplittable())
          P.EndOffset = std::max(P.EndOffset, P.SJ->endOffset());
        ++P.SJ;
      }

      // We have a partition across a set of overlapping unsplittable
      // partitions.
      return;
    }

    // If we're starting with a splittable slice, then we need to form
    // a synthetic partition spanning it and any other overlapping splittable
    // splices.
    assert(P.SI->isSplittable() && "Forming a splittable partition!");

    // Collect all of the overlapping splittable slices.
    while (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset &&
           P.SJ->isSplittable()) {
      P.EndOffset = std::max(P.EndOffset, P.SJ->endOffset());
      ++P.SJ;
    }

    // Back upiP.EndOffset if we ended the span early when encountering an
    // unsplittable slice. This synthesizes the early end offset of
    // a partition spanning only splittable slices.
    if (P.SJ != SE && P.SJ->beginOffset() < P.EndOffset) {
      assert(!P.SJ->isSplittable());
      P.EndOffset = P.SJ->beginOffset();
    }
  }

public:
  bool operator==(const partition_iterator &RHS) const {
    assert(SE == RHS.SE &&
           "End iterators don't match between compared partition iterators!");

    // The observed positions of partitions is marked by the P.SI iterator and
    // the emptiness of the split slices. The latter is only relevant when
    // P.SI == SE, as the end iterator will additionally have an empty split
    // slices list, but the prior may have the same P.SI and a tail of split
    // slices.
    if (P.SI == RHS.P.SI && P.SplitTails.empty() == RHS.P.SplitTails.empty()) {
      assert(P.SJ == RHS.P.SJ &&
             "Same set of slices formed two different sized partitions!");
      assert(P.SplitTails.size() == RHS.P.SplitTails.size() &&
             "Same slice position with differently sized non-empty split "
             "slice tails!");
      return true;
    }
    return false;
  }

  partition_iterator &operator++() {
    advance();
    return *this;
  }

  Partition &operator*() { return P; }
};

/// A forward range over the partitions of the alloca's slices.
///
/// This accesses an iterator range over the partitions of the alloca's
/// slices. It computes these partitions on the fly based on the overlapping
/// offsets of the slices and the ability to split them. It will visit "empty"
/// partitions to cover regions of the alloca only accessed via split
/// slices.
iterator_range<AllocaSlices::partition_iterator> AllocaSlices::partitions() {
  return make_range(partition_iterator(begin(), end()),
                    partition_iterator(end(), end()));
}

static Value *foldSelectInst(SelectInst &SI) {
  // If the condition being selected on is a constant or the same value is
  // being selected between, fold the select. Yes this does (rarely) happen
  // early on.
  if (ConstantInt *CI = dyn_cast<ConstantInt>(SI.getCondition()))
    return SI.getOperand(1 + CI->isZero());
  if (SI.getOperand(1) == SI.getOperand(2))
    return SI.getOperand(1);

  return nullptr;
}

/// A helper that folds a PHI node or a select.
static Value *foldPHINodeOrSelectInst(Instruction &I) {
  if (PHINode *PN = dyn_cast<PHINode>(&I)) {
    // If PN merges together the same value, return that value.
    return PN->hasConstantValue();
  }
  return foldSelectInst(cast<SelectInst>(I));
}

/// Builder for the alloca slices.
///
/// This class builds a set of alloca slices by recursively visiting the uses
/// of an alloca and making a slice for each load and store at each offset.
class AllocaSlices::SliceBuilder : public PtrUseVisitor<SliceBuilder> {
  friend class PtrUseVisitor<SliceBuilder>;
  friend class InstVisitor<SliceBuilder>;

  using Base = PtrUseVisitor<SliceBuilder>;

  const uint64_t AllocSize;
  AllocaSlices &AS;

  SmallDenseMap<Instruction *, unsigned> MemTransferSliceMap;
  SmallDenseMap<Instruction *, uint64_t> PHIOrSelectSizes;

  /// Set to de-duplicate dead instructions found in the use walk.
  SmallPtrSet<Instruction *, 4> VisitedDeadInsts;

public:
  SliceBuilder(const DataLayout &DL, AllocaInst &AI, AllocaSlices &AS)
      : PtrUseVisitor<SliceBuilder>(DL),
        AllocSize(DL.getTypeAllocSize(AI.getAllocatedType()).getFixedValue()),
        AS(AS) {}

private:
  void markAsDead(Instruction &I) {
    if (VisitedDeadInsts.insert(&I).second)
      AS.DeadUsers.push_back(&I);
  }

  void insertUse(Instruction &I, const APInt &Offset, uint64_t Size,
                 bool IsSplittable = false) {
    // Completely skip uses which have a zero size or start either before or
    // past the end of the allocation.
    if (Size == 0 || Offset.uge(AllocSize)) {
      LLVM_DEBUG(dbgs() << "WARNING: Ignoring " << Size << " byte use @"
                        << Offset
                        << " which has zero size or starts outside of the "
                        << AllocSize << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << I << "\n");
      return markAsDead(I);
    }

    uint64_t BeginOffset = Offset.getZExtValue();
    uint64_t EndOffset = BeginOffset + Size;

    // Clamp the end offset to the end of the allocation. Note that this is
    // formulated to handle even the case where "BeginOffset + Size" overflows.
    // This may appear superficially to be something we could ignore entirely,
    // but that is not so! There may be widened loads or PHI-node uses where
    // some instructions are dead but not others. We can't completely ignore
    // them, and so have to record at least the information here.
    assert(AllocSize >= BeginOffset); // Established above.
    if (Size > AllocSize - BeginOffset) {
      LLVM_DEBUG(dbgs() << "WARNING: Clamping a " << Size << " byte use @"
                        << Offset << " to remain within the " << AllocSize
                        << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << I << "\n");
      EndOffset = AllocSize;
    }

    AS.Slices.push_back(Slice(BeginOffset, EndOffset, U, IsSplittable));
  }

  void visitBitCastInst(BitCastInst &BC) {
    if (BC.use_empty())
      return markAsDead(BC);

    return Base::visitBitCastInst(BC);
  }

  void visitAddrSpaceCastInst(AddrSpaceCastInst &ASC) {
    if (ASC.use_empty())
      return markAsDead(ASC);

    return Base::visitAddrSpaceCastInst(ASC);
  }

  void visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    if (GEPI.use_empty())
      return markAsDead(GEPI);

    return Base::visitGetElementPtrInst(GEPI);
  }

  void handleLoadOrStore(Type *Ty, Instruction &I, const APInt &Offset,
                         uint64_t Size, bool IsVolatile) {
    // We allow splitting of non-volatile loads and stores where the type is an
    // integer type. These may be used to implement 'memcpy' or other "transfer
    // of bits" patterns.
    bool IsSplittable =
        Ty->isIntegerTy() && !IsVolatile && DL.typeSizeEqualsStoreSize(Ty);

    insertUse(I, Offset, Size, IsSplittable);
  }

  void visitLoadInst(LoadInst &LI) {
    assert((!LI.isSimple() || LI.getType()->isSingleValueType()) &&
           "All simple FCA loads should have been pre-split");

    if (!IsOffsetKnown)
      return PI.setAborted(&LI);

    TypeSize Size = DL.getTypeStoreSize(LI.getType());
    if (Size.isScalable())
      return PI.setAborted(&LI);

    return handleLoadOrStore(LI.getType(), LI, Offset, Size.getFixedValue(),
                             LI.isVolatile());
  }

  void visitStoreInst(StoreInst &SI) {
    Value *ValOp = SI.getValueOperand();
    if (ValOp == *U)
      return PI.setEscapedAndAborted(&SI);
    if (!IsOffsetKnown)
      return PI.setAborted(&SI);

    TypeSize StoreSize = DL.getTypeStoreSize(ValOp->getType());
    if (StoreSize.isScalable())
      return PI.setAborted(&SI);

    uint64_t Size = StoreSize.getFixedValue();

    // If this memory access can be shown to *statically* extend outside the
    // bounds of the allocation, it's behavior is undefined, so simply
    // ignore it. Note that this is more strict than the generic clamping
    // behavior of insertUse. We also try to handle cases which might run the
    // risk of overflow.
    // FIXME: We should instead consider the pointer to have escaped if this
    // function is being instrumented for addressing bugs or race conditions.
    if (Size > AllocSize || Offset.ugt(AllocSize - Size)) {
      LLVM_DEBUG(dbgs() << "WARNING: Ignoring " << Size << " byte store @"
                        << Offset << " which extends past the end of the "
                        << AllocSize << " byte alloca:\n"
                        << "    alloca: " << AS.AI << "\n"
                        << "       use: " << SI << "\n");
      return markAsDead(SI);
    }

    assert((!SI.isSimple() || ValOp->getType()->isSingleValueType()) &&
           "All simple FCA stores should have been pre-split");
    handleLoadOrStore(ValOp->getType(), SI, Offset, Size, SI.isVolatile());
  }

  void visitMemSetInst(MemSetInst &II) {
    assert(II.getRawDest() == *U && "Pointer use is not the destination?");
    ConstantInt *Length = dyn_cast<ConstantInt>(II.getLength());
    if ((Length && Length->getValue() == 0) ||
        (IsOffsetKnown && Offset.uge(AllocSize)))
      // Zero-length mem transfer intrinsics can be ignored entirely.
      return markAsDead(II);

    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    insertUse(II, Offset,
              Length ? Length->getLimitedValue()
                     : AllocSize - Offset.getLimitedValue(),
              (bool)Length);
  }

  void visitMemTransferInst(MemTransferInst &II) {
    ConstantInt *Length = dyn_cast<ConstantInt>(II.getLength());
    if (Length && Length->getValue() == 0)
      // Zero-length mem transfer intrinsics can be ignored entirely.
      return markAsDead(II);

    // Because we can visit these intrinsics twice, also check to see if the
    // first time marked this instruction as dead. If so, skip it.
    if (VisitedDeadInsts.count(&II))
      return;

    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    // This side of the transfer is completely out-of-bounds, and so we can
    // nuke the entire transfer. However, we also need to nuke the other side
    // if already added to our partitions.
    // FIXME: Yet another place we really should bypass this when
    // instrumenting for ASan.
    if (Offset.uge(AllocSize)) {
      SmallDenseMap<Instruction *, unsigned>::iterator MTPI =
          MemTransferSliceMap.find(&II);
      if (MTPI != MemTransferSliceMap.end())
        AS.Slices[MTPI->second].kill();
      return markAsDead(II);
    }

    uint64_t RawOffset = Offset.getLimitedValue();
    uint64_t Size = Length ? Length->getLimitedValue() : AllocSize - RawOffset;

    // Check for the special case where the same exact value is used for both
    // source and dest.
    if (*U == II.getRawDest() && *U == II.getRawSource()) {
      // For non-volatile transfers this is a no-op.
      if (!II.isVolatile())
        return markAsDead(II);

      return insertUse(II, Offset, Size, /*IsSplittable=*/false);
    }

    // If we have seen both source and destination for a mem transfer, then
    // they both point to the same alloca.
    bool Inserted;
    SmallDenseMap<Instruction *, unsigned>::iterator MTPI;
    std::tie(MTPI, Inserted) =
        MemTransferSliceMap.insert(std::make_pair(&II, AS.Slices.size()));
    unsigned PrevIdx = MTPI->second;
    if (!Inserted) {
      Slice &PrevP = AS.Slices[PrevIdx];

      // Check if the begin offsets match and this is a non-volatile transfer.
      // In that case, we can completely elide the transfer.
      if (!II.isVolatile() && PrevP.beginOffset() == RawOffset) {
        PrevP.kill();
        return markAsDead(II);
      }

      // Otherwise we have an offset transfer within the same alloca. We can't
      // split those.
      PrevP.makeUnsplittable();
    }

    // Insert the use now that we've fixed up the splittable nature.
    insertUse(II, Offset, Size, /*IsSplittable=*/Inserted && Length);

    // Check that we ended up with a valid index in the map.
    assert(AS.Slices[PrevIdx].getUse()->getUser() == &II &&
           "Map index doesn't point back to a slice with this user.");
  }

  // Disable SRoA for any intrinsics except for lifetime invariants and
  // invariant group.
  // FIXME: What about debug intrinsics? This matches old behavior, but
  // doesn't make sense.
  void visitIntrinsicInst(IntrinsicInst &II) {
    if (II.isDroppable()) {
      AS.DeadUseIfPromotable.push_back(U);
      return;
    }

    if (!IsOffsetKnown)
      return PI.setAborted(&II);

    if (II.isLifetimeStartOrEnd()) {
      ConstantInt *Length = cast<ConstantInt>(II.getArgOperand(0));
      uint64_t Size = std::min(AllocSize - Offset.getLimitedValue(),
                               Length->getLimitedValue());
      insertUse(II, Offset, Size, true);
      return;
    }

    if (II.isLaunderOrStripInvariantGroup()) {
      insertUse(II, Offset, AllocSize, true);
      enqueueUsers(II);
      return;
    }

    Base::visitIntrinsicInst(II);
  }

  Instruction *hasUnsafePHIOrSelectUse(Instruction *Root, uint64_t &Size) {
    // We consider any PHI or select that results in a direct load or store of
    // the same offset to be a viable use for slicing purposes. These uses
    // are considered unsplittable and the size is the maximum loaded or stored
    // size.
    SmallPtrSet<Instruction *, 4> Visited;
    SmallVector<std::pair<Instruction *, Instruction *>, 4> Uses;
    Visited.insert(Root);
    Uses.push_back(std::make_pair(cast<Instruction>(*U), Root));
    const DataLayout &DL = Root->getDataLayout();
    // If there are no loads or stores, the access is dead. We mark that as
    // a size zero access.
    Size = 0;
    do {
      Instruction *I, *UsedI;
      std::tie(UsedI, I) = Uses.pop_back_val();

      if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        TypeSize LoadSize = DL.getTypeStoreSize(LI->getType());
        if (LoadSize.isScalable()) {
          PI.setAborted(LI);
          return nullptr;
        }
        Size = std::max(Size, LoadSize.getFixedValue());
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        Value *Op = SI->getOperand(0);
        if (Op == UsedI)
          return SI;
        TypeSize StoreSize = DL.getTypeStoreSize(Op->getType());
        if (StoreSize.isScalable()) {
          PI.setAborted(SI);
          return nullptr;
        }
        Size = std::max(Size, StoreSize.getFixedValue());
        continue;
      }

      if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I)) {
        if (!GEP->hasAllZeroIndices())
          return GEP;
      } else if (!isa<BitCastInst>(I) && !isa<PHINode>(I) &&
                 !isa<SelectInst>(I) && !isa<AddrSpaceCastInst>(I)) {
        return I;
      }

      for (User *U : I->users())
        if (Visited.insert(cast<Instruction>(U)).second)
          Uses.push_back(std::make_pair(I, cast<Instruction>(U)));
    } while (!Uses.empty());

    return nullptr;
  }

  void visitPHINodeOrSelectInst(Instruction &I) {
    assert(isa<PHINode>(I) || isa<SelectInst>(I));
    if (I.use_empty())
      return markAsDead(I);

    // If this is a PHI node before a catchswitch, we cannot insert any non-PHI
    // instructions in this BB, which may be required during rewriting. Bail out
    // on these cases.
    if (isa<PHINode>(I) &&
        I.getParent()->getFirstInsertionPt() == I.getParent()->end())
      return PI.setAborted(&I);

    // TODO: We could use simplifyInstruction here to fold PHINodes and
    // SelectInsts. However, doing so requires to change the current
    // dead-operand-tracking mechanism. For instance, suppose neither loading
    // from %U nor %other traps. Then "load (select undef, %U, %other)" does not
    // trap either.  However, if we simply replace %U with undef using the
    // current dead-operand-tracking mechanism, "load (select undef, undef,
    // %other)" may trap because the select may return the first operand
    // "undef".
    if (Value *Result = foldPHINodeOrSelectInst(I)) {
      if (Result == *U)
        // If the result of the constant fold will be the pointer, recurse
        // through the PHI/select as if we had RAUW'ed it.
        enqueueUsers(I);
      else
        // Otherwise the operand to the PHI/select is dead, and we can replace
        // it with poison.
        AS.DeadOperands.push_back(U);

      return;
    }

    if (!IsOffsetKnown)
      return PI.setAborted(&I);

    // See if we already have computed info on this node.
    uint64_t &Size = PHIOrSelectSizes[&I];
    if (!Size) {
      // This is a new PHI/Select, check for an unsafe use of it.
      if (Instruction *UnsafeI = hasUnsafePHIOrSelectUse(&I, Size))
        return PI.setAborted(UnsafeI);
    }

    // For PHI and select operands outside the alloca, we can't nuke the entire
    // phi or select -- the other side might still be relevant, so we special
    // case them here and use a separate structure to track the operands
    // themselves which should be replaced with poison.
    // FIXME: This should instead be escaped in the event we're instrumenting
    // for address sanitization.
    if (Offset.uge(AllocSize)) {
      AS.DeadOperands.push_back(U);
      return;
    }

    insertUse(I, Offset, Size);
  }

  void visitPHINode(PHINode &PN) { visitPHINodeOrSelectInst(PN); }

  void visitSelectInst(SelectInst &SI) { visitPHINodeOrSelectInst(SI); }

  /// Disable SROA entirely if there are unhandled users of the alloca.
  void visitInstruction(Instruction &I) { PI.setAborted(&I); }
};

AllocaSlices::AllocaSlices(const DataLayout &DL, AllocaInst &AI)
    :
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
      AI(AI),
#endif
      PointerEscapingInstr(nullptr) {
  SliceBuilder PB(DL, AI, *this);
  SliceBuilder::PtrInfo PtrI = PB.visitPtr(AI);
  if (PtrI.isEscaped() || PtrI.isAborted()) {
    // FIXME: We should sink the escape vs. abort info into the caller nicely,
    // possibly by just storing the PtrInfo in the AllocaSlices.
    PointerEscapingInstr = PtrI.getEscapingInst() ? PtrI.getEscapingInst()
                                                  : PtrI.getAbortingInst();
    assert(PointerEscapingInstr && "Did not track a bad instruction");
    return;
  }

  llvm::erase_if(Slices, [](const Slice &S) { return S.isDead(); });

  // Sort the uses. This arranges for the offsets to be in ascending order,
  // and the sizes to be in descending order.
  llvm::stable_sort(Slices);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

void AllocaSlices::print(raw_ostream &OS, const_iterator I,
                         StringRef Indent) const {
  printSlice(OS, I, Indent);
  OS << "\n";
  printUse(OS, I, Indent);
}

void AllocaSlices::printSlice(raw_ostream &OS, const_iterator I,
                              StringRef Indent) const {
  OS << Indent << "[" << I->beginOffset() << "," << I->endOffset() << ")"
     << " slice #" << (I - begin())
     << (I->isSplittable() ? " (splittable)" : "");
}

void AllocaSlices::printUse(raw_ostream &OS, const_iterator I,
                            StringRef Indent) const {
  OS << Indent << "  used by: " << *I->getUse()->getUser() << "\n";
}

void AllocaSlices::print(raw_ostream &OS) const {
  if (PointerEscapingInstr) {
    OS << "Can't analyze slices for alloca: " << AI << "\n"
       << "  A pointer to this alloca escaped by:\n"
       << "  " << *PointerEscapingInstr << "\n";
    return;
  }

  OS << "Slices of alloca: " << AI << "\n";
  for (const_iterator I = begin(), E = end(); I != E; ++I)
    print(OS, I);
}

LLVM_DUMP_METHOD void AllocaSlices::dump(const_iterator I) const {
  print(dbgs(), I);
}
LLVM_DUMP_METHOD void AllocaSlices::dump() const { print(dbgs()); }

#endif // !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)

/// Walk the range of a partitioning looking for a common type to cover this
/// sequence of slices.
static std::pair<Type *, IntegerType *>
findCommonType(AllocaSlices::const_iterator B, AllocaSlices::const_iterator E,
               uint64_t EndOffset) {
  Type *Ty = nullptr;
  bool TyIsCommon = true;
  IntegerType *ITy = nullptr;

  // Note that we need to look at *every* alloca slice's Use to ensure we
  // always get consistent results regardless of the order of slices.
  for (AllocaSlices::const_iterator I = B; I != E; ++I) {
    Use *U = I->getUse();
    if (isa<IntrinsicInst>(*U->getUser()))
      continue;
    if (I->beginOffset() != B->beginOffset() || I->endOffset() != EndOffset)
      continue;

    Type *UserTy = nullptr;
    if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
      UserTy = LI->getType();
    } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
      UserTy = SI->getValueOperand()->getType();
    }

    if (IntegerType *UserITy = dyn_cast_or_null<IntegerType>(UserTy)) {
      // If the type is larger than the partition, skip it. We only encounter
      // this for split integer operations where we want to use the type of the
      // entity causing the split. Also skip if the type is not a byte width
      // multiple.
      if (UserITy->getBitWidth() % 8 != 0 ||
          UserITy->getBitWidth() / 8 > (EndOffset - B->beginOffset()))
        continue;

      // Track the largest bitwidth integer type used in this way in case there
      // is no common type.
      if (!ITy || ITy->getBitWidth() < UserITy->getBitWidth())
        ITy = UserITy;
    }

    // To avoid depending on the order of slices, Ty and TyIsCommon must not
    // depend on types skipped above.
    if (!UserTy || (Ty && Ty != UserTy))
      TyIsCommon = false; // Give up on anything but an iN type.
    else
      Ty = UserTy;
  }

  return {TyIsCommon ? Ty : nullptr, ITy};
}

/// PHI instructions that use an alloca and are subsequently loaded can be
/// rewritten to load both input pointers in the pred blocks and then PHI the
/// results, allowing the load of the alloca to be promoted.
/// From this:
///   %P2 = phi [i32* %Alloca, i32* %Other]
///   %V = load i32* %P2
/// to:
///   %V1 = load i32* %Alloca      -> will be mem2reg'd
///   ...
///   %V2 = load i32* %Other
///   ...
///   %V = phi [i32 %V1, i32 %V2]
///
/// We can do this to a select if its only uses are loads and if the operands
/// to the select can be loaded unconditionally.
///
/// FIXME: This should be hoisted into a generic utility, likely in
/// Transforms/Util/Local.h
static bool isSafePHIToSpeculate(PHINode &PN) {
  const DataLayout &DL = PN.getDataLayout();

  // For now, we can only do this promotion if the load is in the same block
  // as the PHI, and if there are no stores between the phi and load.
  // TODO: Allow recursive phi users.
  // TODO: Allow stores.
  BasicBlock *BB = PN.getParent();
  Align MaxAlign;
  uint64_t APWidth = DL.getIndexTypeSizeInBits(PN.getType());
  Type *LoadType = nullptr;
  for (User *U : PN.users()) {
    LoadInst *LI = dyn_cast<LoadInst>(U);
    if (!LI || !LI->isSimple())
      return false;

    // For now we only allow loads in the same block as the PHI.  This is
    // a common case that happens when instcombine merges two loads through
    // a PHI.
    if (LI->getParent() != BB)
      return false;

    if (LoadType) {
      if (LoadType != LI->getType())
        return false;
    } else {
      LoadType = LI->getType();
    }

    // Ensure that there are no instructions between the PHI and the load that
    // could store.
    for (BasicBlock::iterator BBI(PN); &*BBI != LI; ++BBI)
      if (BBI->mayWriteToMemory())
        return false;

    MaxAlign = std::max(MaxAlign, LI->getAlign());
  }

  if (!LoadType)
    return false;

  APInt LoadSize =
      APInt(APWidth, DL.getTypeStoreSize(LoadType).getFixedValue());

  // We can only transform this if it is safe to push the loads into the
  // predecessor blocks. The only thing to watch out for is that we can't put
  // a possibly trapping load in the predecessor if it is a critical edge.
  for (unsigned Idx = 0, Num = PN.getNumIncomingValues(); Idx != Num; ++Idx) {
    Instruction *TI = PN.getIncomingBlock(Idx)->getTerminator();
    Value *InVal = PN.getIncomingValue(Idx);

    // If the value is produced by the terminator of the predecessor (an
    // invoke) or it has side-effects, there is no valid place to put a load
    // in the predecessor.
    if (TI == InVal || TI->mayHaveSideEffects())
      return false;

    // If the predecessor has a single successor, then the edge isn't
    // critical.
    if (TI->getNumSuccessors() == 1)
      continue;

    // If this pointer is always safe to load, or if we can prove that there
    // is already a load in the block, then we can move the load to the pred
    // block.
    if (isSafeToLoadUnconditionally(InVal, MaxAlign, LoadSize, DL, TI))
      continue;

    return false;
  }

  return true;
}

static void speculatePHINodeLoads(IRBuilderTy &IRB, PHINode &PN) {
  LLVM_DEBUG(dbgs() << "    original: " << PN << "\n");

  LoadInst *SomeLoad = cast<LoadInst>(PN.user_back());
  Type *LoadTy = SomeLoad->getType();
  IRB.SetInsertPoint(&PN);
  PHINode *NewPN = IRB.CreatePHI(LoadTy, PN.getNumIncomingValues(),
                                 PN.getName() + ".sroa.speculated");

  // Get the AA tags and alignment to use from one of the loads. It does not
  // matter which one we get and if any differ.
  AAMDNodes AATags = SomeLoad->getAAMetadata();
  Align Alignment = SomeLoad->getAlign();

  // Rewrite all loads of the PN to use the new PHI.
  while (!PN.use_empty()) {
    LoadInst *LI = cast<LoadInst>(PN.user_back());
    LI->replaceAllUsesWith(NewPN);
    LI->eraseFromParent();
  }

  // Inject loads into all of the pred blocks.
  DenseMap<BasicBlock *, Value *> InjectedLoads;
  for (unsigned Idx = 0, Num = PN.getNumIncomingValues(); Idx != Num; ++Idx) {
    BasicBlock *Pred = PN.getIncomingBlock(Idx);
    Value *InVal = PN.getIncomingValue(Idx);

    // A PHI node is allowed to have multiple (duplicated) entries for the same
    // basic block, as long as the value is the same. So if we already injected
    // a load in the predecessor, then we should reuse the same load for all
    // duplicated entries.
    if (Value *V = InjectedLoads.lookup(Pred)) {
      NewPN->addIncoming(V, Pred);
      continue;
    }

    Instruction *TI = Pred->getTerminator();
    IRB.SetInsertPoint(TI);

    LoadInst *Load = IRB.CreateAlignedLoad(
        LoadTy, InVal, Alignment,
        (PN.getName() + ".sroa.speculate.load." + Pred->getName()));
    ++NumLoadsSpeculated;
    if (AATags)
      Load->setAAMetadata(AATags);
    NewPN->addIncoming(Load, Pred);
    InjectedLoads[Pred] = Load;
  }

  LLVM_DEBUG(dbgs() << "          speculated to: " << *NewPN << "\n");
  PN.eraseFromParent();
}

SelectHandSpeculativity &
SelectHandSpeculativity::setAsSpeculatable(bool isTrueVal) {
  if (isTrueVal)
    Bitfield::set<SelectHandSpeculativity::TrueVal>(Storage, true);
  else
    Bitfield::set<SelectHandSpeculativity::FalseVal>(Storage, true);
  return *this;
}

bool SelectHandSpeculativity::isSpeculatable(bool isTrueVal) const {
  return isTrueVal ? Bitfield::get<SelectHandSpeculativity::TrueVal>(Storage)
                   : Bitfield::get<SelectHandSpeculativity::FalseVal>(Storage);
}

bool SelectHandSpeculativity::areAllSpeculatable() const {
  return isSpeculatable(/*isTrueVal=*/true) &&
         isSpeculatable(/*isTrueVal=*/false);
}

bool SelectHandSpeculativity::areAnySpeculatable() const {
  return isSpeculatable(/*isTrueVal=*/true) ||
         isSpeculatable(/*isTrueVal=*/false);
}
bool SelectHandSpeculativity::areNoneSpeculatable() const {
  return !areAnySpeculatable();
}

static SelectHandSpeculativity
isSafeLoadOfSelectToSpeculate(LoadInst &LI, SelectInst &SI, bool PreserveCFG) {
  assert(LI.isSimple() && "Only for simple loads");
  SelectHandSpeculativity Spec;

  const DataLayout &DL = SI.getDataLayout();
  for (Value *Value : {SI.getTrueValue(), SI.getFalseValue()})
    if (isSafeToLoadUnconditionally(Value, LI.getType(), LI.getAlign(), DL,
                                    &LI))
      Spec.setAsSpeculatable(/*isTrueVal=*/Value == SI.getTrueValue());
    else if (PreserveCFG)
      return Spec;

  return Spec;
}

std::optional<RewriteableMemOps>
SROA::isSafeSelectToSpeculate(SelectInst &SI, bool PreserveCFG) {
  RewriteableMemOps Ops;

  for (User *U : SI.users()) {
    if (auto *BC = dyn_cast<BitCastInst>(U); BC && BC->hasOneUse())
      U = *BC->user_begin();

    if (auto *Store = dyn_cast<StoreInst>(U)) {
      // Note that atomic stores can be transformed; atomic semantics do not
      // have any meaning for a local alloca. Stores are not speculatable,
      // however, so if we can't turn it into a predicated store, we are done.
      if (Store->isVolatile() || PreserveCFG)
        return {}; // Give up on this `select`.
      Ops.emplace_back(Store);
      continue;
    }

    auto *LI = dyn_cast<LoadInst>(U);

    // Note that atomic loads can be transformed;
    // atomic semantics do not have any meaning for a local alloca.
    if (!LI || LI->isVolatile())
      return {}; // Give up on this `select`.

    PossiblySpeculatableLoad Load(LI);
    if (!LI->isSimple()) {
      // If the `load` is not simple, we can't speculatively execute it,
      // but we could handle this via a CFG modification. But can we?
      if (PreserveCFG)
        return {}; // Give up on this `select`.
      Ops.emplace_back(Load);
      continue;
    }

    SelectHandSpeculativity Spec =
        isSafeLoadOfSelectToSpeculate(*LI, SI, PreserveCFG);
    if (PreserveCFG && !Spec.areAllSpeculatable())
      return {}; // Give up on this `select`.

    Load.setInt(Spec);
    Ops.emplace_back(Load);
  }

  return Ops;
}

static void speculateSelectInstLoads(SelectInst &SI, LoadInst &LI,
                                     IRBuilderTy &IRB) {
  LLVM_DEBUG(dbgs() << "    original load: " << SI << "\n");

  Value *TV = SI.getTrueValue();
  Value *FV = SI.getFalseValue();
  // Replace the given load of the select with a select of two loads.

  assert(LI.isSimple() && "We only speculate simple loads");

  IRB.SetInsertPoint(&LI);

  LoadInst *TL =
      IRB.CreateAlignedLoad(LI.getType(), TV, LI.getAlign(),
                            LI.getName() + ".sroa.speculate.load.true");
  LoadInst *FL =
      IRB.CreateAlignedLoad(LI.getType(), FV, LI.getAlign(),
                            LI.getName() + ".sroa.speculate.load.false");
  NumLoadsSpeculated += 2;

  // Transfer alignment and AA info if present.
  TL->setAlignment(LI.getAlign());
  FL->setAlignment(LI.getAlign());

  AAMDNodes Tags = LI.getAAMetadata();
  if (Tags) {
    TL->setAAMetadata(Tags);
    FL->setAAMetadata(Tags);
  }

  Value *V = IRB.CreateSelect(SI.getCondition(), TL, FL,
                              LI.getName() + ".sroa.speculated");

  LLVM_DEBUG(dbgs() << "          speculated to: " << *V << "\n");
  LI.replaceAllUsesWith(V);
}

template <typename T>
static void rewriteMemOpOfSelect(SelectInst &SI, T &I,
                                 SelectHandSpeculativity Spec,
                                 DomTreeUpdater &DTU) {
  assert((isa<LoadInst>(I) || isa<StoreInst>(I)) && "Only for load and store!");
  LLVM_DEBUG(dbgs() << "    original mem op: " << I << "\n");
  BasicBlock *Head = I.getParent();
  Instruction *ThenTerm = nullptr;
  Instruction *ElseTerm = nullptr;
  if (Spec.areNoneSpeculatable())
    SplitBlockAndInsertIfThenElse(SI.getCondition(), &I, &ThenTerm, &ElseTerm,
                                  SI.getMetadata(LLVMContext::MD_prof), &DTU);
  else {
    SplitBlockAndInsertIfThen(SI.getCondition(), &I, /*Unreachable=*/false,
                              SI.getMetadata(LLVMContext::MD_prof), &DTU,
                              /*LI=*/nullptr, /*ThenBlock=*/nullptr);
    if (Spec.isSpeculatable(/*isTrueVal=*/true))
      cast<BranchInst>(Head->getTerminator())->swapSuccessors();
  }
  auto *HeadBI = cast<BranchInst>(Head->getTerminator());
  Spec = {}; // Do not use `Spec` beyond this point.
  BasicBlock *Tail = I.getParent();
  Tail->setName(Head->getName() + ".cont");
  PHINode *PN;
  if (isa<LoadInst>(I))
    PN = PHINode::Create(I.getType(), 2, "", I.getIterator());
  for (BasicBlock *SuccBB : successors(Head)) {
    bool IsThen = SuccBB == HeadBI->getSuccessor(0);
    int SuccIdx = IsThen ? 0 : 1;
    auto *NewMemOpBB = SuccBB == Tail ? Head : SuccBB;
    auto &CondMemOp = cast<T>(*I.clone());
    if (NewMemOpBB != Head) {
      NewMemOpBB->setName(Head->getName() + (IsThen ? ".then" : ".else"));
      if (isa<LoadInst>(I))
        ++NumLoadsPredicated;
      else
        ++NumStoresPredicated;
    } else {
      CondMemOp.dropUBImplyingAttrsAndMetadata();
      ++NumLoadsSpeculated;
    }
    CondMemOp.insertBefore(NewMemOpBB->getTerminator());
    Value *Ptr = SI.getOperand(1 + SuccIdx);
    CondMemOp.setOperand(I.getPointerOperandIndex(), Ptr);
    if (isa<LoadInst>(I)) {
      CondMemOp.setName(I.getName() + (IsThen ? ".then" : ".else") + ".val");
      PN->addIncoming(&CondMemOp, NewMemOpBB);
    } else
      LLVM_DEBUG(dbgs() << "                 to: " << CondMemOp << "\n");
  }
  if (isa<LoadInst>(I)) {
    PN->takeName(&I);
    LLVM_DEBUG(dbgs() << "          to: " << *PN << "\n");
    I.replaceAllUsesWith(PN);
  }
}

static void rewriteMemOpOfSelect(SelectInst &SelInst, Instruction &I,
                                 SelectHandSpeculativity Spec,
                                 DomTreeUpdater &DTU) {
  if (auto *LI = dyn_cast<LoadInst>(&I))
    rewriteMemOpOfSelect(SelInst, *LI, Spec, DTU);
  else if (auto *SI = dyn_cast<StoreInst>(&I))
    rewriteMemOpOfSelect(SelInst, *SI, Spec, DTU);
  else
    llvm_unreachable_internal("Only for load and store.");
}

static bool rewriteSelectInstMemOps(SelectInst &SI,
                                    const RewriteableMemOps &Ops,
                                    IRBuilderTy &IRB, DomTreeUpdater *DTU) {
  bool CFGChanged = false;
  LLVM_DEBUG(dbgs() << "    original select: " << SI << "\n");

  for (const RewriteableMemOp &Op : Ops) {
    SelectHandSpeculativity Spec;
    Instruction *I;
    if (auto *const *US = std::get_if<UnspeculatableStore>(&Op)) {
      I = *US;
    } else {
      auto PSL = std::get<PossiblySpeculatableLoad>(Op);
      I = PSL.getPointer();
      Spec = PSL.getInt();
    }
    if (Spec.areAllSpeculatable()) {
      speculateSelectInstLoads(SI, cast<LoadInst>(*I), IRB);
    } else {
      assert(DTU && "Should not get here when not allowed to modify the CFG!");
      rewriteMemOpOfSelect(SI, *I, Spec, *DTU);
      CFGChanged = true;
    }
    I->eraseFromParent();
  }

  for (User *U : make_early_inc_range(SI.users()))
    cast<BitCastInst>(U)->eraseFromParent();
  SI.eraseFromParent();
  return CFGChanged;
}

/// Compute an adjusted pointer from Ptr by Offset bytes where the
/// resulting pointer has PointerTy.
static Value *getAdjustedPtr(IRBuilderTy &IRB, const DataLayout &DL, Value *Ptr,
                             APInt Offset, Type *PointerTy,
                             const Twine &NamePrefix) {
  if (Offset != 0)
    Ptr = IRB.CreateInBoundsPtrAdd(Ptr, IRB.getInt(Offset),
                                   NamePrefix + "sroa_idx");
  return IRB.CreatePointerBitCastOrAddrSpaceCast(Ptr, PointerTy,
                                                 NamePrefix + "sroa_cast");
}

/// Compute the adjusted alignment for a load or store from an offset.
static Align getAdjustedAlignment(Instruction *I, uint64_t Offset) {
  return commonAlignment(getLoadStoreAlignment(I), Offset);
}

/// Test whether we can convert a value from the old to the new type.
///
/// This predicate should be used to guard calls to convertValue in order to
/// ensure that we only try to convert viable values. The strategy is that we
/// will peel off single element struct and array wrappings to get to an
/// underlying value, and convert that value.
static bool canConvertValue(const DataLayout &DL, Type *OldTy, Type *NewTy) {
  if (OldTy == NewTy)
    return true;

  // For integer types, we can't handle any bit-width differences. This would
  // break both vector conversions with extension and introduce endianness
  // issues when in conjunction with loads and stores.
  if (isa<IntegerType>(OldTy) && isa<IntegerType>(NewTy)) {
    assert(cast<IntegerType>(OldTy)->getBitWidth() !=
               cast<IntegerType>(NewTy)->getBitWidth() &&
           "We can't have the same bitwidth for different int types");
    return false;
  }

  if (DL.getTypeSizeInBits(NewTy).getFixedValue() !=
      DL.getTypeSizeInBits(OldTy).getFixedValue())
    return false;
  if (!NewTy->isSingleValueType() || !OldTy->isSingleValueType())
    return false;

  // We can convert pointers to integers and vice-versa. Same for vectors
  // of pointers and integers.
  OldTy = OldTy->getScalarType();
  NewTy = NewTy->getScalarType();
  if (NewTy->isPointerTy() || OldTy->isPointerTy()) {
    if (NewTy->isPointerTy() && OldTy->isPointerTy()) {
      unsigned OldAS = OldTy->getPointerAddressSpace();
      unsigned NewAS = NewTy->getPointerAddressSpace();
      // Convert pointers if they are pointers from the same address space or
      // different integral (not non-integral) address spaces with the same
      // pointer size.
      return OldAS == NewAS ||
             (!DL.isNonIntegralAddressSpace(OldAS) &&
              !DL.isNonIntegralAddressSpace(NewAS) &&
              DL.getPointerSize(OldAS) == DL.getPointerSize(NewAS));
    }

    // We can convert integers to integral pointers, but not to non-integral
    // pointers.
    if (OldTy->isIntegerTy())
      return !DL.isNonIntegralPointerType(NewTy);

    // We can convert integral pointers to integers, but non-integral pointers
    // need to remain pointers.
    if (!DL.isNonIntegralPointerType(OldTy))
      return NewTy->isIntegerTy();

    return false;
  }

  if (OldTy->isTargetExtTy() || NewTy->isTargetExtTy())
    return false;

  return true;
}

/// Generic routine to convert an SSA value to a value of a different
/// type.
///
/// This will try various different casting techniques, such as bitcasts,
/// inttoptr, and ptrtoint casts. Use the \c canConvertValue predicate to test
/// two types for viability with this routine.
static Value *convertValue(const DataLayout &DL, IRBuilderTy &IRB, Value *V,
                           Type *NewTy) {
  Type *OldTy = V->getType();
  assert(canConvertValue(DL, OldTy, NewTy) && "Value not convertable to type");

  if (OldTy == NewTy)
    return V;

  assert(!(isa<IntegerType>(OldTy) && isa<IntegerType>(NewTy)) &&
         "Integer types must be the exact same to convert.");

  // See if we need inttoptr for this type pair. May require additional bitcast.
  if (OldTy->isIntOrIntVectorTy() && NewTy->isPtrOrPtrVectorTy()) {
    // Expand <2 x i32> to i8* --> <2 x i32> to i64 to i8*
    // Expand i128 to <2 x i8*> --> i128 to <2 x i64> to <2 x i8*>
    // Expand <4 x i32> to <2 x i8*> --> <4 x i32> to <2 x i64> to <2 x i8*>
    // Directly handle i64 to i8*
    return IRB.CreateIntToPtr(IRB.CreateBitCast(V, DL.getIntPtrType(NewTy)),
                              NewTy);
  }

  // See if we need ptrtoint for this type pair. May require additional bitcast.
  if (OldTy->isPtrOrPtrVectorTy() && NewTy->isIntOrIntVectorTy()) {
    // Expand <2 x i8*> to i128 --> <2 x i8*> to <2 x i64> to i128
    // Expand i8* to <2 x i32> --> i8* to i64 to <2 x i32>
    // Expand <2 x i8*> to <4 x i32> --> <2 x i8*> to <2 x i64> to <4 x i32>
    // Expand i8* to i64 --> i8* to i64 to i64
    return IRB.CreateBitCast(IRB.CreatePtrToInt(V, DL.getIntPtrType(OldTy)),
                             NewTy);
  }

  if (OldTy->isPtrOrPtrVectorTy() && NewTy->isPtrOrPtrVectorTy()) {
    unsigned OldAS = OldTy->getPointerAddressSpace();
    unsigned NewAS = NewTy->getPointerAddressSpace();
    // To convert pointers with different address spaces (they are already
    // checked convertible, i.e. they have the same pointer size), so far we
    // cannot use `bitcast` (which has restrict on the same address space) or
    // `addrspacecast` (which is not always no-op casting). Instead, use a pair
    // of no-op `ptrtoint`/`inttoptr` casts through an integer with the same bit
    // size.
    if (OldAS != NewAS) {
      assert(DL.getPointerSize(OldAS) == DL.getPointerSize(NewAS));
      return IRB.CreateIntToPtr(IRB.CreatePtrToInt(V, DL.getIntPtrType(OldTy)),
                                NewTy);
    }
  }

  return IRB.CreateBitCast(V, NewTy);
}

/// Test whether the given slice use can be promoted to a vector.
///
/// This function is called to test each entry in a partition which is slated
/// for a single slice.
static bool isVectorPromotionViableForSlice(Partition &P, const Slice &S,
                                            VectorType *Ty,
                                            uint64_t ElementSize,
                                            const DataLayout &DL) {
  // First validate the slice offsets.
  uint64_t BeginOffset =
      std::max(S.beginOffset(), P.beginOffset()) - P.beginOffset();
  uint64_t BeginIndex = BeginOffset / ElementSize;
  if (BeginIndex * ElementSize != BeginOffset ||
      BeginIndex >= cast<FixedVectorType>(Ty)->getNumElements())
    return false;
  uint64_t EndOffset = std::min(S.endOffset(), P.endOffset()) - P.beginOffset();
  uint64_t EndIndex = EndOffset / ElementSize;
  if (EndIndex * ElementSize != EndOffset ||
      EndIndex > cast<FixedVectorType>(Ty)->getNumElements())
    return false;

  assert(EndIndex > BeginIndex && "Empty vector!");
  uint64_t NumElements = EndIndex - BeginIndex;
  Type *SliceTy = (NumElements == 1)
                      ? Ty->getElementType()
                      : FixedVectorType::get(Ty->getElementType(), NumElements);

  Type *SplitIntTy =
      Type::getIntNTy(Ty->getContext(), NumElements * ElementSize * 8);

  Use *U = S.getUse();

  if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(U->getUser())) {
    if (MI->isVolatile())
      return false;
    if (!S.isSplittable())
      return false; // Skip any unsplittable intrinsics.
  } else if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U->getUser())) {
    if (!II->isLifetimeStartOrEnd() && !II->isDroppable())
      return false;
  } else if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
    if (LI->isVolatile())
      return false;
    Type *LTy = LI->getType();
    // Disable vector promotion when there are loads or stores of an FCA.
    if (LTy->isStructTy())
      return false;
    if (P.beginOffset() > S.beginOffset() || P.endOffset() < S.endOffset()) {
      assert(LTy->isIntegerTy());
      LTy = SplitIntTy;
    }
    if (!canConvertValue(DL, SliceTy, LTy))
      return false;
  } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
    if (SI->isVolatile())
      return false;
    Type *STy = SI->getValueOperand()->getType();
    // Disable vector promotion when there are loads or stores of an FCA.
    if (STy->isStructTy())
      return false;
    if (P.beginOffset() > S.beginOffset() || P.endOffset() < S.endOffset()) {
      assert(STy->isIntegerTy());
      STy = SplitIntTy;
    }
    if (!canConvertValue(DL, STy, SliceTy))
      return false;
  } else {
    return false;
  }

  return true;
}

/// Test whether a vector type is viable for promotion.
///
/// This implements the necessary checking for \c checkVectorTypesForPromotion
/// (and thus isVectorPromotionViable) over all slices of the alloca for the
/// given VectorType.
static bool checkVectorTypeForPromotion(Partition &P, VectorType *VTy,
                                        const DataLayout &DL) {
  uint64_t ElementSize =
      DL.getTypeSizeInBits(VTy->getElementType()).getFixedValue();

  // While the definition of LLVM vectors is bitpacked, we don't support sizes
  // that aren't byte sized.
  if (ElementSize % 8)
    return false;
  assert((DL.getTypeSizeInBits(VTy).getFixedValue() % 8) == 0 &&
         "vector size not a multiple of element size?");
  ElementSize /= 8;

  for (const Slice &S : P)
    if (!isVectorPromotionViableForSlice(P, S, VTy, ElementSize, DL))
      return false;

  for (const Slice *S : P.splitSliceTails())
    if (!isVectorPromotionViableForSlice(P, *S, VTy, ElementSize, DL))
      return false;

  return true;
}

/// Test whether any vector type in \p CandidateTys is viable for promotion.
///
/// This implements the necessary checking for \c isVectorPromotionViable over
/// all slices of the alloca for the given VectorType.
static VectorType *
checkVectorTypesForPromotion(Partition &P, const DataLayout &DL,
                             SmallVectorImpl<VectorType *> &CandidateTys,
                             bool HaveCommonEltTy, Type *CommonEltTy,
                             bool HaveVecPtrTy, bool HaveCommonVecPtrTy,
                             VectorType *CommonVecPtrTy) {
  // If we didn't find a vector type, nothing to do here.
  if (CandidateTys.empty())
    return nullptr;

  // Pointer-ness is sticky, if we had a vector-of-pointers candidate type,
  // then we should choose it, not some other alternative.
  // But, we can't perform a no-op pointer address space change via bitcast,
  // so if we didn't have a common pointer element type, bail.
  if (HaveVecPtrTy && !HaveCommonVecPtrTy)
    return nullptr;

  // Try to pick the "best" element type out of the choices.
  if (!HaveCommonEltTy && HaveVecPtrTy) {
    // If there was a pointer element type, there's really only one choice.
    CandidateTys.clear();
    CandidateTys.push_back(CommonVecPtrTy);
  } else if (!HaveCommonEltTy && !HaveVecPtrTy) {
    // Integer-ify vector types.
    for (VectorType *&VTy : CandidateTys) {
      if (!VTy->getElementType()->isIntegerTy())
        VTy = cast<VectorType>(VTy->getWithNewType(IntegerType::getIntNTy(
            VTy->getContext(), VTy->getScalarSizeInBits())));
    }

    // Rank the remaining candidate vector types. This is easy because we know
    // they're all integer vectors. We sort by ascending number of elements.
    auto RankVectorTypesComp = [&DL](VectorType *RHSTy, VectorType *LHSTy) {
      (void)DL;
      assert(DL.getTypeSizeInBits(RHSTy).getFixedValue() ==
                 DL.getTypeSizeInBits(LHSTy).getFixedValue() &&
             "Cannot have vector types of different sizes!");
      assert(RHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      assert(LHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      return cast<FixedVectorType>(RHSTy)->getNumElements() <
             cast<FixedVectorType>(LHSTy)->getNumElements();
    };
    auto RankVectorTypesEq = [&DL](VectorType *RHSTy, VectorType *LHSTy) {
      (void)DL;
      assert(DL.getTypeSizeInBits(RHSTy).getFixedValue() ==
                 DL.getTypeSizeInBits(LHSTy).getFixedValue() &&
             "Cannot have vector types of different sizes!");
      assert(RHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      assert(LHSTy->getElementType()->isIntegerTy() &&
             "All non-integer types eliminated!");
      return cast<FixedVectorType>(RHSTy)->getNumElements() ==
             cast<FixedVectorType>(LHSTy)->getNumElements();
    };
    llvm::sort(CandidateTys, RankVectorTypesComp);
    CandidateTys.erase(llvm::unique(CandidateTys, RankVectorTypesEq),
                       CandidateTys.end());
  } else {
// The only way to have the same element type in every vector type is to
// have the same vector type. Check that and remove all but one.
#ifndef NDEBUG
    for (VectorType *VTy : CandidateTys) {
      assert(VTy->getElementType() == CommonEltTy &&
             "Unaccounted for element type!");
      assert(VTy == CandidateTys[0] &&
             "Different vector types with the same element type!");
    }
#endif
    CandidateTys.resize(1);
  }

  // FIXME: hack. Do we have a named constant for this?
  // SDAG SDNode can't have more than 65535 operands.
  llvm::erase_if(CandidateTys, [](VectorType *VTy) {
    return cast<FixedVectorType>(VTy)->getNumElements() >
           std::numeric_limits<unsigned short>::max();
  });

  for (VectorType *VTy : CandidateTys)
    if (checkVectorTypeForPromotion(P, VTy, DL))
      return VTy;

  return nullptr;
}

static VectorType *createAndCheckVectorTypesForPromotion(
    SetVector<Type *> &OtherTys, ArrayRef<VectorType *> CandidateTysCopy,
    function_ref<void(Type *)> CheckCandidateType, Partition &P,
    const DataLayout &DL, SmallVectorImpl<VectorType *> &CandidateTys,
    bool &HaveCommonEltTy, Type *&CommonEltTy, bool &HaveVecPtrTy,
    bool &HaveCommonVecPtrTy, VectorType *&CommonVecPtrTy) {
  [[maybe_unused]] VectorType *OriginalElt =
      CandidateTysCopy.size() ? CandidateTysCopy[0] : nullptr;
  // Consider additional vector types where the element type size is a
  // multiple of load/store element size.
  for (Type *Ty : OtherTys) {
    if (!VectorType::isValidElementType(Ty))
      continue;
    unsigned TypeSize = DL.getTypeSizeInBits(Ty).getFixedValue();
    // Make a copy of CandidateTys and iterate through it, because we
    // might append to CandidateTys in the loop.
    for (VectorType *const VTy : CandidateTysCopy) {
      // The elements in the copy should remain invariant throughout the loop
      assert(CandidateTysCopy[0] == OriginalElt && "Different Element");
      unsigned VectorSize = DL.getTypeSizeInBits(VTy).getFixedValue();
      unsigned ElementSize =
          DL.getTypeSizeInBits(VTy->getElementType()).getFixedValue();
      if (TypeSize != VectorSize && TypeSize != ElementSize &&
          VectorSize % TypeSize == 0) {
        VectorType *NewVTy = VectorType::get(Ty, VectorSize / TypeSize, false);
        CheckCandidateType(NewVTy);
      }
    }
  }

  return checkVectorTypesForPromotion(P, DL, CandidateTys, HaveCommonEltTy,
                                      CommonEltTy, HaveVecPtrTy,
                                      HaveCommonVecPtrTy, CommonVecPtrTy);
}

/// Test whether the given alloca partitioning and range of slices can be
/// promoted to a vector.
///
/// This is a quick test to check whether we can rewrite a particular alloca
/// partition (and its newly formed alloca) into a vector alloca with only
/// whole-vector loads and stores such that it could be promoted to a vector
/// SSA value. We only can ensure this for a limited set of operations, and we
/// don't want to do the rewrites unless we are confident that the result will
/// be promotable, so we have an early test here.
static VectorType *isVectorPromotionViable(Partition &P, const DataLayout &DL) {
  // Collect the candidate types for vector-based promotion. Also track whether
  // we have different element types.
  SmallVector<VectorType *, 4> CandidateTys;
  SetVector<Type *> LoadStoreTys;
  SetVector<Type *> DeferredTys;
  Type *CommonEltTy = nullptr;
  VectorType *CommonVecPtrTy = nullptr;
  bool HaveVecPtrTy = false;
  bool HaveCommonEltTy = true;
  bool HaveCommonVecPtrTy = true;
  auto CheckCandidateType = [&](Type *Ty) {
    if (auto *VTy = dyn_cast<VectorType>(Ty)) {
      // Return if bitcast to vectors is different for total size in bits.
      if (!CandidateTys.empty()) {
        VectorType *V = CandidateTys[0];
        if (DL.getTypeSizeInBits(VTy).getFixedValue() !=
            DL.getTypeSizeInBits(V).getFixedValue()) {
          CandidateTys.clear();
          return;
        }
      }
      CandidateTys.push_back(VTy);
      Type *EltTy = VTy->getElementType();

      if (!CommonEltTy)
        CommonEltTy = EltTy;
      else if (CommonEltTy != EltTy)
        HaveCommonEltTy = false;

      if (EltTy->isPointerTy()) {
        HaveVecPtrTy = true;
        if (!CommonVecPtrTy)
          CommonVecPtrTy = VTy;
        else if (CommonVecPtrTy != VTy)
          HaveCommonVecPtrTy = false;
      }
    }
  };

  // Put load and store types into a set for de-duplication.
  for (const Slice &S : P) {
    Type *Ty;
    if (auto *LI = dyn_cast<LoadInst>(S.getUse()->getUser()))
      Ty = LI->getType();
    else if (auto *SI = dyn_cast<StoreInst>(S.getUse()->getUser()))
      Ty = SI->getValueOperand()->getType();
    else
      continue;

    auto CandTy = Ty->getScalarType();
    if (CandTy->isPointerTy() && (S.beginOffset() != P.beginOffset() ||
                                  S.endOffset() != P.endOffset())) {
      DeferredTys.insert(Ty);
      continue;
    }

    LoadStoreTys.insert(Ty);
    // Consider any loads or stores that are the exact size of the slice.
    if (S.beginOffset() == P.beginOffset() && S.endOffset() == P.endOffset())
      CheckCandidateType(Ty);
  }

  SmallVector<VectorType *, 4> CandidateTysCopy = CandidateTys;
  if (auto *VTy = createAndCheckVectorTypesForPromotion(
          LoadStoreTys, CandidateTysCopy, CheckCandidateType, P, DL,
          CandidateTys, HaveCommonEltTy, CommonEltTy, HaveVecPtrTy,
          HaveCommonVecPtrTy, CommonVecPtrTy))
    return VTy;

  CandidateTys.clear();
  return createAndCheckVectorTypesForPromotion(
      DeferredTys, CandidateTysCopy, CheckCandidateType, P, DL, CandidateTys,
      HaveCommonEltTy, CommonEltTy, HaveVecPtrTy, HaveCommonVecPtrTy,
      CommonVecPtrTy);
}

/// Test whether a slice of an alloca is valid for integer widening.
///
/// This implements the necessary checking for the \c isIntegerWideningViable
/// test below on a single slice of the alloca.
static bool isIntegerWideningViableForSlice(const Slice &S,
                                            uint64_t AllocBeginOffset,
                                            Type *AllocaTy,
                                            const DataLayout &DL,
                                            bool &WholeAllocaOp) {
  uint64_t Size = DL.getTypeStoreSize(AllocaTy).getFixedValue();

  uint64_t RelBegin = S.beginOffset() - AllocBeginOffset;
  uint64_t RelEnd = S.endOffset() - AllocBeginOffset;

  Use *U = S.getUse();

  // Lifetime intrinsics operate over the whole alloca whose sizes are usually
  // larger than other load/store slices (RelEnd > Size). But lifetime are
  // always promotable and should not impact other slices' promotability of the
  // partition.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U->getUser())) {
    if (II->isLifetimeStartOrEnd() || II->isDroppable())
      return true;
  }

  // We can't reasonably handle cases where the load or store extends past
  // the end of the alloca's type and into its padding.
  if (RelEnd > Size)
    return false;

  if (LoadInst *LI = dyn_cast<LoadInst>(U->getUser())) {
    if (LI->isVolatile())
      return false;
    // We can't handle loads that extend past the allocated memory.
    if (DL.getTypeStoreSize(LI->getType()).getFixedValue() > Size)
      return false;
    // So far, AllocaSliceRewriter does not support widening split slice tails
    // in rewriteIntegerLoad.
    if (S.beginOffset() < AllocBeginOffset)
      return false;
    // Note that we don't count vector loads or stores as whole-alloca
    // operations which enable integer widening because we would prefer to use
    // vector widening instead.
    if (!isa<VectorType>(LI->getType()) && RelBegin == 0 && RelEnd == Size)
      WholeAllocaOp = true;
    if (IntegerType *ITy = dyn_cast<IntegerType>(LI->getType())) {
      if (ITy->getBitWidth() < DL.getTypeStoreSizeInBits(ITy).getFixedValue())
        return false;
    } else if (RelBegin != 0 || RelEnd != Size ||
               !canConvertValue(DL, AllocaTy, LI->getType())) {
      // Non-integer loads need to be convertible from the alloca type so that
      // they are promotable.
      return false;
    }
  } else if (StoreInst *SI = dyn_cast<StoreInst>(U->getUser())) {
    Type *ValueTy = SI->getValueOperand()->getType();
    if (SI->isVolatile())
      return false;
    // We can't handle stores that extend past the allocated memory.
    if (DL.getTypeStoreSize(ValueTy).getFixedValue() > Size)
      return false;
    // So far, AllocaSliceRewriter does not support widening split slice tails
    // in rewriteIntegerStore.
    if (S.beginOffset() < AllocBeginOffset)
      return false;
    // Note that we don't count vector loads or stores as whole-alloca
    // operations which enable integer widening because we would prefer to use
    // vector widening instead.
    if (!isa<VectorType>(ValueTy) && RelBegin == 0 && RelEnd == Size)
      WholeAllocaOp = true;
    if (IntegerType *ITy = dyn_cast<IntegerType>(ValueTy)) {
      if (ITy->getBitWidth() < DL.getTypeStoreSizeInBits(ITy).getFixedValue())
        return false;
    } else if (RelBegin != 0 || RelEnd != Size ||
               !canConvertValue(DL, ValueTy, AllocaTy)) {
      // Non-integer stores need to be convertible to the alloca type so that
      // they are promotable.
      return false;
    }
  } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(U->getUser())) {
    if (MI->isVolatile() || !isa<Constant>(MI->getLength()))
      return false;
    if (!S.isSplittable())
      return false; // Skip any unsplittable intrinsics.
  } else {
    return false;
  }

  return true;
}

/// Test whether the given alloca partition's integer operations can be
/// widened to promotable ones.
///
/// This is a quick test to check whether we can rewrite the integer loads and
/// stores to a particular alloca into wider loads and stores and be able to
/// promote the resulting alloca.
static bool isIntegerWideningViable(Partition &P, Type *AllocaTy,
                                    const DataLayout &DL) {
  uint64_t SizeInBits = DL.getTypeSizeInBits(AllocaTy).getFixedValue();
  // Don't create integer types larger than the maximum bitwidth.
  if (SizeInBits > IntegerType::MAX_INT_BITS)
    return false;

  // Don't try to handle allocas with bit-padding.
  if (SizeInBits != DL.getTypeStoreSizeInBits(AllocaTy).getFixedValue())
    return false;

  // We need to ensure that an integer type with the appropriate bitwidth can
  // be converted to the alloca type, whatever that is. We don't want to force
  // the alloca itself to have an integer type if there is a more suitable one.
  Type *IntTy = Type::getIntNTy(AllocaTy->getContext(), SizeInBits);
  if (!canConvertValue(DL, AllocaTy, IntTy) ||
      !canConvertValue(DL, IntTy, AllocaTy))
    return false;

  // While examining uses, we ensure that the alloca has a covering load or
  // store. We don't want to widen the integer operations only to fail to
  // promote due to some other unsplittable entry (which we may make splittable
  // later). However, if there are only splittable uses, go ahead and assume
  // that we cover the alloca.
  // FIXME: We shouldn't consider split slices that happen to start in the
  // partition here...
  bool WholeAllocaOp = P.empty() && DL.isLegalInteger(SizeInBits);

  for (const Slice &S : P)
    if (!isIntegerWideningViableForSlice(S, P.beginOffset(), AllocaTy, DL,
                                         WholeAllocaOp))
      return false;

  for (const Slice *S : P.splitSliceTails())
    if (!isIntegerWideningViableForSlice(*S, P.beginOffset(), AllocaTy, DL,
                                         WholeAllocaOp))
      return false;

  return WholeAllocaOp;
}

static Value *extractInteger(const DataLayout &DL, IRBuilderTy &IRB, Value *V,
                             IntegerType *Ty, uint64_t Offset,
                             const Twine &Name) {
  LLVM_DEBUG(dbgs() << "       start: " << *V << "\n");
  IntegerType *IntTy = cast<IntegerType>(V->getType());
  assert(DL.getTypeStoreSize(Ty).getFixedValue() + Offset <=
             DL.getTypeStoreSize(IntTy).getFixedValue() &&
         "Element extends past full value");
  uint64_t ShAmt = 8 * Offset;
  if (DL.isBigEndian())
    ShAmt = 8 * (DL.getTypeStoreSize(IntTy).getFixedValue() -
                 DL.getTypeStoreSize(Ty).getFixedValue() - Offset);
  if (ShAmt) {
    V = IRB.CreateLShr(V, ShAmt, Name + ".shift");
    LLVM_DEBUG(dbgs() << "     shifted: " << *V << "\n");
  }
  assert(Ty->getBitWidth() <= IntTy->getBitWidth() &&
         "Cannot extract to a larger integer!");
  if (Ty != IntTy) {
    V = IRB.CreateTrunc(V, Ty, Name + ".trunc");
    LLVM_DEBUG(dbgs() << "     trunced: " << *V << "\n");
  }
  return V;
}

static Value *insertInteger(const DataLayout &DL, IRBuilderTy &IRB, Value *Old,
                            Value *V, uint64_t Offset, const Twine &Name) {
  IntegerType *IntTy = cast<IntegerType>(Old->getType());
  IntegerType *Ty = cast<IntegerType>(V->getType());
  assert(Ty->getBitWidth() <= IntTy->getBitWidth() &&
         "Cannot insert a larger integer!");
  LLVM_DEBUG(dbgs() << "       start: " << *V << "\n");
  if (Ty != IntTy) {
    V = IRB.CreateZExt(V, IntTy, Name + ".ext");
    LLVM_DEBUG(dbgs() << "    extended: " << *V << "\n");
  }
  assert(DL.getTypeStoreSize(Ty).getFixedValue() + Offset <=
             DL.getTypeStoreSize(IntTy).getFixedValue() &&
         "Element store outside of alloca store");
  uint64_t ShAmt = 8 * Offset;
  if (DL.isBigEndian())
    ShAmt = 8 * (DL.getTypeStoreSize(IntTy).getFixedValue() -
                 DL.getTypeStoreSize(Ty).getFixedValue() - Offset);
  if (ShAmt) {
    V = IRB.CreateShl(V, ShAmt, Name + ".shift");
    LLVM_DEBUG(dbgs() << "     shifted: " << *V << "\n");
  }

  if (ShAmt || Ty->getBitWidth() < IntTy->getBitWidth()) {
    APInt Mask = ~Ty->getMask().zext(IntTy->getBitWidth()).shl(ShAmt);
    Old = IRB.CreateAnd(Old, Mask, Name + ".mask");
    LLVM_DEBUG(dbgs() << "      masked: " << *Old << "\n");
    V = IRB.CreateOr(Old, V, Name + ".insert");
    LLVM_DEBUG(dbgs() << "    inserted: " << *V << "\n");
  }
  return V;
}

static Value *extractVector(IRBuilderTy &IRB, Value *V, unsigned BeginIndex,
                            unsigned EndIndex, const Twine &Name) {
  auto *VecTy = cast<FixedVectorType>(V->getType());
  unsigned NumElements = EndIndex - BeginIndex;
  assert(NumElements <= VecTy->getNumElements() && "Too many elements!");

  if (NumElements == VecTy->getNumElements())
    return V;

  if (NumElements == 1) {
    V = IRB.CreateExtractElement(V, IRB.getInt32(BeginIndex),
                                 Name + ".extract");
    LLVM_DEBUG(dbgs() << "     extract: " << *V << "\n");
    return V;
  }

  auto Mask = llvm::to_vector<8>(llvm::seq<int>(BeginIndex, EndIndex));
  V = IRB.CreateShuffleVector(V, Mask, Name + ".extract");
  LLVM_DEBUG(dbgs() << "     shuffle: " << *V << "\n");
  return V;
}

static Value *insertVector(IRBuilderTy &IRB, Value *Old, Value *V,
                           unsigned BeginIndex, const Twine &Name) {
  VectorType *VecTy = cast<VectorType>(Old->getType());
  assert(VecTy && "Can only insert a vector into a vector");

  VectorType *Ty = dyn_cast<VectorType>(V->getType());
  if (!Ty) {
    // Single element to insert.
    V = IRB.CreateInsertElement(Old, V, IRB.getInt32(BeginIndex),
                                Name + ".insert");
    LLVM_DEBUG(dbgs() << "     insert: " << *V << "\n");
    return V;
  }

  assert(cast<FixedVectorType>(Ty)->getNumElements() <=
             cast<FixedVectorType>(VecTy)->getNumElements() &&
         "Too many elements!");
  if (cast<FixedVectorType>(Ty)->getNumElements() ==
      cast<FixedVectorType>(VecTy)->getNumElements()) {
    assert(V->getType() == VecTy && "Vector type mismatch");
    return V;
  }
  unsigned EndIndex = BeginIndex + cast<FixedVectorType>(Ty)->getNumElements();

  // When inserting a smaller vector into the larger to store, we first
  // use a shuffle vector to widen it with undef elements, and then
  // a second shuffle vector to select between the loaded vector and the
  // incoming vector.
  SmallVector<int, 8> Mask;
  Mask.reserve(cast<FixedVectorType>(VecTy)->getNumElements());
  for (unsigned i = 0; i != cast<FixedVectorType>(VecTy)->getNumElements(); ++i)
    if (i >= BeginIndex && i < EndIndex)
      Mask.push_back(i - BeginIndex);
    else
      Mask.push_back(-1);
  V = IRB.CreateShuffleVector(V, Mask, Name + ".expand");
  LLVM_DEBUG(dbgs() << "    shuffle: " << *V << "\n");

  SmallVector<Constant *, 8> Mask2;
  Mask2.reserve(cast<FixedVectorType>(VecTy)->getNumElements());
  for (unsigned i = 0; i != cast<FixedVectorType>(VecTy)->getNumElements(); ++i)
    Mask2.push_back(IRB.getInt1(i >= BeginIndex && i < EndIndex));

  V = IRB.CreateSelect(ConstantVector::get(Mask2), V, Old, Name + "blend");

  LLVM_DEBUG(dbgs() << "    blend: " << *V << "\n");
  return V;
}

namespace {

/// Visitor to rewrite instructions using p particular slice of an alloca
/// to use a new alloca.
///
/// Also implements the rewriting to vector-based accesses when the partition
/// passes the isVectorPromotionViable predicate. Most of the rewriting logic
/// lives here.
class AllocaSliceRewriter : public InstVisitor<AllocaSliceRewriter, bool> {
  // Befriend the base class so it can delegate to private visit methods.
  friend class InstVisitor<AllocaSliceRewriter, bool>;

  using Base = InstVisitor<AllocaSliceRewriter, bool>;

  const DataLayout &DL;
  AllocaSlices &AS;
  SROA &Pass;
  AllocaInst &OldAI, &NewAI;
  const uint64_t NewAllocaBeginOffset, NewAllocaEndOffset;
  Type *NewAllocaTy;

  // This is a convenience and flag variable that will be null unless the new
  // alloca's integer operations should be widened to this integer type due to
  // passing isIntegerWideningViable above. If it is non-null, the desired
  // integer type will be stored here for easy access during rewriting.
  IntegerType *IntTy;

  // If we are rewriting an alloca partition which can be written as pure
  // vector operations, we stash extra information here. When VecTy is
  // non-null, we have some strict guarantees about the rewritten alloca:
  //   - The new alloca is exactly the size of the vector type here.
  //   - The accesses all either map to the entire vector or to a single
  //     element.
  //   - The set of accessing instructions is only one of those handled above
  //     in isVectorPromotionViable. Generally these are the same access kinds
  //     which are promotable via mem2reg.
  VectorType *VecTy;
  Type *ElementTy;
  uint64_t ElementSize;

  // The original offset of the slice currently being rewritten relative to
  // the original alloca.
  uint64_t BeginOffset = 0;
  uint64_t EndOffset = 0;

  // The new offsets of the slice currently being rewritten relative to the
  // original alloca.
  uint64_t NewBeginOffset = 0, NewEndOffset = 0;

  uint64_t SliceSize = 0;
  bool IsSplittable = false;
  bool IsSplit = false;
  Use *OldUse = nullptr;
  Instruction *OldPtr = nullptr;

  // Track post-rewrite users which are PHI nodes and Selects.
  SmallSetVector<PHINode *, 8> &PHIUsers;
  SmallSetVector<SelectInst *, 8> &SelectUsers;

  // Utility IR builder, whose name prefix is setup for each visited use, and
  // the insertion point is set to point to the user.
  IRBuilderTy IRB;

  // Return the new alloca, addrspacecasted if required to avoid changing the
  // addrspace of a volatile access.
  Value *getPtrToNewAI(unsigned AddrSpace, bool IsVolatile) {
    if (!IsVolatile || AddrSpace == NewAI.getType()->getPointerAddressSpace())
      return &NewAI;

    Type *AccessTy = IRB.getPtrTy(AddrSpace);
    return IRB.CreateAddrSpaceCast(&NewAI, AccessTy);
  }

public:
  AllocaSliceRewriter(const DataLayout &DL, AllocaSlices &AS, SROA &Pass,
                      AllocaInst &OldAI, AllocaInst &NewAI,
                      uint64_t NewAllocaBeginOffset,
                      uint64_t NewAllocaEndOffset, bool IsIntegerPromotable,
                      VectorType *PromotableVecTy,
                      SmallSetVector<PHINode *, 8> &PHIUsers,
                      SmallSetVector<SelectInst *, 8> &SelectUsers)
      : DL(DL), AS(AS), Pass(Pass), OldAI(OldAI), NewAI(NewAI),
        NewAllocaBeginOffset(NewAllocaBeginOffset),
        NewAllocaEndOffset(NewAllocaEndOffset),
        NewAllocaTy(NewAI.getAllocatedType()),
        IntTy(
            IsIntegerPromotable
                ? Type::getIntNTy(NewAI.getContext(),
                                  DL.getTypeSizeInBits(NewAI.getAllocatedType())
                                      .getFixedValue())
                : nullptr),
        VecTy(PromotableVecTy),
        ElementTy(VecTy ? VecTy->getElementType() : nullptr),
        ElementSize(VecTy ? DL.getTypeSizeInBits(ElementTy).getFixedValue() / 8
                          : 0),
        PHIUsers(PHIUsers), SelectUsers(SelectUsers),
        IRB(NewAI.getContext(), ConstantFolder()) {
    if (VecTy) {
      assert((DL.getTypeSizeInBits(ElementTy).getFixedValue() % 8) == 0 &&
             "Only multiple-of-8 sized vector elements are viable");
      ++NumVectorized;
    }
    assert((!IntTy && !VecTy) || (IntTy && !VecTy) || (!IntTy && VecTy));
  }

  bool visit(AllocaSlices::const_iterator I) {
    bool CanSROA = true;
    BeginOffset = I->beginOffset();
    EndOffset = I->endOffset();
    IsSplittable = I->isSplittable();
    IsSplit =
        BeginOffset < NewAllocaBeginOffset || EndOffset > NewAllocaEndOffset;
    LLVM_DEBUG(dbgs() << "  rewriting " << (IsSplit ? "split " : ""));
    LLVM_DEBUG(AS.printSlice(dbgs(), I, ""));
    LLVM_DEBUG(dbgs() << "\n");

    // Compute the intersecting offset range.
    assert(BeginOffset < NewAllocaEndOffset);
    assert(EndOffset > NewAllocaBeginOffset);
    NewBeginOffset = std::max(BeginOffset, NewAllocaBeginOffset);
    NewEndOffset = std::min(EndOffset, NewAllocaEndOffset);

    SliceSize = NewEndOffset - NewBeginOffset;
    LLVM_DEBUG(dbgs() << "   Begin:(" << BeginOffset << ", " << EndOffset
                      << ") NewBegin:(" << NewBeginOffset << ", "
                      << NewEndOffset << ") NewAllocaBegin:("
                      << NewAllocaBeginOffset << ", " << NewAllocaEndOffset
                      << ")\n");
    assert(IsSplit || NewBeginOffset == BeginOffset);
    OldUse = I->getUse();
    OldPtr = cast<Instruction>(OldUse->get());

    Instruction *OldUserI = cast<Instruction>(OldUse->getUser());
    IRB.SetInsertPoint(OldUserI);
    IRB.SetCurrentDebugLocation(OldUserI->getDebugLoc());
    IRB.getInserter().SetNamePrefix(Twine(NewAI.getName()) + "." +
                                    Twine(BeginOffset) + ".");

    CanSROA &= visit(cast<Instruction>(OldUse->getUser()));
    if (VecTy || IntTy)
      assert(CanSROA);
    return CanSROA;
  }

private:
  // Make sure the other visit overloads are visible.
  using Base::visit;

  // Every instruction which can end up as a user must have a rewrite rule.
  bool visitInstruction(Instruction &I) {
    LLVM_DEBUG(dbgs() << "    !!!! Cannot rewrite: " << I << "\n");
    llvm_unreachable("No rewrite rule for this instruction!");
  }

  Value *getNewAllocaSlicePtr(IRBuilderTy &IRB, Type *PointerTy) {
    // Note that the offset computation can use BeginOffset or NewBeginOffset
    // interchangeably for unsplit slices.
    assert(IsSplit || BeginOffset == NewBeginOffset);
    uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;

#ifndef NDEBUG
    StringRef OldName = OldPtr->getName();
    // Skip through the last '.sroa.' component of the name.
    size_t LastSROAPrefix = OldName.rfind(".sroa.");
    if (LastSROAPrefix != StringRef::npos) {
      OldName = OldName.substr(LastSROAPrefix + strlen(".sroa."));
      // Look for an SROA slice index.
      size_t IndexEnd = OldName.find_first_not_of("0123456789");
      if (IndexEnd != StringRef::npos && OldName[IndexEnd] == '.') {
        // Strip the index and look for the offset.
        OldName = OldName.substr(IndexEnd + 1);
        size_t OffsetEnd = OldName.find_first_not_of("0123456789");
        if (OffsetEnd != StringRef::npos && OldName[OffsetEnd] == '.')
          // Strip the offset.
          OldName = OldName.substr(OffsetEnd + 1);
      }
    }
    // Strip any SROA suffixes as well.
    OldName = OldName.substr(0, OldName.find(".sroa_"));
#endif

    return getAdjustedPtr(IRB, DL, &NewAI,
                          APInt(DL.getIndexTypeSizeInBits(PointerTy), Offset),
                          PointerTy,
#ifndef NDEBUG
                          Twine(OldName) + "."
#else
                          Twine()
#endif
    );
  }

  /// Compute suitable alignment to access this slice of the *new*
  /// alloca.
  ///
  /// You can optionally pass a type to this routine and if that type's ABI
  /// alignment is itself suitable, this will return zero.
  Align getSliceAlign() {
    return commonAlignment(NewAI.getAlign(),
                           NewBeginOffset - NewAllocaBeginOffset);
  }

  unsigned getIndex(uint64_t Offset) {
    assert(VecTy && "Can only call getIndex when rewriting a vector");
    uint64_t RelOffset = Offset - NewAllocaBeginOffset;
    assert(RelOffset / ElementSize < UINT32_MAX && "Index out of bounds");
    uint32_t Index = RelOffset / ElementSize;
    assert(Index * ElementSize == RelOffset);
    return Index;
  }

  void deleteIfTriviallyDead(Value *V) {
    Instruction *I = cast<Instruction>(V);
    if (isInstructionTriviallyDead(I))
      Pass.DeadInsts.push_back(I);
  }

  Value *rewriteVectorizedLoadInst(LoadInst &LI) {
    unsigned BeginIndex = getIndex(NewBeginOffset);
    unsigned EndIndex = getIndex(NewEndOffset);
    assert(EndIndex > BeginIndex && "Empty vector!");

    LoadInst *Load = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                           NewAI.getAlign(), "load");

    Load->copyMetadata(LI, {LLVMContext::MD_mem_parallel_loop_access,
                            LLVMContext::MD_access_group});
    return extractVector(IRB, Load, BeginIndex, EndIndex, "vec");
  }

  Value *rewriteIntegerLoad(LoadInst &LI) {
    assert(IntTy && "We cannot insert an integer to the alloca");
    assert(!LI.isVolatile());
    Value *V = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                     NewAI.getAlign(), "load");
    V = convertValue(DL, IRB, V, IntTy);
    assert(NewBeginOffset >= NewAllocaBeginOffset && "Out of bounds offset");
    uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
    if (Offset > 0 || NewEndOffset < NewAllocaEndOffset) {
      IntegerType *ExtractTy = Type::getIntNTy(LI.getContext(), SliceSize * 8);
      V = extractInteger(DL, IRB, V, ExtractTy, Offset, "extract");
    }
    // It is possible that the extracted type is not the load type. This
    // happens if there is a load past the end of the alloca, and as
    // a consequence the slice is narrower but still a candidate for integer
    // lowering. To handle this case, we just zero extend the extracted
    // integer.
    assert(cast<IntegerType>(LI.getType())->getBitWidth() >= SliceSize * 8 &&
           "Can only handle an extract for an overly wide load");
    if (cast<IntegerType>(LI.getType())->getBitWidth() > SliceSize * 8)
      V = IRB.CreateZExt(V, LI.getType());
    return V;
  }

  bool visitLoadInst(LoadInst &LI) {
    LLVM_DEBUG(dbgs() << "    original: " << LI << "\n");
    Value *OldOp = LI.getOperand(0);
    assert(OldOp == OldPtr);

    AAMDNodes AATags = LI.getAAMetadata();

    unsigned AS = LI.getPointerAddressSpace();

    Type *TargetTy = IsSplit ? Type::getIntNTy(LI.getContext(), SliceSize * 8)
                             : LI.getType();
    const bool IsLoadPastEnd =
        DL.getTypeStoreSize(TargetTy).getFixedValue() > SliceSize;
    bool IsPtrAdjusted = false;
    Value *V;
    if (VecTy) {
      V = rewriteVectorizedLoadInst(LI);
    } else if (IntTy && LI.getType()->isIntegerTy()) {
      V = rewriteIntegerLoad(LI);
    } else if (NewBeginOffset == NewAllocaBeginOffset &&
               NewEndOffset == NewAllocaEndOffset &&
               (canConvertValue(DL, NewAllocaTy, TargetTy) ||
                (IsLoadPastEnd && NewAllocaTy->isIntegerTy() &&
                 TargetTy->isIntegerTy() && !LI.isVolatile()))) {
      Value *NewPtr =
          getPtrToNewAI(LI.getPointerAddressSpace(), LI.isVolatile());
      LoadInst *NewLI = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), NewPtr,
                                              NewAI.getAlign(), LI.isVolatile(),
                                              LI.getName());
      if (LI.isVolatile())
        NewLI->setAtomic(LI.getOrdering(), LI.getSyncScopeID());
      if (NewLI->isAtomic())
        NewLI->setAlignment(LI.getAlign());

      // Copy any metadata that is valid for the new load. This may require
      // conversion to a different kind of metadata, e.g. !nonnull might change
      // to !range or vice versa.
      copyMetadataForLoad(*NewLI, LI);

      // Do this after copyMetadataForLoad() to preserve the TBAA shift.
      if (AATags)
        NewLI->setAAMetadata(AATags.adjustForAccess(
            NewBeginOffset - BeginOffset, NewLI->getType(), DL));

      // Try to preserve nonnull metadata
      V = NewLI;

      // If this is an integer load past the end of the slice (which means the
      // bytes outside the slice are undef or this load is dead) just forcibly
      // fix the integer size with correct handling of endianness.
      if (auto *AITy = dyn_cast<IntegerType>(NewAllocaTy))
        if (auto *TITy = dyn_cast<IntegerType>(TargetTy))
          if (AITy->getBitWidth() < TITy->getBitWidth()) {
            V = IRB.CreateZExt(V, TITy, "load.ext");
            if (DL.isBigEndian())
              V = IRB.CreateShl(V, TITy->getBitWidth() - AITy->getBitWidth(),
                                "endian_shift");
          }
    } else {
      Type *LTy = IRB.getPtrTy(AS);
      LoadInst *NewLI =
          IRB.CreateAlignedLoad(TargetTy, getNewAllocaSlicePtr(IRB, LTy),
                                getSliceAlign(), LI.isVolatile(), LI.getName());

      if (AATags)
        NewLI->setAAMetadata(AATags.adjustForAccess(
            NewBeginOffset - BeginOffset, NewLI->getType(), DL));

      if (LI.isVolatile())
        NewLI->setAtomic(LI.getOrdering(), LI.getSyncScopeID());
      NewLI->copyMetadata(LI, {LLVMContext::MD_mem_parallel_loop_access,
                               LLVMContext::MD_access_group});

      V = NewLI;
      IsPtrAdjusted = true;
    }
    V = convertValue(DL, IRB, V, TargetTy);

    if (IsSplit) {
      assert(!LI.isVolatile());
      assert(LI.getType()->isIntegerTy() &&
             "Only integer type loads and stores are split");
      assert(SliceSize < DL.getTypeStoreSize(LI.getType()).getFixedValue() &&
             "Split load isn't smaller than original load");
      assert(DL.typeSizeEqualsStoreSize(LI.getType()) &&
             "Non-byte-multiple bit width");
      // Move the insertion point just past the load so that we can refer to it.
      BasicBlock::iterator LIIt = std::next(LI.getIterator());
      // Ensure the insertion point comes before any debug-info immediately
      // after the load, so that variable values referring to the load are
      // dominated by it.
      LIIt.setHeadBit(true);
      IRB.SetInsertPoint(LI.getParent(), LIIt);
      // Create a placeholder value with the same type as LI to use as the
      // basis for the new value. This allows us to replace the uses of LI with
      // the computed value, and then replace the placeholder with LI, leaving
      // LI only used for this computation.
      Value *Placeholder =
          new LoadInst(LI.getType(), PoisonValue::get(IRB.getPtrTy(AS)), "",
                       false, Align(1));
      V = insertInteger(DL, IRB, Placeholder, V, NewBeginOffset - BeginOffset,
                        "insert");
      LI.replaceAllUsesWith(V);
      Placeholder->replaceAllUsesWith(&LI);
      Placeholder->deleteValue();
    } else {
      LI.replaceAllUsesWith(V);
    }

    Pass.DeadInsts.push_back(&LI);
    deleteIfTriviallyDead(OldOp);
    LLVM_DEBUG(dbgs() << "          to: " << *V << "\n");
    return !LI.isVolatile() && !IsPtrAdjusted;
  }

  bool rewriteVectorizedStoreInst(Value *V, StoreInst &SI, Value *OldOp,
                                  AAMDNodes AATags) {
    // Capture V for the purpose of debug-info accounting once it's converted
    // to a vector store.
    Value *OrigV = V;
    if (V->getType() != VecTy) {
      unsigned BeginIndex = getIndex(NewBeginOffset);
      unsigned EndIndex = getIndex(NewEndOffset);
      assert(EndIndex > BeginIndex && "Empty vector!");
      unsigned NumElements = EndIndex - BeginIndex;
      assert(NumElements <= cast<FixedVectorType>(VecTy)->getNumElements() &&
             "Too many elements!");
      Type *SliceTy = (NumElements == 1)
                          ? ElementTy
                          : FixedVectorType::get(ElementTy, NumElements);
      if (V->getType() != SliceTy)
        V = convertValue(DL, IRB, V, SliceTy);

      // Mix in the existing elements.
      Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                         NewAI.getAlign(), "load");
      V = insertVector(IRB, Old, V, BeginIndex, "vec");
    }
    StoreInst *Store = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlign());
    Store->copyMetadata(SI, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      Store->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                  V->getType(), DL));
    Pass.DeadInsts.push_back(&SI);

    // NOTE: Careful to use OrigV rather than V.
    migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &SI,
                     Store, Store->getPointerOperand(), OrigV, DL);
    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return true;
  }

  bool rewriteIntegerStore(Value *V, StoreInst &SI, AAMDNodes AATags) {
    assert(IntTy && "We cannot extract an integer from the alloca");
    assert(!SI.isVolatile());
    if (DL.getTypeSizeInBits(V->getType()).getFixedValue() !=
        IntTy->getBitWidth()) {
      Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                         NewAI.getAlign(), "oldload");
      Old = convertValue(DL, IRB, Old, IntTy);
      assert(BeginOffset >= NewAllocaBeginOffset && "Out of bounds offset");
      uint64_t Offset = BeginOffset - NewAllocaBeginOffset;
      V = insertInteger(DL, IRB, Old, SI.getValueOperand(), Offset, "insert");
    }
    V = convertValue(DL, IRB, V, NewAllocaTy);
    StoreInst *Store = IRB.CreateAlignedStore(V, &NewAI, NewAI.getAlign());
    Store->copyMetadata(SI, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      Store->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                  V->getType(), DL));

    migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &SI,
                     Store, Store->getPointerOperand(),
                     Store->getValueOperand(), DL);

    Pass.DeadInsts.push_back(&SI);
    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return true;
  }

  bool visitStoreInst(StoreInst &SI) {
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    Value *OldOp = SI.getOperand(1);
    assert(OldOp == OldPtr);

    AAMDNodes AATags = SI.getAAMetadata();
    Value *V = SI.getValueOperand();

    // Strip all inbounds GEPs and pointer casts to try to dig out any root
    // alloca that should be re-examined after promoting this alloca.
    if (V->getType()->isPointerTy())
      if (AllocaInst *AI = dyn_cast<AllocaInst>(V->stripInBoundsOffsets()))
        Pass.PostPromotionWorklist.insert(AI);

    if (SliceSize < DL.getTypeStoreSize(V->getType()).getFixedValue()) {
      assert(!SI.isVolatile());
      assert(V->getType()->isIntegerTy() &&
             "Only integer type loads and stores are split");
      assert(DL.typeSizeEqualsStoreSize(V->getType()) &&
             "Non-byte-multiple bit width");
      IntegerType *NarrowTy = Type::getIntNTy(SI.getContext(), SliceSize * 8);
      V = extractInteger(DL, IRB, V, NarrowTy, NewBeginOffset - BeginOffset,
                         "extract");
    }

    if (VecTy)
      return rewriteVectorizedStoreInst(V, SI, OldOp, AATags);
    if (IntTy && V->getType()->isIntegerTy())
      return rewriteIntegerStore(V, SI, AATags);

    StoreInst *NewSI;
    if (NewBeginOffset == NewAllocaBeginOffset &&
        NewEndOffset == NewAllocaEndOffset &&
        canConvertValue(DL, V->getType(), NewAllocaTy)) {
      V = convertValue(DL, IRB, V, NewAllocaTy);
      Value *NewPtr =
          getPtrToNewAI(SI.getPointerAddressSpace(), SI.isVolatile());

      NewSI =
          IRB.CreateAlignedStore(V, NewPtr, NewAI.getAlign(), SI.isVolatile());
    } else {
      unsigned AS = SI.getPointerAddressSpace();
      Value *NewPtr = getNewAllocaSlicePtr(IRB, IRB.getPtrTy(AS));
      NewSI =
          IRB.CreateAlignedStore(V, NewPtr, getSliceAlign(), SI.isVolatile());
    }
    NewSI->copyMetadata(SI, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      NewSI->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                  V->getType(), DL));
    if (SI.isVolatile())
      NewSI->setAtomic(SI.getOrdering(), SI.getSyncScopeID());
    if (NewSI->isAtomic())
      NewSI->setAlignment(SI.getAlign());

    migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &SI,
                     NewSI, NewSI->getPointerOperand(),
                     NewSI->getValueOperand(), DL);

    Pass.DeadInsts.push_back(&SI);
    deleteIfTriviallyDead(OldOp);

    LLVM_DEBUG(dbgs() << "          to: " << *NewSI << "\n");
    return NewSI->getPointerOperand() == &NewAI &&
           NewSI->getValueOperand()->getType() == NewAllocaTy &&
           !SI.isVolatile();
  }

  /// Compute an integer value from splatting an i8 across the given
  /// number of bytes.
  ///
  /// Note that this routine assumes an i8 is a byte. If that isn't true, don't
  /// call this routine.
  /// FIXME: Heed the advice above.
  ///
  /// \param V The i8 value to splat.
  /// \param Size The number of bytes in the output (assuming i8 is one byte)
  Value *getIntegerSplat(Value *V, unsigned Size) {
    assert(Size > 0 && "Expected a positive number of bytes.");
    IntegerType *VTy = cast<IntegerType>(V->getType());
    assert(VTy->getBitWidth() == 8 && "Expected an i8 value for the byte");
    if (Size == 1)
      return V;

    Type *SplatIntTy = Type::getIntNTy(VTy->getContext(), Size * 8);
    V = IRB.CreateMul(
        IRB.CreateZExt(V, SplatIntTy, "zext"),
        IRB.CreateUDiv(Constant::getAllOnesValue(SplatIntTy),
                       IRB.CreateZExt(Constant::getAllOnesValue(V->getType()),
                                      SplatIntTy)),
        "isplat");
    return V;
  }

  /// Compute a vector splat for a given element value.
  Value *getVectorSplat(Value *V, unsigned NumElements) {
    V = IRB.CreateVectorSplat(NumElements, V, "vsplat");
    LLVM_DEBUG(dbgs() << "       splat: " << *V << "\n");
    return V;
  }

  bool visitMemSetInst(MemSetInst &II) {
    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");
    assert(II.getRawDest() == OldPtr);

    AAMDNodes AATags = II.getAAMetadata();

    // If the memset has a variable size, it cannot be split, just adjust the
    // pointer to the new alloca.
    if (!isa<ConstantInt>(II.getLength())) {
      assert(!IsSplit);
      assert(NewBeginOffset == BeginOffset);
      II.setDest(getNewAllocaSlicePtr(IRB, OldPtr->getType()));
      II.setDestAlignment(getSliceAlign());
      // In theory we should call migrateDebugInfo here. However, we do not
      // emit dbg.assign intrinsics for mem intrinsics storing through non-
      // constant geps, or storing a variable number of bytes.
      assert(at::getAssignmentMarkers(&II).empty() &&
             at::getDVRAssignmentMarkers(&II).empty() &&
             "AT: Unexpected link to non-const GEP");
      deleteIfTriviallyDead(OldPtr);
      return false;
    }

    // Record this instruction for deletion.
    Pass.DeadInsts.push_back(&II);

    Type *AllocaTy = NewAI.getAllocatedType();
    Type *ScalarTy = AllocaTy->getScalarType();

    const bool CanContinue = [&]() {
      if (VecTy || IntTy)
        return true;
      if (BeginOffset > NewAllocaBeginOffset || EndOffset < NewAllocaEndOffset)
        return false;
      // Length must be in range for FixedVectorType.
      auto *C = cast<ConstantInt>(II.getLength());
      const uint64_t Len = C->getLimitedValue();
      if (Len > std::numeric_limits<unsigned>::max())
        return false;
      auto *Int8Ty = IntegerType::getInt8Ty(NewAI.getContext());
      auto *SrcTy = FixedVectorType::get(Int8Ty, Len);
      return canConvertValue(DL, SrcTy, AllocaTy) &&
             DL.isLegalInteger(DL.getTypeSizeInBits(ScalarTy).getFixedValue());
    }();

    // If this doesn't map cleanly onto the alloca type, and that type isn't
    // a single value type, just emit a memset.
    if (!CanContinue) {
      Type *SizeTy = II.getLength()->getType();
      unsigned Sz = NewEndOffset - NewBeginOffset;
      Constant *Size = ConstantInt::get(SizeTy, Sz);
      MemIntrinsic *New = cast<MemIntrinsic>(IRB.CreateMemSet(
          getNewAllocaSlicePtr(IRB, OldPtr->getType()), II.getValue(), Size,
          MaybeAlign(getSliceAlign()), II.isVolatile()));
      if (AATags)
        New->setAAMetadata(
            AATags.adjustForAccess(NewBeginOffset - BeginOffset, Sz));

      migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &II,
                       New, New->getRawDest(), nullptr, DL);

      LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
      return false;
    }

    // If we can represent this as a simple value, we have to build the actual
    // value to store, which requires expanding the byte present in memset to
    // a sensible representation for the alloca type. This is essentially
    // splatting the byte to a sufficiently wide integer, splatting it across
    // any desired vector width, and bitcasting to the final type.
    Value *V;

    if (VecTy) {
      // If this is a memset of a vectorized alloca, insert it.
      assert(ElementTy == ScalarTy);

      unsigned BeginIndex = getIndex(NewBeginOffset);
      unsigned EndIndex = getIndex(NewEndOffset);
      assert(EndIndex > BeginIndex && "Empty vector!");
      unsigned NumElements = EndIndex - BeginIndex;
      assert(NumElements <= cast<FixedVectorType>(VecTy)->getNumElements() &&
             "Too many elements!");

      Value *Splat = getIntegerSplat(
          II.getValue(), DL.getTypeSizeInBits(ElementTy).getFixedValue() / 8);
      Splat = convertValue(DL, IRB, Splat, ElementTy);
      if (NumElements > 1)
        Splat = getVectorSplat(Splat, NumElements);

      Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                         NewAI.getAlign(), "oldload");
      V = insertVector(IRB, Old, Splat, BeginIndex, "vec");
    } else if (IntTy) {
      // If this is a memset on an alloca where we can widen stores, insert the
      // set integer.
      assert(!II.isVolatile());

      uint64_t Size = NewEndOffset - NewBeginOffset;
      V = getIntegerSplat(II.getValue(), Size);

      if (IntTy && (BeginOffset != NewAllocaBeginOffset ||
                    EndOffset != NewAllocaBeginOffset)) {
        Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                           NewAI.getAlign(), "oldload");
        Old = convertValue(DL, IRB, Old, IntTy);
        uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
        V = insertInteger(DL, IRB, Old, V, Offset, "insert");
      } else {
        assert(V->getType() == IntTy &&
               "Wrong type for an alloca wide integer!");
      }
      V = convertValue(DL, IRB, V, AllocaTy);
    } else {
      // Established these invariants above.
      assert(NewBeginOffset == NewAllocaBeginOffset);
      assert(NewEndOffset == NewAllocaEndOffset);

      V = getIntegerSplat(II.getValue(),
                          DL.getTypeSizeInBits(ScalarTy).getFixedValue() / 8);
      if (VectorType *AllocaVecTy = dyn_cast<VectorType>(AllocaTy))
        V = getVectorSplat(
            V, cast<FixedVectorType>(AllocaVecTy)->getNumElements());

      V = convertValue(DL, IRB, V, AllocaTy);
    }

    Value *NewPtr = getPtrToNewAI(II.getDestAddressSpace(), II.isVolatile());
    StoreInst *New =
        IRB.CreateAlignedStore(V, NewPtr, NewAI.getAlign(), II.isVolatile());
    New->copyMetadata(II, {LLVMContext::MD_mem_parallel_loop_access,
                           LLVMContext::MD_access_group});
    if (AATags)
      New->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                V->getType(), DL));

    migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &II,
                     New, New->getPointerOperand(), V, DL);

    LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
    return !II.isVolatile();
  }

  bool visitMemTransferInst(MemTransferInst &II) {
    // Rewriting of memory transfer instructions can be a bit tricky. We break
    // them into two categories: split intrinsics and unsplit intrinsics.

    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");

    AAMDNodes AATags = II.getAAMetadata();

    bool IsDest = &II.getRawDestUse() == OldUse;
    assert((IsDest && II.getRawDest() == OldPtr) ||
           (!IsDest && II.getRawSource() == OldPtr));

    Align SliceAlign = getSliceAlign();
    // For unsplit intrinsics, we simply modify the source and destination
    // pointers in place. This isn't just an optimization, it is a matter of
    // correctness. With unsplit intrinsics we may be dealing with transfers
    // within a single alloca before SROA ran, or with transfers that have
    // a variable length. We may also be dealing with memmove instead of
    // memcpy, and so simply updating the pointers is the necessary for us to
    // update both source and dest of a single call.
    if (!IsSplittable) {
      Value *AdjustedPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
      if (IsDest) {
        // Update the address component of linked dbg.assigns.
        auto UpdateAssignAddress = [&](auto *DbgAssign) {
          if (llvm::is_contained(DbgAssign->location_ops(), II.getDest()) ||
              DbgAssign->getAddress() == II.getDest())
            DbgAssign->replaceVariableLocationOp(II.getDest(), AdjustedPtr);
        };
        for_each(at::getAssignmentMarkers(&II), UpdateAssignAddress);
        for_each(at::getDVRAssignmentMarkers(&II), UpdateAssignAddress);
        II.setDest(AdjustedPtr);
        II.setDestAlignment(SliceAlign);
      } else {
        II.setSource(AdjustedPtr);
        II.setSourceAlignment(SliceAlign);
      }

      LLVM_DEBUG(dbgs() << "          to: " << II << "\n");
      deleteIfTriviallyDead(OldPtr);
      return false;
    }
    // For split transfer intrinsics we have an incredibly useful assurance:
    // the source and destination do not reside within the same alloca, and at
    // least one of them does not escape. This means that we can replace
    // memmove with memcpy, and we don't need to worry about all manner of
    // downsides to splitting and transforming the operations.

    // If this doesn't map cleanly onto the alloca type, and that type isn't
    // a single value type, just emit a memcpy.
    bool EmitMemCpy =
        !VecTy && !IntTy &&
        (BeginOffset > NewAllocaBeginOffset || EndOffset < NewAllocaEndOffset ||
         SliceSize !=
             DL.getTypeStoreSize(NewAI.getAllocatedType()).getFixedValue() ||
         !DL.typeSizeEqualsStoreSize(NewAI.getAllocatedType()) ||
         !NewAI.getAllocatedType()->isSingleValueType());

    // If we're just going to emit a memcpy, the alloca hasn't changed, and the
    // size hasn't been shrunk based on analysis of the viable range, this is
    // a no-op.
    if (EmitMemCpy && &OldAI == &NewAI) {
      // Ensure the start lines up.
      assert(NewBeginOffset == BeginOffset);

      // Rewrite the size as needed.
      if (NewEndOffset != EndOffset)
        II.setLength(ConstantInt::get(II.getLength()->getType(),
                                      NewEndOffset - NewBeginOffset));
      return false;
    }
    // Record this instruction for deletion.
    Pass.DeadInsts.push_back(&II);

    // Strip all inbounds GEPs and pointer casts to try to dig out any root
    // alloca that should be re-examined after rewriting this instruction.
    Value *OtherPtr = IsDest ? II.getRawSource() : II.getRawDest();
    if (AllocaInst *AI =
            dyn_cast<AllocaInst>(OtherPtr->stripInBoundsOffsets())) {
      assert(AI != &OldAI && AI != &NewAI &&
             "Splittable transfers cannot reach the same alloca on both ends.");
      Pass.Worklist.insert(AI);
    }

    Type *OtherPtrTy = OtherPtr->getType();
    unsigned OtherAS = OtherPtrTy->getPointerAddressSpace();

    // Compute the relative offset for the other pointer within the transfer.
    unsigned OffsetWidth = DL.getIndexSizeInBits(OtherAS);
    APInt OtherOffset(OffsetWidth, NewBeginOffset - BeginOffset);
    Align OtherAlign =
        (IsDest ? II.getSourceAlign() : II.getDestAlign()).valueOrOne();
    OtherAlign =
        commonAlignment(OtherAlign, OtherOffset.zextOrTrunc(64).getZExtValue());

    if (EmitMemCpy) {
      // Compute the other pointer, folding as much as possible to produce
      // a single, simple GEP in most cases.
      OtherPtr = getAdjustedPtr(IRB, DL, OtherPtr, OtherOffset, OtherPtrTy,
                                OtherPtr->getName() + ".");

      Value *OurPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
      Type *SizeTy = II.getLength()->getType();
      Constant *Size = ConstantInt::get(SizeTy, NewEndOffset - NewBeginOffset);

      Value *DestPtr, *SrcPtr;
      MaybeAlign DestAlign, SrcAlign;
      // Note: IsDest is true iff we're copying into the new alloca slice
      if (IsDest) {
        DestPtr = OurPtr;
        DestAlign = SliceAlign;
        SrcPtr = OtherPtr;
        SrcAlign = OtherAlign;
      } else {
        DestPtr = OtherPtr;
        DestAlign = OtherAlign;
        SrcPtr = OurPtr;
        SrcAlign = SliceAlign;
      }
      CallInst *New = IRB.CreateMemCpy(DestPtr, DestAlign, SrcPtr, SrcAlign,
                                       Size, II.isVolatile());
      if (AATags)
        New->setAAMetadata(AATags.shift(NewBeginOffset - BeginOffset));

      APInt Offset(DL.getIndexTypeSizeInBits(DestPtr->getType()), 0);
      if (IsDest) {
        migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8,
                         &II, New, DestPtr, nullptr, DL);
      } else if (AllocaInst *Base = dyn_cast<AllocaInst>(
                     DestPtr->stripAndAccumulateConstantOffsets(
                         DL, Offset, /*AllowNonInbounds*/ true))) {
        migrateDebugInfo(Base, IsSplit, Offset.getZExtValue() * 8,
                         SliceSize * 8, &II, New, DestPtr, nullptr, DL);
      }
      LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");
      return false;
    }

    bool IsWholeAlloca = NewBeginOffset == NewAllocaBeginOffset &&
                         NewEndOffset == NewAllocaEndOffset;
    uint64_t Size = NewEndOffset - NewBeginOffset;
    unsigned BeginIndex = VecTy ? getIndex(NewBeginOffset) : 0;
    unsigned EndIndex = VecTy ? getIndex(NewEndOffset) : 0;
    unsigned NumElements = EndIndex - BeginIndex;
    IntegerType *SubIntTy =
        IntTy ? Type::getIntNTy(IntTy->getContext(), Size * 8) : nullptr;

    // Reset the other pointer type to match the register type we're going to
    // use, but using the address space of the original other pointer.
    Type *OtherTy;
    if (VecTy && !IsWholeAlloca) {
      if (NumElements == 1)
        OtherTy = VecTy->getElementType();
      else
        OtherTy = FixedVectorType::get(VecTy->getElementType(), NumElements);
    } else if (IntTy && !IsWholeAlloca) {
      OtherTy = SubIntTy;
    } else {
      OtherTy = NewAllocaTy;
    }

    Value *AdjPtr = getAdjustedPtr(IRB, DL, OtherPtr, OtherOffset, OtherPtrTy,
                                   OtherPtr->getName() + ".");
    MaybeAlign SrcAlign = OtherAlign;
    MaybeAlign DstAlign = SliceAlign;
    if (!IsDest)
      std::swap(SrcAlign, DstAlign);

    Value *SrcPtr;
    Value *DstPtr;

    if (IsDest) {
      DstPtr = getPtrToNewAI(II.getDestAddressSpace(), II.isVolatile());
      SrcPtr = AdjPtr;
    } else {
      DstPtr = AdjPtr;
      SrcPtr = getPtrToNewAI(II.getSourceAddressSpace(), II.isVolatile());
    }

    Value *Src;
    if (VecTy && !IsWholeAlloca && !IsDest) {
      Src = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                  NewAI.getAlign(), "load");
      Src = extractVector(IRB, Src, BeginIndex, EndIndex, "vec");
    } else if (IntTy && !IsWholeAlloca && !IsDest) {
      Src = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                  NewAI.getAlign(), "load");
      Src = convertValue(DL, IRB, Src, IntTy);
      uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
      Src = extractInteger(DL, IRB, Src, SubIntTy, Offset, "extract");
    } else {
      LoadInst *Load = IRB.CreateAlignedLoad(OtherTy, SrcPtr, SrcAlign,
                                             II.isVolatile(), "copyload");
      Load->copyMetadata(II, {LLVMContext::MD_mem_parallel_loop_access,
                              LLVMContext::MD_access_group});
      if (AATags)
        Load->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                   Load->getType(), DL));
      Src = Load;
    }

    if (VecTy && !IsWholeAlloca && IsDest) {
      Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                         NewAI.getAlign(), "oldload");
      Src = insertVector(IRB, Old, Src, BeginIndex, "vec");
    } else if (IntTy && !IsWholeAlloca && IsDest) {
      Value *Old = IRB.CreateAlignedLoad(NewAI.getAllocatedType(), &NewAI,
                                         NewAI.getAlign(), "oldload");
      Old = convertValue(DL, IRB, Old, IntTy);
      uint64_t Offset = NewBeginOffset - NewAllocaBeginOffset;
      Src = insertInteger(DL, IRB, Old, Src, Offset, "insert");
      Src = convertValue(DL, IRB, Src, NewAllocaTy);
    }

    StoreInst *Store = cast<StoreInst>(
        IRB.CreateAlignedStore(Src, DstPtr, DstAlign, II.isVolatile()));
    Store->copyMetadata(II, {LLVMContext::MD_mem_parallel_loop_access,
                             LLVMContext::MD_access_group});
    if (AATags)
      Store->setAAMetadata(AATags.adjustForAccess(NewBeginOffset - BeginOffset,
                                                  Src->getType(), DL));

    APInt Offset(DL.getIndexTypeSizeInBits(DstPtr->getType()), 0);
    if (IsDest) {

      migrateDebugInfo(&OldAI, IsSplit, NewBeginOffset * 8, SliceSize * 8, &II,
                       Store, DstPtr, Src, DL);
    } else if (AllocaInst *Base = dyn_cast<AllocaInst>(
                   DstPtr->stripAndAccumulateConstantOffsets(
                       DL, Offset, /*AllowNonInbounds*/ true))) {
      migrateDebugInfo(Base, IsSplit, Offset.getZExtValue() * 8, SliceSize * 8,
                       &II, Store, DstPtr, Src, DL);
    }

    LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    return !II.isVolatile();
  }

  bool visitIntrinsicInst(IntrinsicInst &II) {
    assert((II.isLifetimeStartOrEnd() || II.isLaunderOrStripInvariantGroup() ||
            II.isDroppable()) &&
           "Unexpected intrinsic!");
    LLVM_DEBUG(dbgs() << "    original: " << II << "\n");

    // Record this instruction for deletion.
    Pass.DeadInsts.push_back(&II);

    if (II.isDroppable()) {
      assert(II.getIntrinsicID() == Intrinsic::assume && "Expected assume");
      // TODO For now we forget assumed information, this can be improved.
      OldPtr->dropDroppableUsesIn(II);
      return true;
    }

    if (II.isLaunderOrStripInvariantGroup())
      return true;

    assert(II.getArgOperand(1) == OldPtr);
    // Lifetime intrinsics are only promotable if they cover the whole alloca.
    // Therefore, we drop lifetime intrinsics which don't cover the whole
    // alloca.
    // (In theory, intrinsics which partially cover an alloca could be
    // promoted, but PromoteMemToReg doesn't handle that case.)
    // FIXME: Check whether the alloca is promotable before dropping the
    // lifetime intrinsics?
    if (NewBeginOffset != NewAllocaBeginOffset ||
        NewEndOffset != NewAllocaEndOffset)
      return true;

    ConstantInt *Size =
        ConstantInt::get(cast<IntegerType>(II.getArgOperand(0)->getType()),
                         NewEndOffset - NewBeginOffset);
    // Lifetime intrinsics always expect an i8* so directly get such a pointer
    // for the new alloca slice.
    Type *PointerTy = IRB.getPtrTy(OldPtr->getType()->getPointerAddressSpace());
    Value *Ptr = getNewAllocaSlicePtr(IRB, PointerTy);
    Value *New;
    if (II.getIntrinsicID() == Intrinsic::lifetime_start)
      New = IRB.CreateLifetimeStart(Ptr, Size);
    else
      New = IRB.CreateLifetimeEnd(Ptr, Size);

    (void)New;
    LLVM_DEBUG(dbgs() << "          to: " << *New << "\n");

    return true;
  }

  void fixLoadStoreAlign(Instruction &Root) {
    // This algorithm implements the same visitor loop as
    // hasUnsafePHIOrSelectUse, and fixes the alignment of each load
    // or store found.
    SmallPtrSet<Instruction *, 4> Visited;
    SmallVector<Instruction *, 4> Uses;
    Visited.insert(&Root);
    Uses.push_back(&Root);
    do {
      Instruction *I = Uses.pop_back_val();

      if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
        LI->setAlignment(std::min(LI->getAlign(), getSliceAlign()));
        continue;
      }
      if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
        SI->setAlignment(std::min(SI->getAlign(), getSliceAlign()));
        continue;
      }

      assert(isa<BitCastInst>(I) || isa<AddrSpaceCastInst>(I) ||
             isa<PHINode>(I) || isa<SelectInst>(I) ||
             isa<GetElementPtrInst>(I));
      for (User *U : I->users())
        if (Visited.insert(cast<Instruction>(U)).second)
          Uses.push_back(cast<Instruction>(U));
    } while (!Uses.empty());
  }

  bool visitPHINode(PHINode &PN) {
    LLVM_DEBUG(dbgs() << "    original: " << PN << "\n");
    assert(BeginOffset >= NewAllocaBeginOffset && "PHIs are unsplittable");
    assert(EndOffset <= NewAllocaEndOffset && "PHIs are unsplittable");

    // We would like to compute a new pointer in only one place, but have it be
    // as local as possible to the PHI. To do that, we re-use the location of
    // the old pointer, which necessarily must be in the right position to
    // dominate the PHI.
    IRBuilderBase::InsertPointGuard Guard(IRB);
    if (isa<PHINode>(OldPtr))
      IRB.SetInsertPoint(OldPtr->getParent(),
                         OldPtr->getParent()->getFirstInsertionPt());
    else
      IRB.SetInsertPoint(OldPtr);
    IRB.SetCurrentDebugLocation(OldPtr->getDebugLoc());

    Value *NewPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
    // Replace the operands which were using the old pointer.
    std::replace(PN.op_begin(), PN.op_end(), cast<Value>(OldPtr), NewPtr);

    LLVM_DEBUG(dbgs() << "          to: " << PN << "\n");
    deleteIfTriviallyDead(OldPtr);

    // Fix the alignment of any loads or stores using this PHI node.
    fixLoadStoreAlign(PN);

    // PHIs can't be promoted on their own, but often can be speculated. We
    // check the speculation outside of the rewriter so that we see the
    // fully-rewritten alloca.
    PHIUsers.insert(&PN);
    return true;
  }

  bool visitSelectInst(SelectInst &SI) {
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    assert((SI.getTrueValue() == OldPtr || SI.getFalseValue() == OldPtr) &&
           "Pointer isn't an operand!");
    assert(BeginOffset >= NewAllocaBeginOffset && "Selects are unsplittable");
    assert(EndOffset <= NewAllocaEndOffset && "Selects are unsplittable");

    Value *NewPtr = getNewAllocaSlicePtr(IRB, OldPtr->getType());
    // Replace the operands which were using the old pointer.
    if (SI.getOperand(1) == OldPtr)
      SI.setOperand(1, NewPtr);
    if (SI.getOperand(2) == OldPtr)
      SI.setOperand(2, NewPtr);

    LLVM_DEBUG(dbgs() << "          to: " << SI << "\n");
    deleteIfTriviallyDead(OldPtr);

    // Fix the alignment of any loads or stores using this select.
    fixLoadStoreAlign(SI);

    // Selects can't be promoted on their own, but often can be speculated. We
    // check the speculation outside of the rewriter so that we see the
    // fully-rewritten alloca.
    SelectUsers.insert(&SI);
    return true;
  }
};

/// Visitor to rewrite aggregate loads and stores as scalar.
///
/// This pass aggressively rewrites all aggregate loads and stores on
/// a particular pointer (or any pointer derived from it which we can identify)
/// with scalar loads and stores.
class AggLoadStoreRewriter : public InstVisitor<AggLoadStoreRewriter, bool> {
  // Befriend the base class so it can delegate to private visit methods.
  friend class InstVisitor<AggLoadStoreRewriter, bool>;

  /// Queue of pointer uses to analyze and potentially rewrite.
  SmallVector<Use *, 8> Queue;

  /// Set to prevent us from cycling with phi nodes and loops.
  SmallPtrSet<User *, 8> Visited;

  /// The current pointer use being rewritten. This is used to dig up the used
  /// value (as opposed to the user).
  Use *U = nullptr;

  /// Used to calculate offsets, and hence alignment, of subobjects.
  const DataLayout &DL;

  IRBuilderTy &IRB;

public:
  AggLoadStoreRewriter(const DataLayout &DL, IRBuilderTy &IRB)
      : DL(DL), IRB(IRB) {}

  /// Rewrite loads and stores through a pointer and all pointers derived from
  /// it.
  bool rewrite(Instruction &I) {
    LLVM_DEBUG(dbgs() << "  Rewriting FCA loads and stores...\n");
    enqueueUsers(I);
    bool Changed = false;
    while (!Queue.empty()) {
      U = Queue.pop_back_val();
      Changed |= visit(cast<Instruction>(U->getUser()));
    }
    return Changed;
  }

private:
  /// Enqueue all the users of the given instruction for further processing.
  /// This uses a set to de-duplicate users.
  void enqueueUsers(Instruction &I) {
    for (Use &U : I.uses())
      if (Visited.insert(U.getUser()).second)
        Queue.push_back(&U);
  }

  // Conservative default is to not rewrite anything.
  bool visitInstruction(Instruction &I) { return false; }

  /// Generic recursive split emission class.
  template <typename Derived> class OpSplitter {
  protected:
    /// The builder used to form new instructions.
    IRBuilderTy &IRB;

    /// The indices which to be used with insert- or extractvalue to select the
    /// appropriate value within the aggregate.
    SmallVector<unsigned, 4> Indices;

    /// The indices to a GEP instruction which will move Ptr to the correct slot
    /// within the aggregate.
    SmallVector<Value *, 4> GEPIndices;

    /// The base pointer of the original op, used as a base for GEPing the
    /// split operations.
    Value *Ptr;

    /// The base pointee type being GEPed into.
    Type *BaseTy;

    /// Known alignment of the base pointer.
    Align BaseAlign;

    /// To calculate offset of each component so we can correctly deduce
    /// alignments.
    const DataLayout &DL;

    /// Initialize the splitter with an insertion point, Ptr and start with a
    /// single zero GEP index.
    OpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
               Align BaseAlign, const DataLayout &DL, IRBuilderTy &IRB)
        : IRB(IRB), GEPIndices(1, IRB.getInt32(0)), Ptr(Ptr), BaseTy(BaseTy),
          BaseAlign(BaseAlign), DL(DL) {
      IRB.SetInsertPoint(InsertionPoint);
    }

  public:
    /// Generic recursive split emission routine.
    ///
    /// This method recursively splits an aggregate op (load or store) into
    /// scalar or vector ops. It splits recursively until it hits a single value
    /// and emits that single value operation via the template argument.
    ///
    /// The logic of this routine relies on GEPs and insertvalue and
    /// extractvalue all operating with the same fundamental index list, merely
    /// formatted differently (GEPs need actual values).
    ///
    /// \param Ty  The type being split recursively into smaller ops.
    /// \param Agg The aggregate value being built up or stored, depending on
    /// whether this is splitting a load or a store respectively.
    void emitSplitOps(Type *Ty, Value *&Agg, const Twine &Name) {
      if (Ty->isSingleValueType()) {
        unsigned Offset = DL.getIndexedOffsetInType(BaseTy, GEPIndices);
        return static_cast<Derived *>(this)->emitFunc(
            Ty, Agg, commonAlignment(BaseAlign, Offset), Name);
      }

      if (ArrayType *ATy = dyn_cast<ArrayType>(Ty)) {
        unsigned OldSize = Indices.size();
        (void)OldSize;
        for (unsigned Idx = 0, Size = ATy->getNumElements(); Idx != Size;
             ++Idx) {
          assert(Indices.size() == OldSize && "Did not return to the old size");
          Indices.push_back(Idx);
          GEPIndices.push_back(IRB.getInt32(Idx));
          emitSplitOps(ATy->getElementType(), Agg, Name + "." + Twine(Idx));
          GEPIndices.pop_back();
          Indices.pop_back();
        }
        return;
      }

      if (StructType *STy = dyn_cast<StructType>(Ty)) {
        unsigned OldSize = Indices.size();
        (void)OldSize;
        for (unsigned Idx = 0, Size = STy->getNumElements(); Idx != Size;
             ++Idx) {
          assert(Indices.size() == OldSize && "Did not return to the old size");
          Indices.push_back(Idx);
          GEPIndices.push_back(IRB.getInt32(Idx));
          emitSplitOps(STy->getElementType(Idx), Agg, Name + "." + Twine(Idx));
          GEPIndices.pop_back();
          Indices.pop_back();
        }
        return;
      }

      llvm_unreachable("Only arrays and structs are aggregate loadable types");
    }
  };

  struct LoadOpSplitter : public OpSplitter<LoadOpSplitter> {
    AAMDNodes AATags;

    LoadOpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
                   AAMDNodes AATags, Align BaseAlign, const DataLayout &DL,
                   IRBuilderTy &IRB)
        : OpSplitter<LoadOpSplitter>(InsertionPoint, Ptr, BaseTy, BaseAlign, DL,
                                     IRB),
          AATags(AATags) {}

    /// Emit a leaf load of a single value. This is called at the leaves of the
    /// recursive emission to actually load values.
    void emitFunc(Type *Ty, Value *&Agg, Align Alignment, const Twine &Name) {
      assert(Ty->isSingleValueType());
      // Load the single value and insert it using the indices.
      Value *GEP =
          IRB.CreateInBoundsGEP(BaseTy, Ptr, GEPIndices, Name + ".gep");
      LoadInst *Load =
          IRB.CreateAlignedLoad(Ty, GEP, Alignment, Name + ".load");

      APInt Offset(
          DL.getIndexSizeInBits(Ptr->getType()->getPointerAddressSpace()), 0);
      if (AATags &&
          GEPOperator::accumulateConstantOffset(BaseTy, GEPIndices, DL, Offset))
        Load->setAAMetadata(
            AATags.adjustForAccess(Offset.getZExtValue(), Load->getType(), DL));

      Agg = IRB.CreateInsertValue(Agg, Load, Indices, Name + ".insert");
      LLVM_DEBUG(dbgs() << "          to: " << *Load << "\n");
    }
  };

  bool visitLoadInst(LoadInst &LI) {
    assert(LI.getPointerOperand() == *U);
    if (!LI.isSimple() || LI.getType()->isSingleValueType())
      return false;

    // We have an aggregate being loaded, split it apart.
    LLVM_DEBUG(dbgs() << "    original: " << LI << "\n");
    LoadOpSplitter Splitter(&LI, *U, LI.getType(), LI.getAAMetadata(),
                            getAdjustedAlignment(&LI, 0), DL, IRB);
    Value *V = PoisonValue::get(LI.getType());
    Splitter.emitSplitOps(LI.getType(), V, LI.getName() + ".fca");
    Visited.erase(&LI);
    LI.replaceAllUsesWith(V);
    LI.eraseFromParent();
    return true;
  }

  struct StoreOpSplitter : public OpSplitter<StoreOpSplitter> {
    StoreOpSplitter(Instruction *InsertionPoint, Value *Ptr, Type *BaseTy,
                    AAMDNodes AATags, StoreInst *AggStore, Align BaseAlign,
                    const DataLayout &DL, IRBuilderTy &IRB)
        : OpSplitter<StoreOpSplitter>(InsertionPoint, Ptr, BaseTy, BaseAlign,
                                      DL, IRB),
          AATags(AATags), AggStore(AggStore) {}
    AAMDNodes AATags;
    StoreInst *AggStore;
    /// Emit a leaf store of a single value. This is called at the leaves of the
    /// recursive emission to actually produce stores.
    void emitFunc(Type *Ty, Value *&Agg, Align Alignment, const Twine &Name) {
      assert(Ty->isSingleValueType());
      // Extract the single value and store it using the indices.
      //
      // The gep and extractvalue values are factored out of the CreateStore
      // call to make the output independent of the argument evaluation order.
      Value *ExtractValue =
          IRB.CreateExtractValue(Agg, Indices, Name + ".extract");
      Value *InBoundsGEP =
          IRB.CreateInBoundsGEP(BaseTy, Ptr, GEPIndices, Name + ".gep");
      StoreInst *Store =
          IRB.CreateAlignedStore(ExtractValue, InBoundsGEP, Alignment);

      APInt Offset(
          DL.getIndexSizeInBits(Ptr->getType()->getPointerAddressSpace()), 0);
      GEPOperator::accumulateConstantOffset(BaseTy, GEPIndices, DL, Offset);
      if (AATags) {
        Store->setAAMetadata(AATags.adjustForAccess(
            Offset.getZExtValue(), ExtractValue->getType(), DL));
      }

      // migrateDebugInfo requires the base Alloca. Walk to it from this gep.
      // If we cannot (because there's an intervening non-const or unbounded
      // gep) then we wouldn't expect to see dbg.assign intrinsics linked to
      // this instruction.
      Value *Base = AggStore->getPointerOperand()->stripInBoundsOffsets();
      if (auto *OldAI = dyn_cast<AllocaInst>(Base)) {
        uint64_t SizeInBits =
            DL.getTypeSizeInBits(Store->getValueOperand()->getType());
        migrateDebugInfo(OldAI, /*IsSplit*/ true, Offset.getZExtValue() * 8,
                         SizeInBits, AggStore, Store,
                         Store->getPointerOperand(), Store->getValueOperand(),
                         DL);
      } else {
        assert(at::getAssignmentMarkers(Store).empty() &&
               at::getDVRAssignmentMarkers(Store).empty() &&
               "AT: unexpected debug.assign linked to store through "
               "unbounded GEP");
      }
      LLVM_DEBUG(dbgs() << "          to: " << *Store << "\n");
    }
  };

  bool visitStoreInst(StoreInst &SI) {
    if (!SI.isSimple() || SI.getPointerOperand() != *U)
      return false;
    Value *V = SI.getValueOperand();
    if (V->getType()->isSingleValueType())
      return false;

    // We have an aggregate being stored, split it apart.
    LLVM_DEBUG(dbgs() << "    original: " << SI << "\n");
    StoreOpSplitter Splitter(&SI, *U, V->getType(), SI.getAAMetadata(), &SI,
                             getAdjustedAlignment(&SI, 0), DL, IRB);
    Splitter.emitSplitOps(V->getType(), V, V->getName() + ".fca");
    Visited.erase(&SI);
    // The stores replacing SI each have markers describing fragments of the
    // assignment so delete the assignment markers linked to SI.
    at::deleteAssignmentMarkers(&SI);
    SI.eraseFromParent();
    return true;
  }

  bool visitBitCastInst(BitCastInst &BC) {
    enqueueUsers(BC);
    return false;
  }

  bool visitAddrSpaceCastInst(AddrSpaceCastInst &ASC) {
    enqueueUsers(ASC);
    return false;
  }

  // Unfold gep (select cond, ptr1, ptr2), idx
  //   => select cond, gep(ptr1, idx), gep(ptr2, idx)
  // and  gep ptr, (select cond, idx1, idx2)
  //   => select cond, gep(ptr, idx1), gep(ptr, idx2)
  bool unfoldGEPSelect(GetElementPtrInst &GEPI) {
    // Check whether the GEP has exactly one select operand and all indices
    // will become constant after the transform.
    SelectInst *Sel = dyn_cast<SelectInst>(GEPI.getPointerOperand());
    for (Value *Op : GEPI.indices()) {
      if (auto *SI = dyn_cast<SelectInst>(Op)) {
        if (Sel)
          return false;

        Sel = SI;
        if (!isa<ConstantInt>(Sel->getTrueValue()) ||
            !isa<ConstantInt>(Sel->getFalseValue()))
          return false;
        continue;
      }

      if (!isa<ConstantInt>(Op))
        return false;
    }

    if (!Sel)
      return false;

    LLVM_DEBUG(dbgs() << "  Rewriting gep(select) -> select(gep):\n";
               dbgs() << "    original: " << *Sel << "\n";
               dbgs() << "              " << GEPI << "\n";);

    auto GetNewOps = [&](Value *SelOp) {
      SmallVector<Value *> NewOps;
      for (Value *Op : GEPI.operands())
        if (Op == Sel)
          NewOps.push_back(SelOp);
        else
          NewOps.push_back(Op);
      return NewOps;
    };

    Value *True = Sel->getTrueValue();
    Value *False = Sel->getFalseValue();
    SmallVector<Value *> TrueOps = GetNewOps(True);
    SmallVector<Value *> FalseOps = GetNewOps(False);

    IRB.SetInsertPoint(&GEPI);
    GEPNoWrapFlags NW = GEPI.getNoWrapFlags();

    Type *Ty = GEPI.getSourceElementType();
    Value *NTrue = IRB.CreateGEP(Ty, TrueOps[0], ArrayRef(TrueOps).drop_front(),
                                 True->getName() + ".sroa.gep", NW);

    Value *NFalse =
        IRB.CreateGEP(Ty, FalseOps[0], ArrayRef(FalseOps).drop_front(),
                      False->getName() + ".sroa.gep", NW);

    Value *NSel = IRB.CreateSelect(Sel->getCondition(), NTrue, NFalse,
                                   Sel->getName() + ".sroa.sel");
    Visited.erase(&GEPI);
    GEPI.replaceAllUsesWith(NSel);
    GEPI.eraseFromParent();
    Instruction *NSelI = cast<Instruction>(NSel);
    Visited.insert(NSelI);
    enqueueUsers(*NSelI);

    LLVM_DEBUG(dbgs() << "          to: " << *NTrue << "\n";
               dbgs() << "              " << *NFalse << "\n";
               dbgs() << "              " << *NSel << "\n";);

    return true;
  }

  // Unfold gep (phi ptr1, ptr2), idx
  //   => phi ((gep ptr1, idx), (gep ptr2, idx))
  // and  gep ptr, (phi idx1, idx2)
  //   => phi ((gep ptr, idx1), (gep ptr, idx2))
  bool unfoldGEPPhi(GetElementPtrInst &GEPI) {
    // To prevent infinitely expanding recursive phis, bail if the GEP pointer
    // operand (looking through the phi if it is the phi we want to unfold) is
    // an instruction besides a static alloca.
    PHINode *Phi = dyn_cast<PHINode>(GEPI.getPointerOperand());
    auto IsInvalidPointerOperand = [](Value *V) {
      if (!isa<Instruction>(V))
        return false;
      if (auto *AI = dyn_cast<AllocaInst>(V))
        return !AI->isStaticAlloca();
      return true;
    };
    if (Phi) {
      if (any_of(Phi->operands(), IsInvalidPointerOperand))
        return false;
    } else {
      if (IsInvalidPointerOperand(GEPI.getPointerOperand()))
        return false;
    }
    // Check whether the GEP has exactly one phi operand (including the pointer
    // operand) and all indices will become constant after the transform.
    for (Value *Op : GEPI.indices()) {
      if (auto *SI = dyn_cast<PHINode>(Op)) {
        if (Phi)
          return false;

        Phi = SI;
        if (!all_of(Phi->incoming_values(),
                    [](Value *V) { return isa<ConstantInt>(V); }))
          return false;
        continue;
      }

      if (!isa<ConstantInt>(Op))
        return false;
    }

    if (!Phi)
      return false;

    LLVM_DEBUG(dbgs() << "  Rewriting gep(phi) -> phi(gep):\n";
               dbgs() << "    original: " << *Phi << "\n";
               dbgs() << "              " << GEPI << "\n";);

    auto GetNewOps = [&](Value *PhiOp) {
      SmallVector<Value *> NewOps;
      for (Value *Op : GEPI.operands())
        if (Op == Phi)
          NewOps.push_back(PhiOp);
        else
          NewOps.push_back(Op);
      return NewOps;
    };

    IRB.SetInsertPoint(Phi);
    PHINode *NewPhi = IRB.CreatePHI(GEPI.getType(), Phi->getNumIncomingValues(),
                                    Phi->getName() + ".sroa.phi");

    Type *SourceTy = GEPI.getSourceElementType();
    // We only handle arguments, constants, and static allocas here, so we can
    // insert GEPs at the end of the entry block.
    IRB.SetInsertPoint(GEPI.getFunction()->getEntryBlock().getTerminator());
    for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I) {
      Value *Op = Phi->getIncomingValue(I);
      BasicBlock *BB = Phi->getIncomingBlock(I);
      Value *NewGEP;
      if (int NI = NewPhi->getBasicBlockIndex(BB); NI >= 0) {
        NewGEP = NewPhi->getIncomingValue(NI);
      } else {
        SmallVector<Value *> NewOps = GetNewOps(Op);
        NewGEP =
            IRB.CreateGEP(SourceTy, NewOps[0], ArrayRef(NewOps).drop_front(),
                          Phi->getName() + ".sroa.gep", GEPI.getNoWrapFlags());
      }
      NewPhi->addIncoming(NewGEP, BB);
    }

    Visited.erase(&GEPI);
    GEPI.replaceAllUsesWith(NewPhi);
    GEPI.eraseFromParent();
    Visited.insert(NewPhi);
    enqueueUsers(*NewPhi);

    LLVM_DEBUG(dbgs() << "          to: ";
               for (Value *In
                    : NewPhi->incoming_values()) dbgs()
               << "\n              " << *In;
               dbgs() << "\n              " << *NewPhi << '\n');

    return true;
  }

  bool visitGetElementPtrInst(GetElementPtrInst &GEPI) {
    if (unfoldGEPSelect(GEPI))
      return true;

    if (unfoldGEPPhi(GEPI))
      return true;

    enqueueUsers(GEPI);
    return false;
  }

  bool visitPHINode(PHINode &PN) {
    enqueueUsers(PN);
    return false;
  }

  bool visitSelectInst(SelectInst &SI) {
    enqueueUsers(SI);
    return false;
  }
};

} // end anonymous namespace

/// Strip aggregate type wrapping.
///
/// This removes no-op aggregate types wrapping an underlying type. It will
/// strip as many layers of types as it can without changing either the type
/// size or the allocated size.
static Type *stripAggregateTypeWrapping(const DataLayout &DL, Type *Ty) {
  if (Ty->isSingleValueType())
    return Ty;

  uint64_t AllocSize = DL.getTypeAllocSize(Ty).getFixedValue();
  uint64_t TypeSize = DL.getTypeSizeInBits(Ty).getFixedValue();

  Type *InnerTy;
  if (ArrayType *ArrTy = dyn_cast<ArrayType>(Ty)) {
    InnerTy = ArrTy->getElementType();
  } else if (StructType *STy = dyn_cast<StructType>(Ty)) {
    const StructLayout *SL = DL.getStructLayout(STy);
    unsigned Index = SL->getElementContainingOffset(0);
    InnerTy = STy->getElementType(Index);
  } else {
    return Ty;
  }

  if (AllocSize > DL.getTypeAllocSize(InnerTy).getFixedValue() ||
      TypeSize > DL.getTypeSizeInBits(InnerTy).getFixedValue())
    return Ty;

  return stripAggregateTypeWrapping(DL, InnerTy);
}

/// Try to find a partition of the aggregate type passed in for a given
/// offset and size.
///
/// This recurses through the aggregate type and tries to compute a subtype
/// based on the offset and size. When the offset and size span a sub-section
/// of an array, it will even compute a new array type for that sub-section,
/// and the same for structs.
///
/// Note that this routine is very strict and tries to find a partition of the
/// type which produces the *exact* right offset and size. It is not forgiving
/// when the size or offset cause either end of type-based partition to be off.
/// Also, this is a best-effort routine. It is reasonable to give up and not
/// return a type if necessary.
static Type *getTypePartition(const DataLayout &DL, Type *Ty, uint64_t Offset,
                              uint64_t Size) {
  if (Offset == 0 && DL.getTypeAllocSize(Ty).getFixedValue() == Size)
    return stripAggregateTypeWrapping(DL, Ty);
  if (Offset > DL.getTypeAllocSize(Ty).getFixedValue() ||
      (DL.getTypeAllocSize(Ty).getFixedValue() - Offset) < Size)
    return nullptr;

  if (isa<ArrayType>(Ty) || isa<VectorType>(Ty)) {
    Type *ElementTy;
    uint64_t TyNumElements;
    if (auto *AT = dyn_cast<ArrayType>(Ty)) {
      ElementTy = AT->getElementType();
      TyNumElements = AT->getNumElements();
    } else {
      // FIXME: This isn't right for vectors with non-byte-sized or
      // non-power-of-two sized elements.
      auto *VT = cast<FixedVectorType>(Ty);
      ElementTy = VT->getElementType();
      TyNumElements = VT->getNumElements();
    }
    uint64_t ElementSize = DL.getTypeAllocSize(ElementTy).getFixedValue();
    uint64_t NumSkippedElements = Offset / ElementSize;
    if (NumSkippedElements >= TyNumElements)
      return nullptr;
    Offset -= NumSkippedElements * ElementSize;

    // First check if we need to recurse.
    if (Offset > 0 || Size < ElementSize) {
      // Bail if the partition ends in a different array element.
      if ((Offset + Size) > ElementSize)
        return nullptr;
      // Recurse through the element type trying to peel off offset bytes.
      return getTypePartition(DL, ElementTy, Offset, Size);
    }
    assert(Offset == 0);

    if (Size == ElementSize)
      return stripAggregateTypeWrapping(DL, ElementTy);
    assert(Size > ElementSize);
    uint64_t NumElements = Size / ElementSize;
    if (NumElements * ElementSize != Size)
      return nullptr;
    return ArrayType::get(ElementTy, NumElements);
  }

  StructType *STy = dyn_cast<StructType>(Ty);
  if (!STy)
    return nullptr;

  const StructLayout *SL = DL.getStructLayout(STy);

  if (SL->getSizeInBits().isScalable())
    return nullptr;

  if (Offset >= SL->getSizeInBytes())
    return nullptr;
  uint64_t EndOffset = Offset + Size;
  if (EndOffset > SL->getSizeInBytes())
    return nullptr;

  unsigned Index = SL->getElementContainingOffset(Offset);
  Offset -= SL->getElementOffset(Index);

  Type *ElementTy = STy->getElementType(Index);
  uint64_t ElementSize = DL.getTypeAllocSize(ElementTy).getFixedValue();
  if (Offset >= ElementSize)
    return nullptr; // The offset points into alignment padding.

  // See if any partition must be contained by the element.
  if (Offset > 0 || Size < ElementSize) {
    if ((Offset + Size) > ElementSize)
      return nullptr;
    return getTypePartition(DL, ElementTy, Offset, Size);
  }
  assert(Offset == 0);

  if (Size == ElementSize)
    return stripAggregateTypeWrapping(DL, ElementTy);

  StructType::element_iterator EI = STy->element_begin() + Index,
                               EE = STy->element_end();
  if (EndOffset < SL->getSizeInBytes()) {
    unsigned EndIndex = SL->getElementContainingOffset(EndOffset);
    if (Index == EndIndex)
      return nullptr; // Within a single element and its padding.

    // Don't try to form "natural" types if the elements don't line up with the
    // expected size.
    // FIXME: We could potentially recurse down through the last element in the
    // sub-struct to find a natural end point.
    if (SL->getElementOffset(EndIndex) != EndOffset)
      return nullptr;

    assert(Index < EndIndex);
    EE = STy->element_begin() + EndIndex;
  }

  // Try to build up a sub-structure.
  StructType *SubTy =
      StructType::get(STy->getContext(), ArrayRef(EI, EE), STy->isPacked());
  const StructLayout *SubSL = DL.getStructLayout(SubTy);
  if (Size != SubSL->getSizeInBytes())
    return nullptr; // The sub-struct doesn't have quite the size needed.

  return SubTy;
}

/// Pre-split loads and stores to simplify rewriting.
///
/// We want to break up the splittable load+store pairs as much as
/// possible. This is important to do as a preprocessing step, as once we
/// start rewriting the accesses to partitions of the alloca we lose the
/// necessary information to correctly split apart paired loads and stores
/// which both point into this alloca. The case to consider is something like
/// the following:
///
///   %a = alloca [12 x i8]
///   %gep1 = getelementptr i8, ptr %a, i32 0
///   %gep2 = getelementptr i8, ptr %a, i32 4
///   %gep3 = getelementptr i8, ptr %a, i32 8
///   store float 0.0, ptr %gep1
///   store float 1.0, ptr %gep2
///   %v = load i64, ptr %gep1
///   store i64 %v, ptr %gep2
///   %f1 = load float, ptr %gep2
///   %f2 = load float, ptr %gep3
///
/// Here we want to form 3 partitions of the alloca, each 4 bytes large, and
/// promote everything so we recover the 2 SSA values that should have been
/// there all along.
///
/// \returns true if any changes are made.
bool SROA::presplitLoadsAndStores(AllocaInst &AI, AllocaSlices &AS) {
  LLVM_DEBUG(dbgs() << "Pre-splitting loads and stores\n");

  // Track the loads and stores which are candidates for pre-splitting here, in
  // the order they first appear during the partition scan. These give stable
  // iteration order and a basis for tracking which loads and stores we
  // actually split.
  SmallVector<LoadInst *, 4> Loads;
  SmallVector<StoreInst *, 4> Stores;

  // We need to accumulate the splits required of each load or store where we
  // can find them via a direct lookup. This is important to cross-check loads
  // and stores against each other. We also track the slice so that we can kill
  // all the slices that end up split.
  struct SplitOffsets {
    Slice *S;
    std::vector<uint64_t> Splits;
  };
  SmallDenseMap<Instruction *, SplitOffsets, 8> SplitOffsetsMap;

  // Track loads out of this alloca which cannot, for any reason, be pre-split.
  // This is important as we also cannot pre-split stores of those loads!
  // FIXME: This is all pretty gross. It means that we can be more aggressive
  // in pre-splitting when the load feeding the store happens to come from
  // a separate alloca. Put another way, the effectiveness of SROA would be
  // decreased by a frontend which just concatenated all of its local allocas
  // into one big flat alloca. But defeating such patterns is exactly the job
  // SROA is tasked with! Sadly, to not have this discrepancy we would have
  // change store pre-splitting to actually force pre-splitting of the load
  // that feeds it *and all stores*. That makes pre-splitting much harder, but
  // maybe it would make it more principled?
  SmallPtrSet<LoadInst *, 8> UnsplittableLoads;

  LLVM_DEBUG(dbgs() << "  Searching for candidate loads and stores\n");
  for (auto &P : AS.partitions()) {
    for (Slice &S : P) {
      Instruction *I = cast<Instruction>(S.getUse()->getUser());
      if (!S.isSplittable() || S.endOffset() <= P.endOffset()) {
        // If this is a load we have to track that it can't participate in any
        // pre-splitting. If this is a store of a load we have to track that
        // that load also can't participate in any pre-splitting.
        if (auto *LI = dyn_cast<LoadInst>(I))
          UnsplittableLoads.insert(LI);
        else if (auto *SI = dyn_cast<StoreInst>(I))
          if (auto *LI = dyn_cast<LoadInst>(SI->getValueOperand()))
            UnsplittableLoads.insert(LI);
        continue;
      }
      assert(P.endOffset() > S.beginOffset() &&
             "Empty or backwards partition!");

      // Determine if this is a pre-splittable slice.
      if (auto *LI = dyn_cast<LoadInst>(I)) {
        assert(!LI->isVolatile() && "Cannot split volatile loads!");

        // The load must be used exclusively to store into other pointers for
        // us to be able to arbitrarily pre-split it. The stores must also be
        // simple to avoid changing semantics.
        auto IsLoadSimplyStored = [](LoadInst *LI) {
          for (User *LU : LI->users()) {
            auto *SI = dyn_cast<StoreInst>(LU);
            if (!SI || !SI->isSimple())
              return false;
          }
          return true;
        };
        if (!IsLoadSimplyStored(LI)) {
          UnsplittableLoads.insert(LI);
          continue;
        }

        Loads.push_back(LI);
      } else if (auto *SI = dyn_cast<StoreInst>(I)) {
        if (S.getUse() != &SI->getOperandUse(SI->getPointerOperandIndex()))
          // Skip stores *of* pointers. FIXME: This shouldn't even be possible!
          continue;
        auto *StoredLoad = dyn_cast<LoadInst>(SI->getValueOperand());
        if (!StoredLoad || !StoredLoad->isSimple())
          continue;
        assert(!SI->isVolatile() && "Cannot split volatile stores!");

        Stores.push_back(SI);
      } else {
        // Other uses cannot be pre-split.
        continue;
      }

      // Record the initial split.
      LLVM_DEBUG(dbgs() << "    Candidate: " << *I << "\n");
      auto &Offsets = SplitOffsetsMap[I];
      assert(Offsets.Splits.empty() &&
             "Should not have splits the first time we see an instruction!");
      Offsets.S = &S;
      Offsets.Splits.push_back(P.endOffset() - S.beginOffset());
    }

    // Now scan the already split slices, and add a split for any of them which
    // we're going to pre-split.
    for (Slice *S : P.splitSliceTails()) {
      auto SplitOffsetsMapI =
          SplitOffsetsMap.find(cast<Instruction>(S->getUse()->getUser()));
      if (SplitOffsetsMapI == SplitOffsetsMap.end())
        continue;
      auto &Offsets = SplitOffsetsMapI->second;

      assert(Offsets.S == S && "Found a mismatched slice!");
      assert(!Offsets.Splits.empty() &&
             "Cannot have an empty set of splits on the second partition!");
      assert(Offsets.Splits.back() ==
                 P.beginOffset() - Offsets.S->beginOffset() &&
             "Previous split does not end where this one begins!");

      // Record each split. The last partition's end isn't needed as the size
      // of the slice dictates that.
      if (S->endOffset() > P.endOffset())
        Offsets.Splits.push_back(P.endOffset() - Offsets.S->beginOffset());
    }
  }

  // We may have split loads where some of their stores are split stores. For
  // such loads and stores, we can only pre-split them if their splits exactly
  // match relative to their starting offset. We have to verify this prior to
  // any rewriting.
  llvm::erase_if(Stores, [&UnsplittableLoads, &SplitOffsetsMap](StoreInst *SI) {
    // Lookup the load we are storing in our map of split
    // offsets.
    auto *LI = cast<LoadInst>(SI->getValueOperand());
    // If it was completely unsplittable, then we're done,
    // and this store can't be pre-split.
    if (UnsplittableLoads.count(LI))
      return true;

    auto LoadOffsetsI = SplitOffsetsMap.find(LI);
    if (LoadOffsetsI == SplitOffsetsMap.end())
      return false; // Unrelated loads are definitely safe.
    auto &LoadOffsets = LoadOffsetsI->second;

    // Now lookup the store's offsets.
    auto &StoreOffsets = SplitOffsetsMap[SI];

    // If the relative offsets of each split in the load and
    // store match exactly, then we can split them and we
    // don't need to remove them here.
    if (LoadOffsets.Splits == StoreOffsets.Splits)
      return false;

    LLVM_DEBUG(dbgs() << "    Mismatched splits for load and store:\n"
                      << "      " << *LI << "\n"
                      << "      " << *SI << "\n");

    // We've found a store and load that we need to split
    // with mismatched relative splits. Just give up on them
    // and remove both instructions from our list of
    // candidates.
    UnsplittableLoads.insert(LI);
    return true;
  });
  // Now we have to go *back* through all the stores, because a later store may
  // have caused an earlier store's load to become unsplittable and if it is
  // unsplittable for the later store, then we can't rely on it being split in
  // the earlier store either.
  llvm::erase_if(Stores, [&UnsplittableLoads](StoreInst *SI) {
    auto *LI = cast<LoadInst>(SI->getValueOperand());
    return UnsplittableLoads.count(LI);
  });
  // Once we've established all the loads that can't be split for some reason,
  // filter any that made it into our list out.
  llvm::erase_if(Loads, [&UnsplittableLoads](LoadInst *LI) {
    return UnsplittableLoads.count(LI);
  });

  // If no loads or stores are left, there is no pre-splitting to be done for
  // this alloca.
  if (Loads.empty() && Stores.empty())
    return false;

  // From here on, we can't fail and will be building new accesses, so rig up
  // an IR builder.
  IRBuilderTy IRB(&AI);

  // Collect the new slices which we will merge into the alloca slices.
  SmallVector<Slice, 4> NewSlices;

  // Track any allocas we end up splitting loads and stores for so we iterate
  // on them.
  SmallPtrSet<AllocaInst *, 4> ResplitPromotableAllocas;

  // At this point, we have collected all of the loads and stores we can
  // pre-split, and the specific splits needed for them. We actually do the
  // splitting in a specific order in order to handle when one of the loads in
  // the value operand to one of the stores.
  //
  // First, we rewrite all of the split loads, and just accumulate each split
  // load in a parallel structure. We also build the slices for them and append
  // them to the alloca slices.
  SmallDenseMap<LoadInst *, std::vector<LoadInst *>, 1> SplitLoadsMap;
  std::vector<LoadInst *> SplitLoads;
  const DataLayout &DL = AI.getDataLayout();
  for (LoadInst *LI : Loads) {
    SplitLoads.clear();

    auto &Offsets = SplitOffsetsMap[LI];
    unsigned SliceSize = Offsets.S->endOffset() - Offsets.S->beginOffset();
    assert(LI->getType()->getIntegerBitWidth() % 8 == 0 &&
           "Load must have type size equal to store size");
    assert(LI->getType()->getIntegerBitWidth() / 8 >= SliceSize &&
           "Load must be >= slice size");

    uint64_t BaseOffset = Offsets.S->beginOffset();
    assert(BaseOffset + SliceSize > BaseOffset &&
           "Cannot represent alloca access size using 64-bit integers!");

    Instruction *BasePtr = cast<Instruction>(LI->getPointerOperand());
    IRB.SetInsertPoint(LI);

    LLVM_DEBUG(dbgs() << "  Splitting load: " << *LI << "\n");

    uint64_t PartOffset = 0, PartSize = Offsets.Splits.front();
    int Idx = 0, Size = Offsets.Splits.size();
    for (;;) {
      auto *PartTy = Type::getIntNTy(LI->getContext(), PartSize * 8);
      auto AS = LI->getPointerAddressSpace();
      auto *PartPtrTy = LI->getPointerOperandType();
      LoadInst *PLoad = IRB.CreateAlignedLoad(
          PartTy,
          getAdjustedPtr(IRB, DL, BasePtr,
                         APInt(DL.getIndexSizeInBits(AS), PartOffset),
                         PartPtrTy, BasePtr->getName() + "."),
          getAdjustedAlignment(LI, PartOffset),
          /*IsVolatile*/ false, LI->getName());
      PLoad->copyMetadata(*LI, {LLVMContext::MD_mem_parallel_loop_access,
                                LLVMContext::MD_access_group});

      // Append this load onto the list of split loads so we can find it later
      // to rewrite the stores.
      SplitLoads.push_back(PLoad);

      // Now build a new slice for the alloca.
      NewSlices.push_back(
          Slice(BaseOffset + PartOffset, BaseOffset + PartOffset + PartSize,
                &PLoad->getOperandUse(PLoad->getPointerOperandIndex()),
                /*IsSplittable*/ false));
      LLVM_DEBUG(dbgs() << "    new slice [" << NewSlices.back().beginOffset()
                        << ", " << NewSlices.back().endOffset()
                        << "): " << *PLoad << "\n");

      // See if we've handled all the splits.
      if (Idx >= Size)
        break;

      // Setup the next partition.
      PartOffset = Offsets.Splits[Idx];
      ++Idx;
      PartSize = (Idx < Size ? Offsets.Splits[Idx] : SliceSize) - PartOffset;
    }

    // Now that we have the split loads, do the slow walk over all uses of the
    // load and rewrite them as split stores, or save the split loads to use
    // below if the store is going to be split there anyways.
    bool DeferredStores = false;
    for (User *LU : LI->users()) {
      StoreInst *SI = cast<StoreInst>(LU);
      if (!Stores.empty() && SplitOffsetsMap.count(SI)) {
        DeferredStores = true;
        LLVM_DEBUG(dbgs() << "    Deferred splitting of store: " << *SI
                          << "\n");
        continue;
      }

      Value *StoreBasePtr = SI->getPointerOperand();
      IRB.SetInsertPoint(SI);
      AAMDNodes AATags = SI->getAAMetadata();

      LLVM_DEBUG(dbgs() << "    Splitting store of load: " << *SI << "\n");

      for (int Idx = 0, Size = SplitLoads.size(); Idx < Size; ++Idx) {
        LoadInst *PLoad = SplitLoads[Idx];
        uint64_t PartOffset = Idx == 0 ? 0 : Offsets.Splits[Idx - 1];
        auto *PartPtrTy = SI->getPointerOperandType();

        auto AS = SI->getPointerAddressSpace();
        StoreInst *PStore = IRB.CreateAlignedStore(
            PLoad,
            getAdjustedPtr(IRB, DL, StoreBasePtr,
                           APInt(DL.getIndexSizeInBits(AS), PartOffset),
                           PartPtrTy, StoreBasePtr->getName() + "."),
            getAdjustedAlignment(SI, PartOffset),
            /*IsVolatile*/ false);
        PStore->copyMetadata(*SI, {LLVMContext::MD_mem_parallel_loop_access,
                                   LLVMContext::MD_access_group,
                                   LLVMContext::MD_DIAssignID});

        if (AATags)
          PStore->setAAMetadata(
              AATags.adjustForAccess(PartOffset, PLoad->getType(), DL));
        LLVM_DEBUG(dbgs() << "      +" << PartOffset << ":" << *PStore << "\n");
      }

      // We want to immediately iterate on any allocas impacted by splitting
      // this store, and we have to track any promotable alloca (indicated by
      // a direct store) as needing to be resplit because it is no longer
      // promotable.
      if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(StoreBasePtr)) {
        ResplitPromotableAllocas.insert(OtherAI);
        Worklist.insert(OtherAI);
      } else if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(
                     StoreBasePtr->stripInBoundsOffsets())) {
        Worklist.insert(OtherAI);
      }

      // Mark the original store as dead.
      DeadInsts.push_back(SI);
    }

    // Save the split loads if there are deferred stores among the users.
    if (DeferredStores)
      SplitLoadsMap.insert(std::make_pair(LI, std::move(SplitLoads)));

    // Mark the original load as dead and kill the original slice.
    DeadInsts.push_back(LI);
    Offsets.S->kill();
  }

  // Second, we rewrite all of the split stores. At this point, we know that
  // all loads from this alloca have been split already. For stores of such
  // loads, we can simply look up the pre-existing split loads. For stores of
  // other loads, we split those loads first and then write split stores of
  // them.
  for (StoreInst *SI : Stores) {
    auto *LI = cast<LoadInst>(SI->getValueOperand());
    IntegerType *Ty = cast<IntegerType>(LI->getType());
    assert(Ty->getBitWidth() % 8 == 0);
    uint64_t StoreSize = Ty->getBitWidth() / 8;
    assert(StoreSize > 0 && "Cannot have a zero-sized integer store!");

    auto &Offsets = SplitOffsetsMap[SI];
    assert(StoreSize == Offsets.S->endOffset() - Offsets.S->beginOffset() &&
           "Slice size should always match load size exactly!");
    uint64_t BaseOffset = Offsets.S->beginOffset();
    assert(BaseOffset + StoreSize > BaseOffset &&
           "Cannot represent alloca access size using 64-bit integers!");

    Value *LoadBasePtr = LI->getPointerOperand();
    Instruction *StoreBasePtr = cast<Instruction>(SI->getPointerOperand());

    LLVM_DEBUG(dbgs() << "  Splitting store: " << *SI << "\n");

    // Check whether we have an already split load.
    auto SplitLoadsMapI = SplitLoadsMap.find(LI);
    std::vector<LoadInst *> *SplitLoads = nullptr;
    if (SplitLoadsMapI != SplitLoadsMap.end()) {
      SplitLoads = &SplitLoadsMapI->second;
      assert(SplitLoads->size() == Offsets.Splits.size() + 1 &&
             "Too few split loads for the number of splits in the store!");
    } else {
      LLVM_DEBUG(dbgs() << "          of load: " << *LI << "\n");
    }

    uint64_t PartOffset = 0, PartSize = Offsets.Splits.front();
    int Idx = 0, Size = Offsets.Splits.size();
    for (;;) {
      auto *PartTy = Type::getIntNTy(Ty->getContext(), PartSize * 8);
      auto *LoadPartPtrTy = LI->getPointerOperandType();
      auto *StorePartPtrTy = SI->getPointerOperandType();

      // Either lookup a split load or create one.
      LoadInst *PLoad;
      if (SplitLoads) {
        PLoad = (*SplitLoads)[Idx];
      } else {
        IRB.SetInsertPoint(LI);
        auto AS = LI->getPointerAddressSpace();
        PLoad = IRB.CreateAlignedLoad(
            PartTy,
            getAdjustedPtr(IRB, DL, LoadBasePtr,
                           APInt(DL.getIndexSizeInBits(AS), PartOffset),
                           LoadPartPtrTy, LoadBasePtr->getName() + "."),
            getAdjustedAlignment(LI, PartOffset),
            /*IsVolatile*/ false, LI->getName());
        PLoad->copyMetadata(*LI, {LLVMContext::MD_mem_parallel_loop_access,
                                  LLVMContext::MD_access_group});
      }

      // And store this partition.
      IRB.SetInsertPoint(SI);
      auto AS = SI->getPointerAddressSpace();
      StoreInst *PStore = IRB.CreateAlignedStore(
          PLoad,
          getAdjustedPtr(IRB, DL, StoreBasePtr,
                         APInt(DL.getIndexSizeInBits(AS), PartOffset),
                         StorePartPtrTy, StoreBasePtr->getName() + "."),
          getAdjustedAlignment(SI, PartOffset),
          /*IsVolatile*/ false);
      PStore->copyMetadata(*SI, {LLVMContext::MD_mem_parallel_loop_access,
                                 LLVMContext::MD_access_group});

      // Now build a new slice for the alloca.
      NewSlices.push_back(
          Slice(BaseOffset + PartOffset, BaseOffset + PartOffset + PartSize,
                &PStore->getOperandUse(PStore->getPointerOperandIndex()),
                /*IsSplittable*/ false));
      LLVM_DEBUG(dbgs() << "    new slice [" << NewSlices.back().beginOffset()
                        << ", " << NewSlices.back().endOffset()
                        << "): " << *PStore << "\n");
      if (!SplitLoads) {
        LLVM_DEBUG(dbgs() << "      of split load: " << *PLoad << "\n");
      }

      // See if we've finished all the splits.
      if (Idx >= Size)
        break;

      // Setup the next partition.
      PartOffset = Offsets.Splits[Idx];
      ++Idx;
      PartSize = (Idx < Size ? Offsets.Splits[Idx] : StoreSize) - PartOffset;
    }

    // We want to immediately iterate on any allocas impacted by splitting
    // this load, which is only relevant if it isn't a load of this alloca and
    // thus we didn't already split the loads above. We also have to keep track
    // of any promotable allocas we split loads on as they can no longer be
    // promoted.
    if (!SplitLoads) {
      if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(LoadBasePtr)) {
        assert(OtherAI != &AI && "We can't re-split our own alloca!");
        ResplitPromotableAllocas.insert(OtherAI);
        Worklist.insert(OtherAI);
      } else if (AllocaInst *OtherAI = dyn_cast<AllocaInst>(
                     LoadBasePtr->stripInBoundsOffsets())) {
        assert(OtherAI != &AI && "We can't re-split our own alloca!");
        Worklist.insert(OtherAI);
      }
    }

    // Mark the original store as dead now that we've split it up and kill its
    // slice. Note that we leave the original load in place unless this store
    // was its only use. It may in turn be split up if it is an alloca load
    // for some other alloca, but it may be a normal load. This may introduce
    // redundant loads, but where those can be merged the rest of the optimizer
    // should handle the merging, and this uncovers SSA splits which is more
    // important. In practice, the original loads will almost always be fully
    // split and removed eventually, and the splits will be merged by any
    // trivial CSE, including instcombine.
    if (LI->hasOneUse()) {
      assert(*LI->user_begin() == SI && "Single use isn't this store!");
      DeadInsts.push_back(LI);
    }
    DeadInsts.push_back(SI);
    Offsets.S->kill();
  }

  // Remove the killed slices that have ben pre-split.
  llvm::erase_if(AS, [](const Slice &S) { return S.isDead(); });

  // Insert our new slices. This will sort and merge them into the sorted
  // sequence.
  AS.insert(NewSlices);

  LLVM_DEBUG(dbgs() << "  Pre-split slices:\n");
#ifndef NDEBUG
  for (auto I = AS.begin(), E = AS.end(); I != E; ++I)
    LLVM_DEBUG(AS.print(dbgs(), I, "    "));
#endif

  // Finally, don't try to promote any allocas that new require re-splitting.
  // They have already been added to the worklist above.
  llvm::erase_if(PromotableAllocas, [&](AllocaInst *AI) {
    return ResplitPromotableAllocas.count(AI);
  });

  return true;
}

/// Rewrite an alloca partition's users.
///
/// This routine drives both of the rewriting goals of the SROA pass. It tries
/// to rewrite uses of an alloca partition to be conducive for SSA value
/// promotion. If the partition needs a new, more refined alloca, this will
/// build that new alloca, preserving as much type information as possible, and
/// rewrite the uses of the old alloca to point at the new one and have the
/// appropriate new offsets. It also evaluates how successful the rewrite was
/// at enabling promotion and if it was successful queues the alloca to be
/// promoted.
AllocaInst *SROA::rewritePartition(AllocaInst &AI, AllocaSlices &AS,
                                   Partition &P) {
  // Try to compute a friendly type for this partition of the alloca. This
  // won't always succeed, in which case we fall back to a legal integer type
  // or an i8 array of an appropriate size.
  Type *SliceTy = nullptr;
  VectorType *SliceVecTy = nullptr;
  const DataLayout &DL = AI.getDataLayout();
  std::pair<Type *, IntegerType *> CommonUseTy =
      findCommonType(P.begin(), P.end(), P.endOffset());
  // Do all uses operate on the same type?
  if (CommonUseTy.first)
    if (DL.getTypeAllocSize(CommonUseTy.first).getFixedValue() >= P.size()) {
      SliceTy = CommonUseTy.first;
      SliceVecTy = dyn_cast<VectorType>(SliceTy);
    }
  // If not, can we find an appropriate subtype in the original allocated type?
  if (!SliceTy)
    if (Type *TypePartitionTy = getTypePartition(DL, AI.getAllocatedType(),
                                                 P.beginOffset(), P.size()))
      SliceTy = TypePartitionTy;

  // If still not, can we use the largest bitwidth integer type used?
  if (!SliceTy && CommonUseTy.second)
    if (DL.getTypeAllocSize(CommonUseTy.second).getFixedValue() >= P.size()) {
      SliceTy = CommonUseTy.second;
      SliceVecTy = dyn_cast<VectorType>(SliceTy);
    }
  if ((!SliceTy || (SliceTy->isArrayTy() &&
                    SliceTy->getArrayElementType()->isIntegerTy())) &&
      DL.isLegalInteger(P.size() * 8)) {
    SliceTy = Type::getIntNTy(*C, P.size() * 8);
  }

  // If the common use types are not viable for promotion then attempt to find
  // another type that is viable.
  if (SliceVecTy && !checkVectorTypeForPromotion(P, SliceVecTy, DL))
    if (Type *TypePartitionTy = getTypePartition(DL, AI.getAllocatedType(),
                                                 P.beginOffset(), P.size())) {
      VectorType *TypePartitionVecTy = dyn_cast<VectorType>(TypePartitionTy);
      if (TypePartitionVecTy &&
          checkVectorTypeForPromotion(P, TypePartitionVecTy, DL))
        SliceTy = TypePartitionTy;
    }

  if (!SliceTy)
    SliceTy = ArrayType::get(Type::getInt8Ty(*C), P.size());
  assert(DL.getTypeAllocSize(SliceTy).getFixedValue() >= P.size());

  bool IsIntegerPromotable = isIntegerWideningViable(P, SliceTy, DL);

  VectorType *VecTy =
      IsIntegerPromotable ? nullptr : isVectorPromotionViable(P, DL);
  if (VecTy)
    SliceTy = VecTy;

  // Check for the case where we're going to rewrite to a new alloca of the
  // exact same type as the original, and with the same access offsets. In that
  // case, re-use the existing alloca, but still run through the rewriter to
  // perform phi and select speculation.
  // P.beginOffset() can be non-zero even with the same type in a case with
  // out-of-bounds access (e.g. @PR35657 function in SROA/basictest.ll).
  AllocaInst *NewAI;
  if (SliceTy == AI.getAllocatedType() && P.beginOffset() == 0) {
    NewAI = &AI;
    // FIXME: We should be able to bail at this point with "nothing changed".
    // FIXME: We might want to defer PHI speculation until after here.
    // FIXME: return nullptr;
  } else {
    // Make sure the alignment is compatible with P.beginOffset().
    const Align Alignment = commonAlignment(AI.getAlign(), P.beginOffset());
    // If we will get at least this much alignment from the type alone, leave
    // the alloca's alignment unconstrained.
    const bool IsUnconstrained = Alignment <= DL.getABITypeAlign(SliceTy);
    NewAI = new AllocaInst(
        SliceTy, AI.getAddressSpace(), nullptr,
        IsUnconstrained ? DL.getPrefTypeAlign(SliceTy) : Alignment,
        AI.getName() + ".sroa." + Twine(P.begin() - AS.begin()),
        AI.getIterator());
    // Copy the old AI debug location over to the new one.
    NewAI->setDebugLoc(AI.getDebugLoc());
    ++NumNewAllocas;
  }

  LLVM_DEBUG(dbgs() << "Rewriting alloca partition " << "[" << P.beginOffset()
                    << "," << P.endOffset() << ") to: " << *NewAI << "\n");

  // Track the high watermark on the worklist as it is only relevant for
  // promoted allocas. We will reset it to this point if the alloca is not in
  // fact scheduled for promotion.
  unsigned PPWOldSize = PostPromotionWorklist.size();
  unsigned NumUses = 0;
  SmallSetVector<PHINode *, 8> PHIUsers;
  SmallSetVector<SelectInst *, 8> SelectUsers;

  AllocaSliceRewriter Rewriter(DL, AS, *this, AI, *NewAI, P.beginOffset(),
                               P.endOffset(), IsIntegerPromotable, VecTy,
                               PHIUsers, SelectUsers);
  bool Promotable = true;
  for (Slice *S : P.splitSliceTails()) {
    Promotable &= Rewriter.visit(S);
    ++NumUses;
  }
  for (Slice &S : P) {
    Promotable &= Rewriter.visit(&S);
    ++NumUses;
  }

  NumAllocaPartitionUses += NumUses;
  MaxUsesPerAllocaPartition.updateMax(NumUses);

  // Now that we've processed all the slices in the new partition, check if any
  // PHIs or Selects would block promotion.
  for (PHINode *PHI : PHIUsers)
    if (!isSafePHIToSpeculate(*PHI)) {
      Promotable = false;
      PHIUsers.clear();
      SelectUsers.clear();
      break;
    }

  SmallVector<std::pair<SelectInst *, RewriteableMemOps>, 2>
      NewSelectsToRewrite;
  NewSelectsToRewrite.reserve(SelectUsers.size());
  for (SelectInst *Sel : SelectUsers) {
    std::optional<RewriteableMemOps> Ops =
        isSafeSelectToSpeculate(*Sel, PreserveCFG);
    if (!Ops) {
      Promotable = false;
      PHIUsers.clear();
      SelectUsers.clear();
      NewSelectsToRewrite.clear();
      break;
    }
    NewSelectsToRewrite.emplace_back(std::make_pair(Sel, *Ops));
  }

  if (Promotable) {
    for (Use *U : AS.getDeadUsesIfPromotable()) {
      auto *OldInst = dyn_cast<Instruction>(U->get());
      Value::dropDroppableUse(*U);
      if (OldInst)
        if (isInstructionTriviallyDead(OldInst))
          DeadInsts.push_back(OldInst);
    }
    if (PHIUsers.empty() && SelectUsers.empty()) {
      // Promote the alloca.
      PromotableAllocas.push_back(NewAI);
    } else {
      // If we have either PHIs or Selects to speculate, add them to those
      // worklists and re-queue the new alloca so that we promote in on the
      // next iteration.
      for (PHINode *PHIUser : PHIUsers)
        SpeculatablePHIs.insert(PHIUser);
      SelectsToRewrite.reserve(SelectsToRewrite.size() +
                               NewSelectsToRewrite.size());
      for (auto &&KV : llvm::make_range(
               std::make_move_iterator(NewSelectsToRewrite.begin()),
               std::make_move_iterator(NewSelectsToRewrite.end())))
        SelectsToRewrite.insert(std::move(KV));
      Worklist.insert(NewAI);
    }
  } else {
    // Drop any post-promotion work items if promotion didn't happen.
    while (PostPromotionWorklist.size() > PPWOldSize)
      PostPromotionWorklist.pop_back();

    // We couldn't promote and we didn't create a new partition, nothing
    // happened.
    if (NewAI == &AI)
      return nullptr;

    // If we can't promote the alloca, iterate on it to check for new
    // refinements exposed by splitting the current alloca. Don't iterate on an
    // alloca which didn't actually change and didn't get promoted.
    Worklist.insert(NewAI);
  }

  return NewAI;
}

// There isn't a shared interface to get the "address" parts out of a
// dbg.declare and dbg.assign, so provide some wrappers now for
// both debug intrinsics and records.
const Value *getAddress(const DbgVariableIntrinsic *DVI) {
  if (const auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI))
    return DAI->getAddress();
  return cast<DbgDeclareInst>(DVI)->getAddress();
}

const Value *getAddress(const DbgVariableRecord *DVR) {
  assert(DVR->getType() == DbgVariableRecord::LocationType::Declare ||
         DVR->getType() == DbgVariableRecord::LocationType::Assign);
  return DVR->getAddress();
}

bool isKillAddress(const DbgVariableIntrinsic *DVI) {
  if (const auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI))
    return DAI->isKillAddress();
  return cast<DbgDeclareInst>(DVI)->isKillLocation();
}

bool isKillAddress(const DbgVariableRecord *DVR) {
  assert(DVR->getType() == DbgVariableRecord::LocationType::Declare ||
         DVR->getType() == DbgVariableRecord::LocationType::Assign);
  if (DVR->getType() == DbgVariableRecord::LocationType::Assign)
    return DVR->isKillAddress();
  return DVR->isKillLocation();
}

const DIExpression *getAddressExpression(const DbgVariableIntrinsic *DVI) {
  if (const auto *DAI = dyn_cast<DbgAssignIntrinsic>(DVI))
    return DAI->getAddressExpression();
  return cast<DbgDeclareInst>(DVI)->getExpression();
}

const DIExpression *getAddressExpression(const DbgVariableRecord *DVR) {
  assert(DVR->getType() == DbgVariableRecord::LocationType::Declare ||
         DVR->getType() == DbgVariableRecord::LocationType::Assign);
  if (DVR->getType() == DbgVariableRecord::LocationType::Assign)
    return DVR->getAddressExpression();
  return DVR->getExpression();
}

/// Create or replace an existing fragment in a DIExpression with \p Frag.
/// If the expression already contains a DW_OP_LLVM_extract_bits_[sz]ext
/// operation, add \p BitExtractOffset to the offset part.
///
/// Returns the new expression, or nullptr if this fails (see details below).
///
/// This function is similar to DIExpression::createFragmentExpression except
/// for 3 important distinctions:
///   1. The new fragment isn't relative to an existing fragment.
///   2. It assumes the computed location is a memory location. This means we
///      don't need to perform checks that creating the fragment preserves the
///      expression semantics.
///   3. Existing extract_bits are modified independently of fragment changes
///      using \p BitExtractOffset. A change to the fragment offset or size
///      may affect a bit extract. But a bit extract offset can change
///      independently of the fragment dimensions.
///
/// Returns the new expression, or nullptr if one couldn't be created.
/// Ideally this is only used to signal that a bit-extract has become
/// zero-sized (and thus the new debug record has no size and can be
/// dropped), however, it fails for other reasons too - see the FIXME below.
///
/// FIXME: To keep the change that introduces this function NFC it bails
/// in some situations unecessarily, e.g. when fragment and bit extract
/// sizes differ.
static DIExpression *createOrReplaceFragment(const DIExpression *Expr,
                                             DIExpression::FragmentInfo Frag,
                                             int64_t BitExtractOffset) {
  SmallVector<uint64_t, 8> Ops;
  bool HasFragment = false;
  bool HasBitExtract = false;

  for (auto &Op : Expr->expr_ops()) {
    if (Op.getOp() == dwarf::DW_OP_LLVM_fragment) {
      HasFragment = true;
      continue;
    }
    if (Op.getOp() == dwarf::DW_OP_LLVM_extract_bits_zext ||
        Op.getOp() == dwarf::DW_OP_LLVM_extract_bits_sext) {
      HasBitExtract = true;
      int64_t ExtractOffsetInBits = Op.getArg(0);
      int64_t ExtractSizeInBits = Op.getArg(1);

      // DIExpression::createFragmentExpression doesn't know how to handle
      // a fragment that is smaller than the extract. Copy the behaviour
      // (bail) to avoid non-NFC changes.
      // FIXME: Don't do this.
      if (Frag.SizeInBits < uint64_t(ExtractSizeInBits))
        return nullptr;

      assert(BitExtractOffset <= 0);
      int64_t AdjustedOffset = ExtractOffsetInBits + BitExtractOffset;

      // DIExpression::createFragmentExpression doesn't know what to do
      // if the new extract starts "outside" the existing one. Copy the
      // behaviour (bail) to avoid non-NFC changes.
      // FIXME: Don't do this.
      if (AdjustedOffset < 0)
        return nullptr;

      Ops.push_back(Op.getOp());
      Ops.push_back(std::max<int64_t>(0, AdjustedOffset));
      Ops.push_back(ExtractSizeInBits);
      continue;
    }
    Op.appendToVector(Ops);
  }

  // Unsupported by createFragmentExpression, so don't support it here yet to
  // preserve NFC-ness.
  if (HasFragment && HasBitExtract)
    return nullptr;

  if (!HasBitExtract) {
    Ops.push_back(dwarf::DW_OP_LLVM_fragment);
    Ops.push_back(Frag.OffsetInBits);
    Ops.push_back(Frag.SizeInBits);
  }
  return DIExpression::get(Expr->getContext(), Ops);
}

/// Insert a new dbg.declare.
/// \p Orig Original to copy debug loc and variable from.
/// \p NewAddr Location's new base address.
/// \p NewAddrExpr New expression to apply to address.
/// \p BeforeInst Insert position.
/// \p NewFragment New fragment (absolute, non-relative).
/// \p BitExtractAdjustment Offset to apply to any extract_bits op.
static void
insertNewDbgInst(DIBuilder &DIB, DbgDeclareInst *Orig, AllocaInst *NewAddr,
                 DIExpression *NewAddrExpr, Instruction *BeforeInst,
                 std::optional<DIExpression::FragmentInfo> NewFragment,
                 int64_t BitExtractAdjustment) {
  if (NewFragment)
    NewAddrExpr = createOrReplaceFragment(NewAddrExpr, *NewFragment,
                                          BitExtractAdjustment);
  if (!NewAddrExpr)
    return;

  DIB.insertDeclare(NewAddr, Orig->getVariable(), NewAddrExpr,
                    Orig->getDebugLoc(), BeforeInst);
}

/// Insert a new dbg.assign.
/// \p Orig Original to copy debug loc, variable, value and value expression
///    from.
/// \p NewAddr Location's new base address.
/// \p NewAddrExpr New expression to apply to address.
/// \p BeforeInst Insert position.
/// \p NewFragment New fragment (absolute, non-relative).
/// \p BitExtractAdjustment Offset to apply to any extract_bits op.
static void
insertNewDbgInst(DIBuilder &DIB, DbgAssignIntrinsic *Orig, AllocaInst *NewAddr,
                 DIExpression *NewAddrExpr, Instruction *BeforeInst,
                 std::optional<DIExpression::FragmentInfo> NewFragment,
                 int64_t BitExtractAdjustment) {
  // DIBuilder::insertDbgAssign will insert the #dbg_assign after NewAddr.
  (void)BeforeInst;

  // A dbg.assign puts fragment info in the value expression only. The address
  // expression has already been built: NewAddrExpr.
  DIExpression *NewFragmentExpr = Orig->getExpression();
  if (NewFragment)
    NewFragmentExpr = createOrReplaceFragment(NewFragmentExpr, *NewFragment,
                                              BitExtractAdjustment);
  if (!NewFragmentExpr)
    return;

  // Apply a DIAssignID to the store if it doesn't already have it.
  if (!NewAddr->hasMetadata(LLVMContext::MD_DIAssignID)) {
    NewAddr->setMetadata(LLVMContext::MD_DIAssignID,
                         DIAssignID::getDistinct(NewAddr->getContext()));
  }

  Instruction *NewAssign =
      DIB.insertDbgAssign(NewAddr, Orig->getValue(), Orig->getVariable(),
                          NewFragmentExpr, NewAddr, NewAddrExpr,
                          Orig->getDebugLoc())
          .get<Instruction *>();
  LLVM_DEBUG(dbgs() << "Created new assign intrinsic: " << *NewAssign << "\n");
  (void)NewAssign;
}

/// Insert a new DbgRecord.
/// \p Orig Original to copy record type, debug loc and variable from, and
///    additionally value and value expression for dbg_assign records.
/// \p NewAddr Location's new base address.
/// \p NewAddrExpr New expression to apply to address.
/// \p BeforeInst Insert position.
/// \p NewFragment New fragment (absolute, non-relative).
/// \p BitExtractAdjustment Offset to apply to any extract_bits op.
static void
insertNewDbgInst(DIBuilder &DIB, DbgVariableRecord *Orig, AllocaInst *NewAddr,
                 DIExpression *NewAddrExpr, Instruction *BeforeInst,
                 std::optional<DIExpression::FragmentInfo> NewFragment,
                 int64_t BitExtractAdjustment) {
  (void)DIB;

  // A dbg_assign puts fragment info in the value expression only. The address
  // expression has already been built: NewAddrExpr. A dbg_declare puts the
  // new fragment info into NewAddrExpr (as it only has one expression).
  DIExpression *NewFragmentExpr =
      Orig->isDbgAssign() ? Orig->getExpression() : NewAddrExpr;
  if (NewFragment)
    NewFragmentExpr = createOrReplaceFragment(NewFragmentExpr, *NewFragment,
                                              BitExtractAdjustment);
  if (!NewFragmentExpr)
    return;

  if (Orig->isDbgDeclare()) {
    DbgVariableRecord *DVR = DbgVariableRecord::createDVRDeclare(
        NewAddr, Orig->getVariable(), NewFragmentExpr, Orig->getDebugLoc());
    BeforeInst->getParent()->insertDbgRecordBefore(DVR,
                                                   BeforeInst->getIterator());
    return;
  }

  // Apply a DIAssignID to the store if it doesn't already have it.
  if (!NewAddr->hasMetadata(LLVMContext::MD_DIAssignID)) {
    NewAddr->setMetadata(LLVMContext::MD_DIAssignID,
                         DIAssignID::getDistinct(NewAddr->getContext()));
  }

  DbgVariableRecord *NewAssign = DbgVariableRecord::createLinkedDVRAssign(
      NewAddr, Orig->getValue(), Orig->getVariable(), NewFragmentExpr, NewAddr,
      NewAddrExpr, Orig->getDebugLoc());
  LLVM_DEBUG(dbgs() << "Created new DVRAssign: " << *NewAssign << "\n");
  (void)NewAssign;
}

/// Walks the slices of an alloca and form partitions based on them,
/// rewriting each of their uses.
bool SROA::splitAlloca(AllocaInst &AI, AllocaSlices &AS) {
  if (AS.begin() == AS.end())
    return false;

  unsigned NumPartitions = 0;
  bool Changed = false;
  const DataLayout &DL = AI.getModule()->getDataLayout();

  // First try to pre-split loads and stores.
  Changed |= presplitLoadsAndStores(AI, AS);

  // Now that we have identified any pre-splitting opportunities,
  // mark loads and stores unsplittable except for the following case.
  // We leave a slice splittable if all other slices are disjoint or fully
  // included in the slice, such as whole-alloca loads and stores.
  // If we fail to split these during pre-splitting, we want to force them
  // to be rewritten into a partition.
  bool IsSorted = true;

  uint64_t AllocaSize =
      DL.getTypeAllocSize(AI.getAllocatedType()).getFixedValue();
  const uint64_t MaxBitVectorSize = 1024;
  if (AllocaSize <= MaxBitVectorSize) {
    // If a byte boundary is included in any load or store, a slice starting or
    // ending at the boundary is not splittable.
    SmallBitVector SplittableOffset(AllocaSize + 1, true);
    for (Slice &S : AS)
      for (unsigned O = S.beginOffset() + 1;
           O < S.endOffset() && O < AllocaSize; O++)
        SplittableOffset.reset(O);

    for (Slice &S : AS) {
      if (!S.isSplittable())
        continue;

      if ((S.beginOffset() > AllocaSize || SplittableOffset[S.beginOffset()]) &&
          (S.endOffset() > AllocaSize || SplittableOffset[S.endOffset()]))
        continue;

      if (isa<LoadInst>(S.getUse()->getUser()) ||
          isa<StoreInst>(S.getUse()->getUser())) {
        S.makeUnsplittable();
        IsSorted = false;
      }
    }
  } else {
    // We only allow whole-alloca splittable loads and stores
    // for a large alloca to avoid creating too large BitVector.
    for (Slice &S : AS) {
      if (!S.isSplittable())
        continue;

      if (S.beginOffset() == 0 && S.endOffset() >= AllocaSize)
        continue;

      if (isa<LoadInst>(S.getUse()->getUser()) ||
          isa<StoreInst>(S.getUse()->getUser())) {
        S.makeUnsplittable();
        IsSorted = false;
      }
    }
  }

  if (!IsSorted)
    llvm::stable_sort(AS);

  /// Describes the allocas introduced by rewritePartition in order to migrate
  /// the debug info.
  struct Fragment {
    AllocaInst *Alloca;
    uint64_t Offset;
    uint64_t Size;
    Fragment(AllocaInst *AI, uint64_t O, uint64_t S)
        : Alloca(AI), Offset(O), Size(S) {}
  };
  SmallVector<Fragment, 4> Fragments;

  // Rewrite each partition.
  for (auto &P : AS.partitions()) {
    if (AllocaInst *NewAI = rewritePartition(AI, AS, P)) {
      Changed = true;
      if (NewAI != &AI) {
        uint64_t SizeOfByte = 8;
        uint64_t AllocaSize =
            DL.getTypeSizeInBits(NewAI->getAllocatedType()).getFixedValue();
        // Don't include any padding.
        uint64_t Size = std::min(AllocaSize, P.size() * SizeOfByte);
        Fragments.push_back(
            Fragment(NewAI, P.beginOffset() * SizeOfByte, Size));
      }
    }
    ++NumPartitions;
  }

  NumAllocaPartitions += NumPartitions;
  MaxPartitionsPerAlloca.updateMax(NumPartitions);

  // Migrate debug information from the old alloca to the new alloca(s)
  // and the individual partitions.
  auto MigrateOne = [&](auto *DbgVariable) {
    // Can't overlap with undef memory.
    if (isKillAddress(DbgVariable))
      return;

    const Value *DbgPtr = getAddress(DbgVariable);
    DIExpression::FragmentInfo VarFrag =
        DbgVariable->getFragmentOrEntireVariable();
    // Get the address expression constant offset if one exists and the ops
    // that come after it.
    int64_t CurrentExprOffsetInBytes = 0;
    SmallVector<uint64_t> PostOffsetOps;
    if (!getAddressExpression(DbgVariable)
             ->extractLeadingOffset(CurrentExprOffsetInBytes, PostOffsetOps))
      return; // Couldn't interpret this DIExpression - drop the var.

    // Offset defined by a DW_OP_LLVM_extract_bits_[sz]ext.
    int64_t ExtractOffsetInBits = 0;
    for (auto Op : getAddressExpression(DbgVariable)->expr_ops()) {
      if (Op.getOp() == dwarf::DW_OP_LLVM_extract_bits_zext ||
          Op.getOp() == dwarf::DW_OP_LLVM_extract_bits_sext) {
        ExtractOffsetInBits = Op.getArg(0);
        break;
      }
    }

    DIBuilder DIB(*AI.getModule(), /*AllowUnresolved*/ false);
    for (auto Fragment : Fragments) {
      int64_t OffsetFromLocationInBits;
      std::optional<DIExpression::FragmentInfo> NewDbgFragment;
      // Find the variable fragment that the new alloca slice covers.
      // Drop debug info for this variable fragment if we can't compute an
      // intersect between it and the alloca slice.
      if (!DIExpression::calculateFragmentIntersect(
              DL, &AI, Fragment.Offset, Fragment.Size, DbgPtr,
              CurrentExprOffsetInBytes * 8, ExtractOffsetInBits, VarFrag,
              NewDbgFragment, OffsetFromLocationInBits))
        continue; // Do not migrate this fragment to this slice.

      // Zero sized fragment indicates there's no intersect between the variable
      // fragment and the alloca slice. Skip this slice for this variable
      // fragment.
      if (NewDbgFragment && !NewDbgFragment->SizeInBits)
        continue; // Do not migrate this fragment to this slice.

      // No fragment indicates DbgVariable's variable or fragment exactly
      // overlaps the slice; copy its fragment (or nullopt if there isn't one).
      if (!NewDbgFragment)
        NewDbgFragment = DbgVariable->getFragment();

      // Reduce the new expression offset by the bit-extract offset since
      // we'll be keeping that.
      int64_t OffestFromNewAllocaInBits =
          OffsetFromLocationInBits - ExtractOffsetInBits;
      // We need to adjust an existing bit extract if the offset expression
      // can't eat the slack (i.e., if the new offset would be negative).
      int64_t BitExtractOffset =
          std::min<int64_t>(0, OffestFromNewAllocaInBits);
      // The magnitude of a negative value indicates the number of bits into
      // the existing variable fragment that the memory region begins. The new
      // variable fragment already excludes those bits - the new DbgPtr offset
      // only needs to be applied if it's positive.
      OffestFromNewAllocaInBits =
          std::max(int64_t(0), OffestFromNewAllocaInBits);

      // Rebuild the expression:
      //    {Offset(OffestFromNewAllocaInBits), PostOffsetOps, NewDbgFragment}
      // Add NewDbgFragment later, because dbg.assigns don't want it in the
      // address expression but the value expression instead.
      DIExpression *NewExpr = DIExpression::get(AI.getContext(), PostOffsetOps);
      if (OffestFromNewAllocaInBits > 0) {
        int64_t OffsetInBytes = (OffestFromNewAllocaInBits + 7) / 8;
        NewExpr = DIExpression::prepend(NewExpr, /*flags=*/0, OffsetInBytes);
      }

      // Remove any existing intrinsics on the new alloca describing
      // the variable fragment.
      auto RemoveOne = [DbgVariable](auto *OldDII) {
        auto SameVariableFragment = [](const auto *LHS, const auto *RHS) {
          return LHS->getVariable() == RHS->getVariable() &&
                 LHS->getDebugLoc()->getInlinedAt() ==
                     RHS->getDebugLoc()->getInlinedAt();
        };
        if (SameVariableFragment(OldDII, DbgVariable))
          OldDII->eraseFromParent();
      };
      for_each(findDbgDeclares(Fragment.Alloca), RemoveOne);
      for_each(findDVRDeclares(Fragment.Alloca), RemoveOne);

      insertNewDbgInst(DIB, DbgVariable, Fragment.Alloca, NewExpr, &AI,
                       NewDbgFragment, BitExtractOffset);
    }
  };

  // Migrate debug information from the old alloca to the new alloca(s)
  // and the individual partitions.
  for_each(findDbgDeclares(&AI), MigrateOne);
  for_each(findDVRDeclares(&AI), MigrateOne);
  for_each(at::getAssignmentMarkers(&AI), MigrateOne);
  for_each(at::getDVRAssignmentMarkers(&AI), MigrateOne);

  return Changed;
}

/// Clobber a use with poison, deleting the used value if it becomes dead.
void SROA::clobberUse(Use &U) {
  Value *OldV = U;
  // Replace the use with an poison value.
  U = PoisonValue::get(OldV->getType());

  // Check for this making an instruction dead. We have to garbage collect
  // all the dead instructions to ensure the uses of any alloca end up being
  // minimal.
  if (Instruction *OldI = dyn_cast<Instruction>(OldV))
    if (isInstructionTriviallyDead(OldI)) {
      DeadInsts.push_back(OldI);
    }
}

/// Analyze an alloca for SROA.
///
/// This analyzes the alloca to ensure we can reason about it, builds
/// the slices of the alloca, and then hands it off to be split and
/// rewritten as needed.
std::pair<bool /*Changed*/, bool /*CFGChanged*/>
SROA::runOnAlloca(AllocaInst &AI) {
  bool Changed = false;
  bool CFGChanged = false;

  LLVM_DEBUG(dbgs() << "SROA alloca: " << AI << "\n");
  ++NumAllocasAnalyzed;

  // Special case dead allocas, as they're trivial.
  if (AI.use_empty()) {
    AI.eraseFromParent();
    Changed = true;
    return {Changed, CFGChanged};
  }
  const DataLayout &DL = AI.getDataLayout();

  // Skip alloca forms that this analysis can't handle.
  auto *AT = AI.getAllocatedType();
  TypeSize Size = DL.getTypeAllocSize(AT);
  if (AI.isArrayAllocation() || !AT->isSized() || Size.isScalable() ||
      Size.getFixedValue() == 0)
    return {Changed, CFGChanged};

  // First, split any FCA loads and stores touching this alloca to promote
  // better splitting and promotion opportunities.
  IRBuilderTy IRB(&AI);
  AggLoadStoreRewriter AggRewriter(DL, IRB);
  Changed |= AggRewriter.rewrite(AI);

  // Build the slices using a recursive instruction-visiting builder.
  AllocaSlices AS(DL, AI);
  LLVM_DEBUG(AS.print(dbgs()));
  if (AS.isEscaped())
    return {Changed, CFGChanged};

  // Delete all the dead users of this alloca before splitting and rewriting it.
  for (Instruction *DeadUser : AS.getDeadUsers()) {
    // Free up everything used by this instruction.
    for (Use &DeadOp : DeadUser->operands())
      clobberUse(DeadOp);

    // Now replace the uses of this instruction.
    DeadUser->replaceAllUsesWith(PoisonValue::get(DeadUser->getType()));

    // And mark it for deletion.
    DeadInsts.push_back(DeadUser);
    Changed = true;
  }
  for (Use *DeadOp : AS.getDeadOperands()) {
    clobberUse(*DeadOp);
    Changed = true;
  }

  // No slices to split. Leave the dead alloca for a later pass to clean up.
  if (AS.begin() == AS.end())
    return {Changed, CFGChanged};

  Changed |= splitAlloca(AI, AS);

  LLVM_DEBUG(dbgs() << "  Speculating PHIs\n");
  while (!SpeculatablePHIs.empty())
    speculatePHINodeLoads(IRB, *SpeculatablePHIs.pop_back_val());

  LLVM_DEBUG(dbgs() << "  Rewriting Selects\n");
  auto RemainingSelectsToRewrite = SelectsToRewrite.takeVector();
  while (!RemainingSelectsToRewrite.empty()) {
    const auto [K, V] = RemainingSelectsToRewrite.pop_back_val();
    CFGChanged |=
        rewriteSelectInstMemOps(*K, V, IRB, PreserveCFG ? nullptr : DTU);
  }

  return {Changed, CFGChanged};
}

/// Delete the dead instructions accumulated in this run.
///
/// Recursively deletes the dead instructions we've accumulated. This is done
/// at the very end to maximize locality of the recursive delete and to
/// minimize the problems of invalidated instruction pointers as such pointers
/// are used heavily in the intermediate stages of the algorithm.
///
/// We also record the alloca instructions deleted here so that they aren't
/// subsequently handed to mem2reg to promote.
bool SROA::deleteDeadInstructions(
    SmallPtrSetImpl<AllocaInst *> &DeletedAllocas) {
  bool Changed = false;
  while (!DeadInsts.empty()) {
    Instruction *I = dyn_cast_or_null<Instruction>(DeadInsts.pop_back_val());
    if (!I)
      continue;
    LLVM_DEBUG(dbgs() << "Deleting dead instruction: " << *I << "\n");

    // If the instruction is an alloca, find the possible dbg.declare connected
    // to it, and remove it too. We must do this before calling RAUW or we will
    // not be able to find it.
    if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
      DeletedAllocas.insert(AI);
      for (DbgDeclareInst *OldDII : findDbgDeclares(AI))
        OldDII->eraseFromParent();
      for (DbgVariableRecord *OldDII : findDVRDeclares(AI))
        OldDII->eraseFromParent();
    }

    at::deleteAssignmentMarkers(I);
    I->replaceAllUsesWith(UndefValue::get(I->getType()));

    for (Use &Operand : I->operands())
      if (Instruction *U = dyn_cast<Instruction>(Operand)) {
        // Zero out the operand and see if it becomes trivially dead.
        Operand = nullptr;
        if (isInstructionTriviallyDead(U))
          DeadInsts.push_back(U);
      }

    ++NumDeleted;
    I->eraseFromParent();
    Changed = true;
  }
  return Changed;
}

/// Promote the allocas, using the best available technique.
///
/// This attempts to promote whatever allocas have been identified as viable in
/// the PromotableAllocas list. If that list is empty, there is nothing to do.
/// This function returns whether any promotion occurred.
bool SROA::promoteAllocas(Function &F) {
  if (PromotableAllocas.empty())
    return false;

  NumPromoted += PromotableAllocas.size();

  if (SROASkipMem2Reg) {
    LLVM_DEBUG(dbgs() << "Not promoting allocas with mem2reg!\n");
  } else {
    LLVM_DEBUG(dbgs() << "Promoting allocas with mem2reg...\n");
    PromoteMemToReg(PromotableAllocas, DTU->getDomTree(), AC);
  }

  PromotableAllocas.clear();
  return true;
}

std::pair<bool /*Changed*/, bool /*CFGChanged*/> SROA::runSROA(Function &F) {
  LLVM_DEBUG(dbgs() << "SROA function: " << F.getName() << "\n");

  const DataLayout &DL = F.getDataLayout();
  BasicBlock &EntryBB = F.getEntryBlock();
  for (BasicBlock::iterator I = EntryBB.begin(), E = std::prev(EntryBB.end());
       I != E; ++I) {
    if (AllocaInst *AI = dyn_cast<AllocaInst>(I)) {
      if (DL.getTypeAllocSize(AI->getAllocatedType()).isScalable() &&
          isAllocaPromotable(AI))
        PromotableAllocas.push_back(AI);
      else
        Worklist.insert(AI);
    }
  }

  bool Changed = false;
  bool CFGChanged = false;
  // A set of deleted alloca instruction pointers which should be removed from
  // the list of promotable allocas.
  SmallPtrSet<AllocaInst *, 4> DeletedAllocas;

  do {
    while (!Worklist.empty()) {
      auto [IterationChanged, IterationCFGChanged] =
          runOnAlloca(*Worklist.pop_back_val());
      Changed |= IterationChanged;
      CFGChanged |= IterationCFGChanged;

      Changed |= deleteDeadInstructions(DeletedAllocas);

      // Remove the deleted allocas from various lists so that we don't try to
      // continue processing them.
      if (!DeletedAllocas.empty()) {
        auto IsInSet = [&](AllocaInst *AI) { return DeletedAllocas.count(AI); };
        Worklist.remove_if(IsInSet);
        PostPromotionWorklist.remove_if(IsInSet);
        llvm::erase_if(PromotableAllocas, IsInSet);
        DeletedAllocas.clear();
      }
    }

    Changed |= promoteAllocas(F);

    Worklist = PostPromotionWorklist;
    PostPromotionWorklist.clear();
  } while (!Worklist.empty());

  assert((!CFGChanged || Changed) && "Can not only modify the CFG.");
  assert((!CFGChanged || !PreserveCFG) &&
         "Should not have modified the CFG when told to preserve it.");

  if (Changed && isAssignmentTrackingEnabled(*F.getParent())) {
    for (auto &BB : F) {
      RemoveRedundantDbgInstrs(&BB);
    }
  }

  return {Changed, CFGChanged};
}

PreservedAnalyses SROAPass::run(Function &F, FunctionAnalysisManager &AM) {
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  AssumptionCache &AC = AM.getResult<AssumptionAnalysis>(F);
  DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
  auto [Changed, CFGChanged] =
      SROA(&F.getContext(), &DTU, &AC, PreserveCFG).runSROA(F);
  if (!Changed)
    return PreservedAnalyses::all();
  PreservedAnalyses PA;
  if (!CFGChanged)
    PA.preserveSet<CFGAnalyses>();
  PA.preserve<DominatorTreeAnalysis>();
  return PA;
}

void SROAPass::printPipeline(
    raw_ostream &OS, function_ref<StringRef(StringRef)> MapClassName2PassName) {
  static_cast<PassInfoMixin<SROAPass> *>(this)->printPipeline(
      OS, MapClassName2PassName);
  OS << (PreserveCFG == SROAOptions::PreserveCFG ? "<preserve-cfg>"
                                                 : "<modify-cfg>");
}

SROAPass::SROAPass(SROAOptions PreserveCFG) : PreserveCFG(PreserveCFG) {}

namespace {

/// A legacy pass for the legacy pass manager that wraps the \c SROA pass.
class SROALegacyPass : public FunctionPass {
  SROAOptions PreserveCFG;

public:
  static char ID;

  SROALegacyPass(SROAOptions PreserveCFG = SROAOptions::PreserveCFG)
      : FunctionPass(ID), PreserveCFG(PreserveCFG) {
    initializeSROALegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    if (skipFunction(F))
      return false;

    DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
    AssumptionCache &AC =
        getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);
    DomTreeUpdater DTU(DT, DomTreeUpdater::UpdateStrategy::Lazy);
    auto [Changed, _] =
        SROA(&F.getContext(), &DTU, &AC, PreserveCFG).runSROA(F);
    return Changed;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<AssumptionCacheTracker>();
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addPreserved<GlobalsAAWrapperPass>();
    AU.addPreserved<DominatorTreeWrapperPass>();
  }

  StringRef getPassName() const override { return "SROA"; }
};

} // end anonymous namespace

char SROALegacyPass::ID = 0;

FunctionPass *llvm::createSROAPass(bool PreserveCFG) {
  return new SROALegacyPass(PreserveCFG ? SROAOptions::PreserveCFG
                                        : SROAOptions::ModifyCFG);
}

INITIALIZE_PASS_BEGIN(SROALegacyPass, "sroa",
                      "Scalar Replacement Of Aggregates", false, false)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SROALegacyPass, "sroa", "Scalar Replacement Of Aggregates",
                    false, false)
