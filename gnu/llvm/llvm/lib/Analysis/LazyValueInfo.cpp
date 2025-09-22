//===- LazyValueInfo.cpp - Value constraint analysis ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface for lazy computation of value constraint
// information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/LazyValueInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/ValueLattice.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "lazy-value-info"

// This is the number of worklist items we will process to try to discover an
// answer for a given value.
static const unsigned MaxProcessedPerValue = 500;

char LazyValueInfoWrapperPass::ID = 0;
LazyValueInfoWrapperPass::LazyValueInfoWrapperPass() : FunctionPass(ID) {
  initializeLazyValueInfoWrapperPassPass(*PassRegistry::getPassRegistry());
}
INITIALIZE_PASS_BEGIN(LazyValueInfoWrapperPass, "lazy-value-info",
                "Lazy Value Information Analysis", false, true)
INITIALIZE_PASS_DEPENDENCY(AssumptionCacheTracker)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfoWrapperPass)
INITIALIZE_PASS_END(LazyValueInfoWrapperPass, "lazy-value-info",
                "Lazy Value Information Analysis", false, true)

namespace llvm {
FunctionPass *createLazyValueInfoPass() {
  return new LazyValueInfoWrapperPass();
}
} // namespace llvm

AnalysisKey LazyValueAnalysis::Key;

/// Returns true if this lattice value represents at most one possible value.
/// This is as precise as any lattice value can get while still representing
/// reachable code.
static bool hasSingleValue(const ValueLatticeElement &Val) {
  if (Val.isConstantRange() &&
      Val.getConstantRange().isSingleElement())
    // Integer constants are single element ranges
    return true;
  if (Val.isConstant())
    // Non integer constants
    return true;
  return false;
}

/// Combine two sets of facts about the same value into a single set of
/// facts.  Note that this method is not suitable for merging facts along
/// different paths in a CFG; that's what the mergeIn function is for.  This
/// is for merging facts gathered about the same value at the same location
/// through two independent means.
/// Notes:
/// * This method does not promise to return the most precise possible lattice
///   value implied by A and B.  It is allowed to return any lattice element
///   which is at least as strong as *either* A or B (unless our facts
///   conflict, see below).
/// * Due to unreachable code, the intersection of two lattice values could be
///   contradictory.  If this happens, we return some valid lattice value so as
///   not confuse the rest of LVI.  Ideally, we'd always return Undefined, but
///   we do not make this guarantee.  TODO: This would be a useful enhancement.
static ValueLatticeElement intersect(const ValueLatticeElement &A,
                                     const ValueLatticeElement &B) {
  // Undefined is the strongest state.  It means the value is known to be along
  // an unreachable path.
  if (A.isUnknown())
    return A;
  if (B.isUnknown())
    return B;

  // If we gave up for one, but got a useable fact from the other, use it.
  if (A.isOverdefined())
    return B;
  if (B.isOverdefined())
    return A;

  // Can't get any more precise than constants.
  if (hasSingleValue(A))
    return A;
  if (hasSingleValue(B))
    return B;

  // Could be either constant range or not constant here.
  if (!A.isConstantRange() || !B.isConstantRange()) {
    // TODO: Arbitrary choice, could be improved
    return A;
  }

  // Intersect two constant ranges
  ConstantRange Range =
      A.getConstantRange().intersectWith(B.getConstantRange());
  // Note: An empty range is implicitly converted to unknown or undef depending
  // on MayIncludeUndef internally.
  return ValueLatticeElement::getRange(
      std::move(Range), /*MayIncludeUndef=*/A.isConstantRangeIncludingUndef() ||
                            B.isConstantRangeIncludingUndef());
}

//===----------------------------------------------------------------------===//
//                          LazyValueInfoCache Decl
//===----------------------------------------------------------------------===//

namespace {
  /// A callback value handle updates the cache when values are erased.
  class LazyValueInfoCache;
  struct LVIValueHandle final : public CallbackVH {
    LazyValueInfoCache *Parent;

    LVIValueHandle(Value *V, LazyValueInfoCache *P = nullptr)
      : CallbackVH(V), Parent(P) { }

    void deleted() override;
    void allUsesReplacedWith(Value *V) override {
      deleted();
    }
  };
} // end anonymous namespace

namespace {
using NonNullPointerSet = SmallDenseSet<AssertingVH<Value>, 2>;

/// This is the cache kept by LazyValueInfo which
/// maintains information about queries across the clients' queries.
class LazyValueInfoCache {
  /// This is all of the cached information for one basic block. It contains
  /// the per-value lattice elements, as well as a separate set for
  /// overdefined values to reduce memory usage. Additionally pointers
  /// dereferenced in the block are cached for nullability queries.
  struct BlockCacheEntry {
    SmallDenseMap<AssertingVH<Value>, ValueLatticeElement, 4> LatticeElements;
    SmallDenseSet<AssertingVH<Value>, 4> OverDefined;
    // std::nullopt indicates that the nonnull pointers for this basic block
    // block have not been computed yet.
    std::optional<NonNullPointerSet> NonNullPointers;
  };

  /// Cached information per basic block.
  DenseMap<PoisoningVH<BasicBlock>, std::unique_ptr<BlockCacheEntry>>
      BlockCache;
  /// Set of value handles used to erase values from the cache on deletion.
  DenseSet<LVIValueHandle, DenseMapInfo<Value *>> ValueHandles;

  const BlockCacheEntry *getBlockEntry(BasicBlock *BB) const {
    auto It = BlockCache.find_as(BB);
    if (It == BlockCache.end())
      return nullptr;
    return It->second.get();
  }

  BlockCacheEntry *getOrCreateBlockEntry(BasicBlock *BB) {
    auto It = BlockCache.find_as(BB);
    if (It == BlockCache.end())
      It = BlockCache.insert({BB, std::make_unique<BlockCacheEntry>()}).first;

    return It->second.get();
  }

  void addValueHandle(Value *Val) {
    auto HandleIt = ValueHandles.find_as(Val);
    if (HandleIt == ValueHandles.end())
      ValueHandles.insert({Val, this});
  }

public:
  void insertResult(Value *Val, BasicBlock *BB,
                    const ValueLatticeElement &Result) {
    BlockCacheEntry *Entry = getOrCreateBlockEntry(BB);

    // Insert over-defined values into their own cache to reduce memory
    // overhead.
    if (Result.isOverdefined())
      Entry->OverDefined.insert(Val);
    else
      Entry->LatticeElements.insert({Val, Result});

    addValueHandle(Val);
  }

  std::optional<ValueLatticeElement> getCachedValueInfo(Value *V,
                                                        BasicBlock *BB) const {
    const BlockCacheEntry *Entry = getBlockEntry(BB);
    if (!Entry)
      return std::nullopt;

    if (Entry->OverDefined.count(V))
      return ValueLatticeElement::getOverdefined();

    auto LatticeIt = Entry->LatticeElements.find_as(V);
    if (LatticeIt == Entry->LatticeElements.end())
      return std::nullopt;

    return LatticeIt->second;
  }

  bool
  isNonNullAtEndOfBlock(Value *V, BasicBlock *BB,
                        function_ref<NonNullPointerSet(BasicBlock *)> InitFn) {
    BlockCacheEntry *Entry = getOrCreateBlockEntry(BB);
    if (!Entry->NonNullPointers) {
      Entry->NonNullPointers = InitFn(BB);
      for (Value *V : *Entry->NonNullPointers)
        addValueHandle(V);
    }

    return Entry->NonNullPointers->count(V);
  }

  /// clear - Empty the cache.
  void clear() {
    BlockCache.clear();
    ValueHandles.clear();
  }

  /// Inform the cache that a given value has been deleted.
  void eraseValue(Value *V);

  /// This is part of the update interface to inform the cache
  /// that a block has been deleted.
  void eraseBlock(BasicBlock *BB);

  /// Updates the cache to remove any influence an overdefined value in
  /// OldSucc might have (unless also overdefined in NewSucc).  This just
  /// flushes elements from the cache and does not add any.
  void threadEdgeImpl(BasicBlock *OldSucc, BasicBlock *NewSucc);
};
} // namespace

void LazyValueInfoCache::eraseValue(Value *V) {
  for (auto &Pair : BlockCache) {
    Pair.second->LatticeElements.erase(V);
    Pair.second->OverDefined.erase(V);
    if (Pair.second->NonNullPointers)
      Pair.second->NonNullPointers->erase(V);
  }

  auto HandleIt = ValueHandles.find_as(V);
  if (HandleIt != ValueHandles.end())
    ValueHandles.erase(HandleIt);
}

void LVIValueHandle::deleted() {
  // This erasure deallocates *this, so it MUST happen after we're done
  // using any and all members of *this.
  Parent->eraseValue(*this);
}

void LazyValueInfoCache::eraseBlock(BasicBlock *BB) {
  BlockCache.erase(BB);
}

void LazyValueInfoCache::threadEdgeImpl(BasicBlock *OldSucc,
                                        BasicBlock *NewSucc) {
  // When an edge in the graph has been threaded, values that we could not
  // determine a value for before (i.e. were marked overdefined) may be
  // possible to solve now. We do NOT try to proactively update these values.
  // Instead, we clear their entries from the cache, and allow lazy updating to
  // recompute them when needed.

  // The updating process is fairly simple: we need to drop cached info
  // for all values that were marked overdefined in OldSucc, and for those same
  // values in any successor of OldSucc (except NewSucc) in which they were
  // also marked overdefined.
  std::vector<BasicBlock*> worklist;
  worklist.push_back(OldSucc);

  const BlockCacheEntry *Entry = getBlockEntry(OldSucc);
  if (!Entry || Entry->OverDefined.empty())
    return; // Nothing to process here.
  SmallVector<Value *, 4> ValsToClear(Entry->OverDefined.begin(),
                                      Entry->OverDefined.end());

  // Use a worklist to perform a depth-first search of OldSucc's successors.
  // NOTE: We do not need a visited list since any blocks we have already
  // visited will have had their overdefined markers cleared already, and we
  // thus won't loop to their successors.
  while (!worklist.empty()) {
    BasicBlock *ToUpdate = worklist.back();
    worklist.pop_back();

    // Skip blocks only accessible through NewSucc.
    if (ToUpdate == NewSucc) continue;

    // If a value was marked overdefined in OldSucc, and is here too...
    auto OI = BlockCache.find_as(ToUpdate);
    if (OI == BlockCache.end() || OI->second->OverDefined.empty())
      continue;
    auto &ValueSet = OI->second->OverDefined;

    bool changed = false;
    for (Value *V : ValsToClear) {
      if (!ValueSet.erase(V))
        continue;

      // If we removed anything, then we potentially need to update
      // blocks successors too.
      changed = true;
    }

    if (!changed) continue;

    llvm::append_range(worklist, successors(ToUpdate));
  }
}

namespace llvm {
namespace {
/// An assembly annotator class to print LazyValueCache information in
/// comments.
class LazyValueInfoAnnotatedWriter : public AssemblyAnnotationWriter {
  LazyValueInfoImpl *LVIImpl;
  // While analyzing which blocks we can solve values for, we need the dominator
  // information.
  DominatorTree &DT;

public:
  LazyValueInfoAnnotatedWriter(LazyValueInfoImpl *L, DominatorTree &DTree)
      : LVIImpl(L), DT(DTree) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override;

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override;
};
} // namespace
// The actual implementation of the lazy analysis and update.  Note that the
// inheritance from LazyValueInfoCache is intended to be temporary while
// splitting the code and then transitioning to a has-a relationship.
class LazyValueInfoImpl {

  /// Cached results from previous queries
  LazyValueInfoCache TheCache;

  /// This stack holds the state of the value solver during a query.
  /// It basically emulates the callstack of the naive
  /// recursive value lookup process.
  SmallVector<std::pair<BasicBlock*, Value*>, 8> BlockValueStack;

  /// Keeps track of which block-value pairs are in BlockValueStack.
  DenseSet<std::pair<BasicBlock*, Value*> > BlockValueSet;

  /// Push BV onto BlockValueStack unless it's already in there.
  /// Returns true on success.
  bool pushBlockValue(const std::pair<BasicBlock *, Value *> &BV) {
    if (!BlockValueSet.insert(BV).second)
      return false;  // It's already in the stack.

    LLVM_DEBUG(dbgs() << "PUSH: " << *BV.second << " in "
                      << BV.first->getName() << "\n");
    BlockValueStack.push_back(BV);
    return true;
  }

  AssumptionCache *AC;  ///< A pointer to the cache of @llvm.assume calls.
  const DataLayout &DL; ///< A mandatory DataLayout

  /// Declaration of the llvm.experimental.guard() intrinsic,
  /// if it exists in the module.
  Function *GuardDecl;

  std::optional<ValueLatticeElement> getBlockValue(Value *Val, BasicBlock *BB,
                                                   Instruction *CxtI);
  std::optional<ValueLatticeElement> getEdgeValue(Value *V, BasicBlock *F,
                                                  BasicBlock *T,
                                                  Instruction *CxtI = nullptr);

  // These methods process one work item and may add more. A false value
  // returned means that the work item was not completely processed and must
  // be revisited after going through the new items.
  bool solveBlockValue(Value *Val, BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueImpl(Value *Val,
                                                         BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueNonLocal(Value *Val,
                                                             BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValuePHINode(PHINode *PN,
                                                            BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueSelect(SelectInst *S,
                                                           BasicBlock *BB);
  std::optional<ConstantRange> getRangeFor(Value *V, Instruction *CxtI,
                                           BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueBinaryOpImpl(
      Instruction *I, BasicBlock *BB,
      std::function<ConstantRange(const ConstantRange &, const ConstantRange &)>
          OpFn);
  std::optional<ValueLatticeElement>
  solveBlockValueBinaryOp(BinaryOperator *BBI, BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueCast(CastInst *CI,
                                                         BasicBlock *BB);
  std::optional<ValueLatticeElement>
  solveBlockValueOverflowIntrinsic(WithOverflowInst *WO, BasicBlock *BB);
  std::optional<ValueLatticeElement> solveBlockValueIntrinsic(IntrinsicInst *II,
                                                              BasicBlock *BB);
  std::optional<ValueLatticeElement>
  solveBlockValueInsertElement(InsertElementInst *IEI, BasicBlock *BB);
  std::optional<ValueLatticeElement>
  solveBlockValueExtractValue(ExtractValueInst *EVI, BasicBlock *BB);
  bool isNonNullAtEndOfBlock(Value *Val, BasicBlock *BB);
  void intersectAssumeOrGuardBlockValueConstantRange(Value *Val,
                                                     ValueLatticeElement &BBLV,
                                                     Instruction *BBI);

  void solve();

  // For the following methods, if UseBlockValue is true, the function may
  // push additional values to the worklist and return nullopt. If
  // UseBlockValue is false, it will never return nullopt.

  std::optional<ValueLatticeElement>
  getValueFromSimpleICmpCondition(CmpInst::Predicate Pred, Value *RHS,
                                  const APInt &Offset, Instruction *CxtI,
                                  bool UseBlockValue);

  std::optional<ValueLatticeElement>
  getValueFromICmpCondition(Value *Val, ICmpInst *ICI, bool isTrueDest,
                            bool UseBlockValue);

  std::optional<ValueLatticeElement>
  getValueFromCondition(Value *Val, Value *Cond, bool IsTrueDest,
                        bool UseBlockValue, unsigned Depth = 0);

  std::optional<ValueLatticeElement> getEdgeValueLocal(Value *Val,
                                                       BasicBlock *BBFrom,
                                                       BasicBlock *BBTo,
                                                       bool UseBlockValue);

public:
  /// This is the query interface to determine the lattice value for the
  /// specified Value* at the context instruction (if specified) or at the
  /// start of the block.
  ValueLatticeElement getValueInBlock(Value *V, BasicBlock *BB,
                                      Instruction *CxtI = nullptr);

  /// This is the query interface to determine the lattice value for the
  /// specified Value* at the specified instruction using only information
  /// from assumes/guards and range metadata. Unlike getValueInBlock(), no
  /// recursive query is performed.
  ValueLatticeElement getValueAt(Value *V, Instruction *CxtI);

  /// This is the query interface to determine the lattice
  /// value for the specified Value* that is true on the specified edge.
  ValueLatticeElement getValueOnEdge(Value *V, BasicBlock *FromBB,
                                     BasicBlock *ToBB,
                                     Instruction *CxtI = nullptr);

  ValueLatticeElement getValueAtUse(const Use &U);

  /// Complete flush all previously computed values
  void clear() {
    TheCache.clear();
  }

  /// Printing the LazyValueInfo Analysis.
  void printLVI(Function &F, DominatorTree &DTree, raw_ostream &OS) {
    LazyValueInfoAnnotatedWriter Writer(this, DTree);
    F.print(OS, &Writer);
  }

  /// This is part of the update interface to remove information related to this
  /// value from the cache.
  void forgetValue(Value *V) { TheCache.eraseValue(V); }

  /// This is part of the update interface to inform the cache
  /// that a block has been deleted.
  void eraseBlock(BasicBlock *BB) {
    TheCache.eraseBlock(BB);
  }

  /// This is the update interface to inform the cache that an edge from
  /// PredBB to OldSucc has been threaded to be from PredBB to NewSucc.
  void threadEdge(BasicBlock *PredBB,BasicBlock *OldSucc,BasicBlock *NewSucc);

  LazyValueInfoImpl(AssumptionCache *AC, const DataLayout &DL,
                    Function *GuardDecl)
      : AC(AC), DL(DL), GuardDecl(GuardDecl) {}
};
} // namespace llvm

void LazyValueInfoImpl::solve() {
  SmallVector<std::pair<BasicBlock *, Value *>, 8> StartingStack(
      BlockValueStack.begin(), BlockValueStack.end());

  unsigned processedCount = 0;
  while (!BlockValueStack.empty()) {
    processedCount++;
    // Abort if we have to process too many values to get a result for this one.
    // Because of the design of the overdefined cache currently being per-block
    // to avoid naming-related issues (IE it wants to try to give different
    // results for the same name in different blocks), overdefined results don't
    // get cached globally, which in turn means we will often try to rediscover
    // the same overdefined result again and again.  Once something like
    // PredicateInfo is used in LVI or CVP, we should be able to make the
    // overdefined cache global, and remove this throttle.
    if (processedCount > MaxProcessedPerValue) {
      LLVM_DEBUG(
          dbgs() << "Giving up on stack because we are getting too deep\n");
      // Fill in the original values
      while (!StartingStack.empty()) {
        std::pair<BasicBlock *, Value *> &e = StartingStack.back();
        TheCache.insertResult(e.second, e.first,
                              ValueLatticeElement::getOverdefined());
        StartingStack.pop_back();
      }
      BlockValueSet.clear();
      BlockValueStack.clear();
      return;
    }
    std::pair<BasicBlock *, Value *> e = BlockValueStack.back();
    assert(BlockValueSet.count(e) && "Stack value should be in BlockValueSet!");
    unsigned StackSize = BlockValueStack.size();
    (void) StackSize;

    if (solveBlockValue(e.second, e.first)) {
      // The work item was completely processed.
      assert(BlockValueStack.size() == StackSize &&
             BlockValueStack.back() == e && "Nothing should have been pushed!");
#ifndef NDEBUG
      std::optional<ValueLatticeElement> BBLV =
          TheCache.getCachedValueInfo(e.second, e.first);
      assert(BBLV && "Result should be in cache!");
      LLVM_DEBUG(
          dbgs() << "POP " << *e.second << " in " << e.first->getName() << " = "
                 << *BBLV << "\n");
#endif

      BlockValueStack.pop_back();
      BlockValueSet.erase(e);
    } else {
      // More work needs to be done before revisiting.
      assert(BlockValueStack.size() == StackSize + 1 &&
             "Exactly one element should have been pushed!");
    }
  }
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::getBlockValue(Value *Val, BasicBlock *BB,
                                 Instruction *CxtI) {
  // If already a constant, there is nothing to compute.
  if (Constant *VC = dyn_cast<Constant>(Val))
    return ValueLatticeElement::get(VC);

  if (std::optional<ValueLatticeElement> OptLatticeVal =
          TheCache.getCachedValueInfo(Val, BB)) {
    intersectAssumeOrGuardBlockValueConstantRange(Val, *OptLatticeVal, CxtI);
    return OptLatticeVal;
  }

  // We have hit a cycle, assume overdefined.
  if (!pushBlockValue({ BB, Val }))
    return ValueLatticeElement::getOverdefined();

  // Yet to be resolved.
  return std::nullopt;
}

static ValueLatticeElement getFromRangeMetadata(Instruction *BBI) {
  switch (BBI->getOpcode()) {
  default:
    break;
  case Instruction::Call:
  case Instruction::Invoke:
    if (std::optional<ConstantRange> Range = cast<CallBase>(BBI)->getRange())
      return ValueLatticeElement::getRange(*Range);
    [[fallthrough]];
  case Instruction::Load:
    if (MDNode *Ranges = BBI->getMetadata(LLVMContext::MD_range))
      if (isa<IntegerType>(BBI->getType())) {
        return ValueLatticeElement::getRange(
            getConstantRangeFromMetadata(*Ranges));
      }
    break;
  };
  // Nothing known - will be intersected with other facts
  return ValueLatticeElement::getOverdefined();
}

bool LazyValueInfoImpl::solveBlockValue(Value *Val, BasicBlock *BB) {
  assert(!isa<Constant>(Val) && "Value should not be constant");
  assert(!TheCache.getCachedValueInfo(Val, BB) &&
         "Value should not be in cache");

  // Hold off inserting this value into the Cache in case we have to return
  // false and come back later.
  std::optional<ValueLatticeElement> Res = solveBlockValueImpl(Val, BB);
  if (!Res)
    // Work pushed, will revisit
    return false;

  TheCache.insertResult(Val, BB, *Res);
  return true;
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueImpl(Value *Val, BasicBlock *BB) {
  Instruction *BBI = dyn_cast<Instruction>(Val);
  if (!BBI || BBI->getParent() != BB)
    return solveBlockValueNonLocal(Val, BB);

  if (PHINode *PN = dyn_cast<PHINode>(BBI))
    return solveBlockValuePHINode(PN, BB);

  if (auto *SI = dyn_cast<SelectInst>(BBI))
    return solveBlockValueSelect(SI, BB);

  // If this value is a nonnull pointer, record it's range and bailout.  Note
  // that for all other pointer typed values, we terminate the search at the
  // definition.  We could easily extend this to look through geps, bitcasts,
  // and the like to prove non-nullness, but it's not clear that's worth it
  // compile time wise.  The context-insensitive value walk done inside
  // isKnownNonZero gets most of the profitable cases at much less expense.
  // This does mean that we have a sensitivity to where the defining
  // instruction is placed, even if it could legally be hoisted much higher.
  // That is unfortunate.
  PointerType *PT = dyn_cast<PointerType>(BBI->getType());
  if (PT && isKnownNonZero(BBI, DL))
    return ValueLatticeElement::getNot(ConstantPointerNull::get(PT));

  if (BBI->getType()->isIntOrIntVectorTy()) {
    if (auto *CI = dyn_cast<CastInst>(BBI))
      return solveBlockValueCast(CI, BB);

    if (BinaryOperator *BO = dyn_cast<BinaryOperator>(BBI))
      return solveBlockValueBinaryOp(BO, BB);

    if (auto *IEI = dyn_cast<InsertElementInst>(BBI))
      return solveBlockValueInsertElement(IEI, BB);

    if (auto *EVI = dyn_cast<ExtractValueInst>(BBI))
      return solveBlockValueExtractValue(EVI, BB);

    if (auto *II = dyn_cast<IntrinsicInst>(BBI))
      return solveBlockValueIntrinsic(II, BB);
  }

  LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                    << "' - unknown inst def found.\n");
  return getFromRangeMetadata(BBI);
}

static void AddNonNullPointer(Value *Ptr, NonNullPointerSet &PtrSet) {
  // TODO: Use NullPointerIsDefined instead.
  if (Ptr->getType()->getPointerAddressSpace() == 0)
    PtrSet.insert(getUnderlyingObject(Ptr));
}

static void AddNonNullPointersByInstruction(
    Instruction *I, NonNullPointerSet &PtrSet) {
  if (LoadInst *L = dyn_cast<LoadInst>(I)) {
    AddNonNullPointer(L->getPointerOperand(), PtrSet);
  } else if (StoreInst *S = dyn_cast<StoreInst>(I)) {
    AddNonNullPointer(S->getPointerOperand(), PtrSet);
  } else if (MemIntrinsic *MI = dyn_cast<MemIntrinsic>(I)) {
    if (MI->isVolatile()) return;

    // FIXME: check whether it has a valuerange that excludes zero?
    ConstantInt *Len = dyn_cast<ConstantInt>(MI->getLength());
    if (!Len || Len->isZero()) return;

    AddNonNullPointer(MI->getRawDest(), PtrSet);
    if (MemTransferInst *MTI = dyn_cast<MemTransferInst>(MI))
      AddNonNullPointer(MTI->getRawSource(), PtrSet);
  }
}

bool LazyValueInfoImpl::isNonNullAtEndOfBlock(Value *Val, BasicBlock *BB) {
  if (NullPointerIsDefined(BB->getParent(),
                           Val->getType()->getPointerAddressSpace()))
    return false;

  Val = Val->stripInBoundsOffsets();
  return TheCache.isNonNullAtEndOfBlock(Val, BB, [](BasicBlock *BB) {
    NonNullPointerSet NonNullPointers;
    for (Instruction &I : *BB)
      AddNonNullPointersByInstruction(&I, NonNullPointers);
    return NonNullPointers;
  });
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueNonLocal(Value *Val, BasicBlock *BB) {
  ValueLatticeElement Result;  // Start Undefined.

  // If this is the entry block, we must be asking about an argument.
  if (BB->isEntryBlock()) {
    assert(isa<Argument>(Val) && "Unknown live-in to the entry block");
    if (std::optional<ConstantRange> Range = cast<Argument>(Val)->getRange())
      return ValueLatticeElement::getRange(*Range);
    return ValueLatticeElement::getOverdefined();
  }

  // Loop over all of our predecessors, merging what we know from them into
  // result.  If we encounter an unexplored predecessor, we eagerly explore it
  // in a depth first manner.  In practice, this has the effect of discovering
  // paths we can't analyze eagerly without spending compile times analyzing
  // other paths.  This heuristic benefits from the fact that predecessors are
  // frequently arranged such that dominating ones come first and we quickly
  // find a path to function entry.  TODO: We should consider explicitly
  // canonicalizing to make this true rather than relying on this happy
  // accident.
  for (BasicBlock *Pred : predecessors(BB)) {
    std::optional<ValueLatticeElement> EdgeResult = getEdgeValue(Val, Pred, BB);
    if (!EdgeResult)
      // Explore that input, then return here
      return std::nullopt;

    Result.mergeIn(*EdgeResult);

    // If we hit overdefined, exit early.  The BlockVals entry is already set
    // to overdefined.
    if (Result.isOverdefined()) {
      LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                        << "' - overdefined because of pred '"
                        << Pred->getName() << "' (non local).\n");
      return Result;
    }
  }

  // Return the merged value, which is more precise than 'overdefined'.
  assert(!Result.isOverdefined());
  return Result;
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValuePHINode(PHINode *PN, BasicBlock *BB) {
  ValueLatticeElement Result;  // Start Undefined.

  // Loop over all of our predecessors, merging what we know from them into
  // result.  See the comment about the chosen traversal order in
  // solveBlockValueNonLocal; the same reasoning applies here.
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i) {
    BasicBlock *PhiBB = PN->getIncomingBlock(i);
    Value *PhiVal = PN->getIncomingValue(i);
    // Note that we can provide PN as the context value to getEdgeValue, even
    // though the results will be cached, because PN is the value being used as
    // the cache key in the caller.
    std::optional<ValueLatticeElement> EdgeResult =
        getEdgeValue(PhiVal, PhiBB, BB, PN);
    if (!EdgeResult)
      // Explore that input, then return here
      return std::nullopt;

    Result.mergeIn(*EdgeResult);

    // If we hit overdefined, exit early.  The BlockVals entry is already set
    // to overdefined.
    if (Result.isOverdefined()) {
      LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                        << "' - overdefined because of pred (local).\n");

      return Result;
    }
  }

  // Return the merged value, which is more precise than 'overdefined'.
  assert(!Result.isOverdefined() && "Possible PHI in entry block?");
  return Result;
}

// If we can determine a constraint on the value given conditions assumed by
// the program, intersect those constraints with BBLV
void LazyValueInfoImpl::intersectAssumeOrGuardBlockValueConstantRange(
    Value *Val, ValueLatticeElement &BBLV, Instruction *BBI) {
  BBI = BBI ? BBI : dyn_cast<Instruction>(Val);
  if (!BBI)
    return;

  BasicBlock *BB = BBI->getParent();
  for (auto &AssumeVH : AC->assumptionsFor(Val)) {
    if (!AssumeVH)
      continue;

    // Only check assumes in the block of the context instruction. Other
    // assumes will have already been taken into account when the value was
    // propagated from predecessor blocks.
    auto *I = cast<CallInst>(AssumeVH);
    if (I->getParent() != BB || !isValidAssumeForContext(I, BBI))
      continue;

    BBLV = intersect(BBLV, *getValueFromCondition(Val, I->getArgOperand(0),
                                                  /*IsTrueDest*/ true,
                                                  /*UseBlockValue*/ false));
  }

  // If guards are not used in the module, don't spend time looking for them
  if (GuardDecl && !GuardDecl->use_empty() &&
      BBI->getIterator() != BB->begin()) {
    for (Instruction &I :
         make_range(std::next(BBI->getIterator().getReverse()), BB->rend())) {
      Value *Cond = nullptr;
      if (match(&I, m_Intrinsic<Intrinsic::experimental_guard>(m_Value(Cond))))
        BBLV = intersect(BBLV,
                         *getValueFromCondition(Val, Cond, /*IsTrueDest*/ true,
                                                /*UseBlockValue*/ false));
    }
  }

  if (BBLV.isOverdefined()) {
    // Check whether we're checking at the terminator, and the pointer has
    // been dereferenced in this block.
    PointerType *PTy = dyn_cast<PointerType>(Val->getType());
    if (PTy && BB->getTerminator() == BBI &&
        isNonNullAtEndOfBlock(Val, BB))
      BBLV = ValueLatticeElement::getNot(ConstantPointerNull::get(PTy));
  }
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueSelect(SelectInst *SI, BasicBlock *BB) {
  // Recurse on our inputs if needed
  std::optional<ValueLatticeElement> OptTrueVal =
      getBlockValue(SI->getTrueValue(), BB, SI);
  if (!OptTrueVal)
    return std::nullopt;
  ValueLatticeElement &TrueVal = *OptTrueVal;

  std::optional<ValueLatticeElement> OptFalseVal =
      getBlockValue(SI->getFalseValue(), BB, SI);
  if (!OptFalseVal)
    return std::nullopt;
  ValueLatticeElement &FalseVal = *OptFalseVal;

  if (TrueVal.isConstantRange() || FalseVal.isConstantRange()) {
    const ConstantRange &TrueCR = TrueVal.asConstantRange(SI->getType());
    const ConstantRange &FalseCR = FalseVal.asConstantRange(SI->getType());
    Value *LHS = nullptr;
    Value *RHS = nullptr;
    SelectPatternResult SPR = matchSelectPattern(SI, LHS, RHS);
    // Is this a min specifically of our two inputs?  (Avoid the risk of
    // ValueTracking getting smarter looking back past our immediate inputs.)
    if (SelectPatternResult::isMinOrMax(SPR.Flavor) &&
        ((LHS == SI->getTrueValue() && RHS == SI->getFalseValue()) ||
         (RHS == SI->getTrueValue() && LHS == SI->getFalseValue()))) {
      ConstantRange ResultCR = [&]() {
        switch (SPR.Flavor) {
        default:
          llvm_unreachable("unexpected minmax type!");
        case SPF_SMIN:                   /// Signed minimum
          return TrueCR.smin(FalseCR);
        case SPF_UMIN:                   /// Unsigned minimum
          return TrueCR.umin(FalseCR);
        case SPF_SMAX:                   /// Signed maximum
          return TrueCR.smax(FalseCR);
        case SPF_UMAX:                   /// Unsigned maximum
          return TrueCR.umax(FalseCR);
        };
      }();
      return ValueLatticeElement::getRange(
          ResultCR, TrueVal.isConstantRangeIncludingUndef() ||
                        FalseVal.isConstantRangeIncludingUndef());
    }

    if (SPR.Flavor == SPF_ABS) {
      if (LHS == SI->getTrueValue())
        return ValueLatticeElement::getRange(
            TrueCR.abs(), TrueVal.isConstantRangeIncludingUndef());
      if (LHS == SI->getFalseValue())
        return ValueLatticeElement::getRange(
            FalseCR.abs(), FalseVal.isConstantRangeIncludingUndef());
    }

    if (SPR.Flavor == SPF_NABS) {
      ConstantRange Zero(APInt::getZero(TrueCR.getBitWidth()));
      if (LHS == SI->getTrueValue())
        return ValueLatticeElement::getRange(
            Zero.sub(TrueCR.abs()), FalseVal.isConstantRangeIncludingUndef());
      if (LHS == SI->getFalseValue())
        return ValueLatticeElement::getRange(
            Zero.sub(FalseCR.abs()), FalseVal.isConstantRangeIncludingUndef());
    }
  }

  // Can we constrain the facts about the true and false values by using the
  // condition itself?  This shows up with idioms like e.g. select(a > 5, a, 5).
  // TODO: We could potentially refine an overdefined true value above.
  Value *Cond = SI->getCondition();
  // If the value is undef, a different value may be chosen in
  // the select condition.
  if (isGuaranteedNotToBeUndef(Cond, AC)) {
    TrueVal =
        intersect(TrueVal, *getValueFromCondition(SI->getTrueValue(), Cond,
                                                  /*IsTrueDest*/ true,
                                                  /*UseBlockValue*/ false));
    FalseVal =
        intersect(FalseVal, *getValueFromCondition(SI->getFalseValue(), Cond,
                                                   /*IsTrueDest*/ false,
                                                   /*UseBlockValue*/ false));
  }

  ValueLatticeElement Result = TrueVal;
  Result.mergeIn(FalseVal);
  return Result;
}

std::optional<ConstantRange>
LazyValueInfoImpl::getRangeFor(Value *V, Instruction *CxtI, BasicBlock *BB) {
  std::optional<ValueLatticeElement> OptVal = getBlockValue(V, BB, CxtI);
  if (!OptVal)
    return std::nullopt;
  return OptVal->asConstantRange(V->getType());
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueCast(CastInst *CI, BasicBlock *BB) {
  // Filter out casts we don't know how to reason about before attempting to
  // recurse on our operand.  This can cut a long search short if we know we're
  // not going to be able to get any useful information anways.
  switch (CI->getOpcode()) {
  case Instruction::Trunc:
  case Instruction::SExt:
  case Instruction::ZExt:
    break;
  default:
    // Unhandled instructions are overdefined.
    LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                      << "' - overdefined (unknown cast).\n");
    return ValueLatticeElement::getOverdefined();
  }

  // Figure out the range of the LHS.  If that fails, we still apply the
  // transfer rule on the full set since we may be able to locally infer
  // interesting facts.
  std::optional<ConstantRange> LHSRes = getRangeFor(CI->getOperand(0), CI, BB);
  if (!LHSRes)
    // More work to do before applying this transfer rule.
    return std::nullopt;
  const ConstantRange &LHSRange = *LHSRes;

  const unsigned ResultBitWidth = CI->getType()->getScalarSizeInBits();

  // NOTE: We're currently limited by the set of operations that ConstantRange
  // can evaluate symbolically.  Enhancing that set will allows us to analyze
  // more definitions.
  return ValueLatticeElement::getRange(LHSRange.castOp(CI->getOpcode(),
                                                       ResultBitWidth));
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueBinaryOpImpl(
    Instruction *I, BasicBlock *BB,
    std::function<ConstantRange(const ConstantRange &, const ConstantRange &)>
        OpFn) {
  // Figure out the ranges of the operands.  If that fails, use a
  // conservative range, but apply the transfer rule anyways.  This
  // lets us pick up facts from expressions like "and i32 (call i32
  // @foo()), 32"
  std::optional<ConstantRange> LHSRes = getRangeFor(I->getOperand(0), I, BB);
  if (!LHSRes)
    return std::nullopt;

  std::optional<ConstantRange> RHSRes = getRangeFor(I->getOperand(1), I, BB);
  if (!RHSRes)
    return std::nullopt;

  const ConstantRange &LHSRange = *LHSRes;
  const ConstantRange &RHSRange = *RHSRes;
  return ValueLatticeElement::getRange(OpFn(LHSRange, RHSRange));
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueBinaryOp(BinaryOperator *BO, BasicBlock *BB) {
  assert(BO->getOperand(0)->getType()->isSized() &&
         "all operands to binary operators are sized");
  if (auto *OBO = dyn_cast<OverflowingBinaryOperator>(BO)) {
    unsigned NoWrapKind = OBO->getNoWrapKind();
    return solveBlockValueBinaryOpImpl(
        BO, BB,
        [BO, NoWrapKind](const ConstantRange &CR1, const ConstantRange &CR2) {
          return CR1.overflowingBinaryOp(BO->getOpcode(), CR2, NoWrapKind);
        });
  }

  return solveBlockValueBinaryOpImpl(
      BO, BB, [BO](const ConstantRange &CR1, const ConstantRange &CR2) {
        return CR1.binaryOp(BO->getOpcode(), CR2);
      });
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueOverflowIntrinsic(WithOverflowInst *WO,
                                                    BasicBlock *BB) {
  return solveBlockValueBinaryOpImpl(
      WO, BB, [WO](const ConstantRange &CR1, const ConstantRange &CR2) {
        return CR1.binaryOp(WO->getBinaryOp(), CR2);
      });
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueIntrinsic(IntrinsicInst *II, BasicBlock *BB) {
  ValueLatticeElement MetadataVal = getFromRangeMetadata(II);
  if (!ConstantRange::isIntrinsicSupported(II->getIntrinsicID())) {
    LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                      << "' - unknown intrinsic.\n");
    return MetadataVal;
  }

  SmallVector<ConstantRange, 2> OpRanges;
  for (Value *Op : II->args()) {
    std::optional<ConstantRange> Range = getRangeFor(Op, II, BB);
    if (!Range)
      return std::nullopt;
    OpRanges.push_back(*Range);
  }

  return intersect(ValueLatticeElement::getRange(ConstantRange::intrinsic(
                       II->getIntrinsicID(), OpRanges)),
                   MetadataVal);
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueInsertElement(InsertElementInst *IEI,
                                                BasicBlock *BB) {
  std::optional<ValueLatticeElement> OptEltVal =
      getBlockValue(IEI->getOperand(1), BB, IEI);
  if (!OptEltVal)
    return std::nullopt;
  ValueLatticeElement &Res = *OptEltVal;

  std::optional<ValueLatticeElement> OptVecVal =
      getBlockValue(IEI->getOperand(0), BB, IEI);
  if (!OptVecVal)
    return std::nullopt;

  Res.mergeIn(*OptVecVal);
  return Res;
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::solveBlockValueExtractValue(ExtractValueInst *EVI,
                                               BasicBlock *BB) {
  if (auto *WO = dyn_cast<WithOverflowInst>(EVI->getAggregateOperand()))
    if (EVI->getNumIndices() == 1 && *EVI->idx_begin() == 0)
      return solveBlockValueOverflowIntrinsic(WO, BB);

  // Handle extractvalue of insertvalue to allow further simplification
  // based on replaced with.overflow intrinsics.
  if (Value *V = simplifyExtractValueInst(
          EVI->getAggregateOperand(), EVI->getIndices(),
          EVI->getDataLayout()))
    return getBlockValue(V, BB, EVI);

  LLVM_DEBUG(dbgs() << " compute BB '" << BB->getName()
                    << "' - overdefined (unknown extractvalue).\n");
  return ValueLatticeElement::getOverdefined();
}

static bool matchICmpOperand(APInt &Offset, Value *LHS, Value *Val,
                             ICmpInst::Predicate Pred) {
  if (LHS == Val)
    return true;

  // Handle range checking idiom produced by InstCombine. We will subtract the
  // offset from the allowed range for RHS in this case.
  const APInt *C;
  if (match(LHS, m_AddLike(m_Specific(Val), m_APInt(C)))) {
    Offset = *C;
    return true;
  }

  // Handle the symmetric case. This appears in saturation patterns like
  // (x == 16) ? 16 : (x + 1).
  if (match(Val, m_AddLike(m_Specific(LHS), m_APInt(C)))) {
    Offset = -*C;
    return true;
  }

  // If (x | y) < C, then (x < C) && (y < C).
  if (match(LHS, m_c_Or(m_Specific(Val), m_Value())) &&
      (Pred == ICmpInst::ICMP_ULT || Pred == ICmpInst::ICMP_ULE))
    return true;

  // If (x & y) > C, then (x > C) && (y > C).
  if (match(LHS, m_c_And(m_Specific(Val), m_Value())) &&
      (Pred == ICmpInst::ICMP_UGT || Pred == ICmpInst::ICMP_UGE))
    return true;

  return false;
}

/// Get value range for a "(Val + Offset) Pred RHS" condition.
std::optional<ValueLatticeElement>
LazyValueInfoImpl::getValueFromSimpleICmpCondition(CmpInst::Predicate Pred,
                                                   Value *RHS,
                                                   const APInt &Offset,
                                                   Instruction *CxtI,
                                                   bool UseBlockValue) {
  ConstantRange RHSRange(RHS->getType()->getScalarSizeInBits(),
                         /*isFullSet=*/true);
  if (ConstantInt *CI = dyn_cast<ConstantInt>(RHS)) {
    RHSRange = ConstantRange(CI->getValue());
  } else if (UseBlockValue) {
    std::optional<ValueLatticeElement> R =
        getBlockValue(RHS, CxtI->getParent(), CxtI);
    if (!R)
      return std::nullopt;
    RHSRange = R->asConstantRange(RHS->getType());
  }

  ConstantRange TrueValues =
      ConstantRange::makeAllowedICmpRegion(Pred, RHSRange);
  return ValueLatticeElement::getRange(TrueValues.subtract(Offset));
}

static std::optional<ConstantRange>
getRangeViaSLT(CmpInst::Predicate Pred, APInt RHS,
               function_ref<std::optional<ConstantRange>(const APInt &)> Fn) {
  bool Invert = false;
  if (Pred == ICmpInst::ICMP_SGT || Pred == ICmpInst::ICMP_SGE) {
    Pred = ICmpInst::getInversePredicate(Pred);
    Invert = true;
  }
  if (Pred == ICmpInst::ICMP_SLE) {
    Pred = ICmpInst::ICMP_SLT;
    if (RHS.isMaxSignedValue())
      return std::nullopt; // Could also return full/empty here, if we wanted.
    ++RHS;
  }
  assert(Pred == ICmpInst::ICMP_SLT && "Must be signed predicate");
  if (auto CR = Fn(RHS))
    return Invert ? CR->inverse() : CR;
  return std::nullopt;
}

std::optional<ValueLatticeElement> LazyValueInfoImpl::getValueFromICmpCondition(
    Value *Val, ICmpInst *ICI, bool isTrueDest, bool UseBlockValue) {
  Value *LHS = ICI->getOperand(0);
  Value *RHS = ICI->getOperand(1);

  // Get the predicate that must hold along the considered edge.
  CmpInst::Predicate EdgePred =
      isTrueDest ? ICI->getPredicate() : ICI->getInversePredicate();

  if (isa<Constant>(RHS)) {
    if (ICI->isEquality() && LHS == Val) {
      if (EdgePred == ICmpInst::ICMP_EQ)
        return ValueLatticeElement::get(cast<Constant>(RHS));
      else if (!isa<UndefValue>(RHS))
        return ValueLatticeElement::getNot(cast<Constant>(RHS));
    }
  }

  Type *Ty = Val->getType();
  if (!Ty->isIntegerTy())
    return ValueLatticeElement::getOverdefined();

  unsigned BitWidth = Ty->getScalarSizeInBits();
  APInt Offset(BitWidth, 0);
  if (matchICmpOperand(Offset, LHS, Val, EdgePred))
    return getValueFromSimpleICmpCondition(EdgePred, RHS, Offset, ICI,
                                           UseBlockValue);

  CmpInst::Predicate SwappedPred = CmpInst::getSwappedPredicate(EdgePred);
  if (matchICmpOperand(Offset, RHS, Val, SwappedPred))
    return getValueFromSimpleICmpCondition(SwappedPred, LHS, Offset, ICI,
                                           UseBlockValue);

  const APInt *Mask, *C;
  if (match(LHS, m_And(m_Specific(Val), m_APInt(Mask))) &&
      match(RHS, m_APInt(C))) {
    // If (Val & Mask) == C then all the masked bits are known and we can
    // compute a value range based on that.
    if (EdgePred == ICmpInst::ICMP_EQ) {
      KnownBits Known;
      Known.Zero = ~*C & *Mask;
      Known.One = *C & *Mask;
      return ValueLatticeElement::getRange(
          ConstantRange::fromKnownBits(Known, /*IsSigned*/ false));
    }

    if (EdgePred == ICmpInst::ICMP_NE)
      return ValueLatticeElement::getRange(
          ConstantRange::makeMaskNotEqualRange(*Mask, *C));
  }

  // If (X urem Modulus) >= C, then X >= C.
  // If trunc X >= C, then X >= C.
  // TODO: An upper bound could be computed as well.
  if (match(LHS, m_CombineOr(m_URem(m_Specific(Val), m_Value()),
                             m_Trunc(m_Specific(Val)))) &&
      match(RHS, m_APInt(C))) {
    // Use the icmp region so we don't have to deal with different predicates.
    ConstantRange CR = ConstantRange::makeExactICmpRegion(EdgePred, *C);
    if (!CR.isEmptySet())
      return ValueLatticeElement::getRange(ConstantRange::getNonEmpty(
          CR.getUnsignedMin().zext(BitWidth), APInt(BitWidth, 0)));
  }

  // Recognize:
  // icmp slt (ashr X, ShAmtC), C --> icmp slt X, C << ShAmtC
  // Preconditions: (C << ShAmtC) >> ShAmtC == C
  const APInt *ShAmtC;
  if (CmpInst::isSigned(EdgePred) &&
      match(LHS, m_AShr(m_Specific(Val), m_APInt(ShAmtC))) &&
      match(RHS, m_APInt(C))) {
    auto CR = getRangeViaSLT(
        EdgePred, *C, [&](const APInt &RHS) -> std::optional<ConstantRange> {
          APInt New = RHS << *ShAmtC;
          if ((New.ashr(*ShAmtC)) != RHS)
            return std::nullopt;
          return ConstantRange::getNonEmpty(
              APInt::getSignedMinValue(New.getBitWidth()), New);
        });
    if (CR)
      return ValueLatticeElement::getRange(*CR);
  }

  return ValueLatticeElement::getOverdefined();
}

// Handle conditions of the form
// extractvalue(op.with.overflow(%x, C), 1).
static ValueLatticeElement getValueFromOverflowCondition(
    Value *Val, WithOverflowInst *WO, bool IsTrueDest) {
  // TODO: This only works with a constant RHS for now. We could also compute
  // the range of the RHS, but this doesn't fit into the current structure of
  // the edge value calculation.
  const APInt *C;
  if (WO->getLHS() != Val || !match(WO->getRHS(), m_APInt(C)))
    return ValueLatticeElement::getOverdefined();

  // Calculate the possible values of %x for which no overflow occurs.
  ConstantRange NWR = ConstantRange::makeExactNoWrapRegion(
      WO->getBinaryOp(), *C, WO->getNoWrapKind());

  // If overflow is false, %x is constrained to NWR. If overflow is true, %x is
  // constrained to it's inverse (all values that might cause overflow).
  if (IsTrueDest)
    NWR = NWR.inverse();
  return ValueLatticeElement::getRange(NWR);
}

std::optional<ValueLatticeElement>
LazyValueInfoImpl::getValueFromCondition(Value *Val, Value *Cond,
                                         bool IsTrueDest, bool UseBlockValue,
                                         unsigned Depth) {
  if (ICmpInst *ICI = dyn_cast<ICmpInst>(Cond))
    return getValueFromICmpCondition(Val, ICI, IsTrueDest, UseBlockValue);

  if (auto *EVI = dyn_cast<ExtractValueInst>(Cond))
    if (auto *WO = dyn_cast<WithOverflowInst>(EVI->getAggregateOperand()))
      if (EVI->getNumIndices() == 1 && *EVI->idx_begin() == 1)
        return getValueFromOverflowCondition(Val, WO, IsTrueDest);

  if (++Depth == MaxAnalysisRecursionDepth)
    return ValueLatticeElement::getOverdefined();

  Value *N;
  if (match(Cond, m_Not(m_Value(N))))
    return getValueFromCondition(Val, N, !IsTrueDest, UseBlockValue, Depth);

  Value *L, *R;
  bool IsAnd;
  if (match(Cond, m_LogicalAnd(m_Value(L), m_Value(R))))
    IsAnd = true;
  else if (match(Cond, m_LogicalOr(m_Value(L), m_Value(R))))
    IsAnd = false;
  else
    return ValueLatticeElement::getOverdefined();

  std::optional<ValueLatticeElement> LV =
      getValueFromCondition(Val, L, IsTrueDest, UseBlockValue, Depth);
  if (!LV)
    return std::nullopt;
  std::optional<ValueLatticeElement> RV =
      getValueFromCondition(Val, R, IsTrueDest, UseBlockValue, Depth);
  if (!RV)
    return std::nullopt;

  // if (L && R) -> intersect L and R
  // if (!(L || R)) -> intersect !L and !R
  // if (L || R) -> union L and R
  // if (!(L && R)) -> union !L and !R
  if (IsTrueDest ^ IsAnd) {
    LV->mergeIn(*RV);
    return *LV;
  }

  return intersect(*LV, *RV);
}

// Return true if Usr has Op as an operand, otherwise false.
static bool usesOperand(User *Usr, Value *Op) {
  return is_contained(Usr->operands(), Op);
}

// Return true if the instruction type of Val is supported by
// constantFoldUser(). Currently CastInst, BinaryOperator and FreezeInst only.
// Call this before calling constantFoldUser() to find out if it's even worth
// attempting to call it.
static bool isOperationFoldable(User *Usr) {
  return isa<CastInst>(Usr) || isa<BinaryOperator>(Usr) || isa<FreezeInst>(Usr);
}

// Check if Usr can be simplified to an integer constant when the value of one
// of its operands Op is an integer constant OpConstVal. If so, return it as an
// lattice value range with a single element or otherwise return an overdefined
// lattice value.
static ValueLatticeElement constantFoldUser(User *Usr, Value *Op,
                                            const APInt &OpConstVal,
                                            const DataLayout &DL) {
  assert(isOperationFoldable(Usr) && "Precondition");
  Constant* OpConst = Constant::getIntegerValue(Op->getType(), OpConstVal);
  // Check if Usr can be simplified to a constant.
  if (auto *CI = dyn_cast<CastInst>(Usr)) {
    assert(CI->getOperand(0) == Op && "Operand 0 isn't Op");
    if (auto *C = dyn_cast_or_null<ConstantInt>(
            simplifyCastInst(CI->getOpcode(), OpConst,
                             CI->getDestTy(), DL))) {
      return ValueLatticeElement::getRange(ConstantRange(C->getValue()));
    }
  } else if (auto *BO = dyn_cast<BinaryOperator>(Usr)) {
    bool Op0Match = BO->getOperand(0) == Op;
    bool Op1Match = BO->getOperand(1) == Op;
    assert((Op0Match || Op1Match) &&
           "Operand 0 nor Operand 1 isn't a match");
    Value *LHS = Op0Match ? OpConst : BO->getOperand(0);
    Value *RHS = Op1Match ? OpConst : BO->getOperand(1);
    if (auto *C = dyn_cast_or_null<ConstantInt>(
            simplifyBinOp(BO->getOpcode(), LHS, RHS, DL))) {
      return ValueLatticeElement::getRange(ConstantRange(C->getValue()));
    }
  } else if (isa<FreezeInst>(Usr)) {
    assert(cast<FreezeInst>(Usr)->getOperand(0) == Op && "Operand 0 isn't Op");
    return ValueLatticeElement::getRange(ConstantRange(OpConstVal));
  }
  return ValueLatticeElement::getOverdefined();
}

/// Compute the value of Val on the edge BBFrom -> BBTo.
std::optional<ValueLatticeElement>
LazyValueInfoImpl::getEdgeValueLocal(Value *Val, BasicBlock *BBFrom,
                                     BasicBlock *BBTo, bool UseBlockValue) {
  // TODO: Handle more complex conditionals. If (v == 0 || v2 < 1) is false, we
  // know that v != 0.
  if (BranchInst *BI = dyn_cast<BranchInst>(BBFrom->getTerminator())) {
    // If this is a conditional branch and only one successor goes to BBTo, then
    // we may be able to infer something from the condition.
    if (BI->isConditional() &&
        BI->getSuccessor(0) != BI->getSuccessor(1)) {
      bool isTrueDest = BI->getSuccessor(0) == BBTo;
      assert(BI->getSuccessor(!isTrueDest) == BBTo &&
             "BBTo isn't a successor of BBFrom");
      Value *Condition = BI->getCondition();

      // If V is the condition of the branch itself, then we know exactly what
      // it is.
      // NB: The condition on a `br` can't be a vector type.
      if (Condition == Val)
        return ValueLatticeElement::get(ConstantInt::get(
                              Type::getInt1Ty(Val->getContext()), isTrueDest));

      // If the condition of the branch is an equality comparison, we may be
      // able to infer the value.
      std::optional<ValueLatticeElement> Result =
          getValueFromCondition(Val, Condition, isTrueDest, UseBlockValue);
      if (!Result)
        return std::nullopt;

      if (!Result->isOverdefined())
        return Result;

      if (User *Usr = dyn_cast<User>(Val)) {
        assert(Result->isOverdefined() && "Result isn't overdefined");
        // Check with isOperationFoldable() first to avoid linearly iterating
        // over the operands unnecessarily which can be expensive for
        // instructions with many operands.
        if (isa<IntegerType>(Usr->getType()) && isOperationFoldable(Usr)) {
          const DataLayout &DL = BBTo->getDataLayout();
          if (usesOperand(Usr, Condition)) {
            // If Val has Condition as an operand and Val can be folded into a
            // constant with either Condition == true or Condition == false,
            // propagate the constant.
            // eg.
            //   ; %Val is true on the edge to %then.
            //   %Val = and i1 %Condition, true.
            //   br %Condition, label %then, label %else
            APInt ConditionVal(1, isTrueDest ? 1 : 0);
            Result = constantFoldUser(Usr, Condition, ConditionVal, DL);
          } else {
            // If one of Val's operand has an inferred value, we may be able to
            // infer the value of Val.
            // eg.
            //    ; %Val is 94 on the edge to %then.
            //    %Val = add i8 %Op, 1
            //    %Condition = icmp eq i8 %Op, 93
            //    br i1 %Condition, label %then, label %else
            for (unsigned i = 0; i < Usr->getNumOperands(); ++i) {
              Value *Op = Usr->getOperand(i);
              ValueLatticeElement OpLatticeVal = *getValueFromCondition(
                  Op, Condition, isTrueDest, /*UseBlockValue*/ false);
              if (std::optional<APInt> OpConst =
                      OpLatticeVal.asConstantInteger()) {
                Result = constantFoldUser(Usr, Op, *OpConst, DL);
                break;
              }
            }
          }
        }
      }
      if (!Result->isOverdefined())
        return Result;
    }
  }

  // If the edge was formed by a switch on the value, then we may know exactly
  // what it is.
  if (SwitchInst *SI = dyn_cast<SwitchInst>(BBFrom->getTerminator())) {
    Value *Condition = SI->getCondition();
    if (!isa<IntegerType>(Val->getType()))
      return ValueLatticeElement::getOverdefined();
    bool ValUsesConditionAndMayBeFoldable = false;
    if (Condition != Val) {
      // Check if Val has Condition as an operand.
      if (User *Usr = dyn_cast<User>(Val))
        ValUsesConditionAndMayBeFoldable = isOperationFoldable(Usr) &&
            usesOperand(Usr, Condition);
      if (!ValUsesConditionAndMayBeFoldable)
        return ValueLatticeElement::getOverdefined();
    }
    assert((Condition == Val || ValUsesConditionAndMayBeFoldable) &&
           "Condition != Val nor Val doesn't use Condition");

    bool DefaultCase = SI->getDefaultDest() == BBTo;
    unsigned BitWidth = Val->getType()->getIntegerBitWidth();
    ConstantRange EdgesVals(BitWidth, DefaultCase/*isFullSet*/);

    for (auto Case : SI->cases()) {
      APInt CaseValue = Case.getCaseValue()->getValue();
      ConstantRange EdgeVal(CaseValue);
      if (ValUsesConditionAndMayBeFoldable) {
        User *Usr = cast<User>(Val);
        const DataLayout &DL = BBTo->getDataLayout();
        ValueLatticeElement EdgeLatticeVal =
            constantFoldUser(Usr, Condition, CaseValue, DL);
        if (EdgeLatticeVal.isOverdefined())
          return ValueLatticeElement::getOverdefined();
        EdgeVal = EdgeLatticeVal.getConstantRange();
      }
      if (DefaultCase) {
        // It is possible that the default destination is the destination of
        // some cases. We cannot perform difference for those cases.
        // We know Condition != CaseValue in BBTo.  In some cases we can use
        // this to infer Val == f(Condition) is != f(CaseValue).  For now, we
        // only do this when f is identity (i.e. Val == Condition), but we
        // should be able to do this for any injective f.
        if (Case.getCaseSuccessor() != BBTo && Condition == Val)
          EdgesVals = EdgesVals.difference(EdgeVal);
      } else if (Case.getCaseSuccessor() == BBTo)
        EdgesVals = EdgesVals.unionWith(EdgeVal);
    }
    return ValueLatticeElement::getRange(std::move(EdgesVals));
  }
  return ValueLatticeElement::getOverdefined();
}

/// Compute the value of Val on the edge BBFrom -> BBTo or the value at
/// the basic block if the edge does not constrain Val.
std::optional<ValueLatticeElement>
LazyValueInfoImpl::getEdgeValue(Value *Val, BasicBlock *BBFrom,
                                BasicBlock *BBTo, Instruction *CxtI) {
  // If already a constant, there is nothing to compute.
  if (Constant *VC = dyn_cast<Constant>(Val))
    return ValueLatticeElement::get(VC);

  std::optional<ValueLatticeElement> LocalResult =
      getEdgeValueLocal(Val, BBFrom, BBTo, /*UseBlockValue*/ true);
  if (!LocalResult)
    return std::nullopt;

  if (hasSingleValue(*LocalResult))
    // Can't get any more precise here
    return LocalResult;

  std::optional<ValueLatticeElement> OptInBlock =
      getBlockValue(Val, BBFrom, BBFrom->getTerminator());
  if (!OptInBlock)
    return std::nullopt;
  ValueLatticeElement &InBlock = *OptInBlock;

  // We can use the context instruction (generically the ultimate instruction
  // the calling pass is trying to simplify) here, even though the result of
  // this function is generally cached when called from the solve* functions
  // (and that cached result might be used with queries using a different
  // context instruction), because when this function is called from the solve*
  // functions, the context instruction is not provided. When called from
  // LazyValueInfoImpl::getValueOnEdge, the context instruction is provided,
  // but then the result is not cached.
  intersectAssumeOrGuardBlockValueConstantRange(Val, InBlock, CxtI);

  return intersect(*LocalResult, InBlock);
}

ValueLatticeElement LazyValueInfoImpl::getValueInBlock(Value *V, BasicBlock *BB,
                                                       Instruction *CxtI) {
  LLVM_DEBUG(dbgs() << "LVI Getting block end value " << *V << " at '"
                    << BB->getName() << "'\n");

  assert(BlockValueStack.empty() && BlockValueSet.empty());
  std::optional<ValueLatticeElement> OptResult = getBlockValue(V, BB, CxtI);
  if (!OptResult) {
    solve();
    OptResult = getBlockValue(V, BB, CxtI);
    assert(OptResult && "Value not available after solving");
  }

  ValueLatticeElement Result = *OptResult;
  LLVM_DEBUG(dbgs() << "  Result = " << Result << "\n");
  return Result;
}

ValueLatticeElement LazyValueInfoImpl::getValueAt(Value *V, Instruction *CxtI) {
  LLVM_DEBUG(dbgs() << "LVI Getting value " << *V << " at '" << CxtI->getName()
                    << "'\n");

  if (auto *C = dyn_cast<Constant>(V))
    return ValueLatticeElement::get(C);

  ValueLatticeElement Result = ValueLatticeElement::getOverdefined();
  if (auto *I = dyn_cast<Instruction>(V))
    Result = getFromRangeMetadata(I);
  intersectAssumeOrGuardBlockValueConstantRange(V, Result, CxtI);

  LLVM_DEBUG(dbgs() << "  Result = " << Result << "\n");
  return Result;
}

ValueLatticeElement LazyValueInfoImpl::
getValueOnEdge(Value *V, BasicBlock *FromBB, BasicBlock *ToBB,
               Instruction *CxtI) {
  LLVM_DEBUG(dbgs() << "LVI Getting edge value " << *V << " from '"
                    << FromBB->getName() << "' to '" << ToBB->getName()
                    << "'\n");

  std::optional<ValueLatticeElement> Result =
      getEdgeValue(V, FromBB, ToBB, CxtI);
  while (!Result) {
    // As the worklist only explicitly tracks block values (but not edge values)
    // we may have to call solve() multiple times, as the edge value calculation
    // may request additional block values.
    solve();
    Result = getEdgeValue(V, FromBB, ToBB, CxtI);
  }

  LLVM_DEBUG(dbgs() << "  Result = " << *Result << "\n");
  return *Result;
}

ValueLatticeElement LazyValueInfoImpl::getValueAtUse(const Use &U) {
  Value *V = U.get();
  auto *CxtI = cast<Instruction>(U.getUser());
  ValueLatticeElement VL = getValueInBlock(V, CxtI->getParent(), CxtI);

  // Check whether the only (possibly transitive) use of the value is in a
  // position where V can be constrained by a select or branch condition.
  const Use *CurrU = &U;
  // TODO: Increase limit?
  const unsigned MaxUsesToInspect = 3;
  for (unsigned I = 0; I < MaxUsesToInspect; ++I) {
    std::optional<ValueLatticeElement> CondVal;
    auto *CurrI = cast<Instruction>(CurrU->getUser());
    if (auto *SI = dyn_cast<SelectInst>(CurrI)) {
      // If the value is undef, a different value may be chosen in
      // the select condition and at use.
      if (!isGuaranteedNotToBeUndef(SI->getCondition(), AC))
        break;
      if (CurrU->getOperandNo() == 1)
        CondVal =
            *getValueFromCondition(V, SI->getCondition(), /*IsTrueDest*/ true,
                                   /*UseBlockValue*/ false);
      else if (CurrU->getOperandNo() == 2)
        CondVal =
            *getValueFromCondition(V, SI->getCondition(), /*IsTrueDest*/ false,
                                   /*UseBlockValue*/ false);
    } else if (auto *PHI = dyn_cast<PHINode>(CurrI)) {
      // TODO: Use non-local query?
      CondVal = *getEdgeValueLocal(V, PHI->getIncomingBlock(*CurrU),
                                   PHI->getParent(), /*UseBlockValue*/ false);
    }
    if (CondVal)
      VL = intersect(VL, *CondVal);

    // Only follow one-use chain, to allow direct intersection of conditions.
    // If there are multiple uses, we would have to intersect with the union of
    // all conditions at different uses.
    // Stop walking if we hit a non-speculatable instruction. Even if the
    // result is only used under a specific condition, executing the
    // instruction itself may cause side effects or UB already.
    // This also disallows looking through phi nodes: If the phi node is part
    // of a cycle, we might end up reasoning about values from different cycle
    // iterations (PR60629).
    if (!CurrI->hasOneUse() || !isSafeToSpeculativelyExecute(CurrI))
      break;
    CurrU = &*CurrI->use_begin();
  }
  return VL;
}

void LazyValueInfoImpl::threadEdge(BasicBlock *PredBB, BasicBlock *OldSucc,
                                   BasicBlock *NewSucc) {
  TheCache.threadEdgeImpl(OldSucc, NewSucc);
}

//===----------------------------------------------------------------------===//
//                            LazyValueInfo Impl
//===----------------------------------------------------------------------===//

bool LazyValueInfoWrapperPass::runOnFunction(Function &F) {
  Info.AC = &getAnalysis<AssumptionCacheTracker>().getAssumptionCache(F);

  if (auto *Impl = Info.getImpl())
    Impl->clear();

  // Fully lazy.
  return false;
}

void LazyValueInfoWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<AssumptionCacheTracker>();
  AU.addRequired<TargetLibraryInfoWrapperPass>();
}

LazyValueInfo &LazyValueInfoWrapperPass::getLVI() { return Info; }

/// This lazily constructs the LazyValueInfoImpl.
LazyValueInfoImpl &LazyValueInfo::getOrCreateImpl(const Module *M) {
  if (!PImpl) {
    assert(M && "getCache() called with a null Module");
    const DataLayout &DL = M->getDataLayout();
    Function *GuardDecl =
        M->getFunction(Intrinsic::getName(Intrinsic::experimental_guard));
    PImpl = new LazyValueInfoImpl(AC, DL, GuardDecl);
  }
  return *static_cast<LazyValueInfoImpl *>(PImpl);
}

LazyValueInfoImpl *LazyValueInfo::getImpl() {
  if (!PImpl)
    return nullptr;
  return static_cast<LazyValueInfoImpl *>(PImpl);
}

LazyValueInfo::~LazyValueInfo() { releaseMemory(); }

void LazyValueInfo::releaseMemory() {
  // If the cache was allocated, free it.
  if (auto *Impl = getImpl()) {
    delete &*Impl;
    PImpl = nullptr;
  }
}

bool LazyValueInfo::invalidate(Function &F, const PreservedAnalyses &PA,
                               FunctionAnalysisManager::Invalidator &Inv) {
  // We need to invalidate if we have either failed to preserve this analyses
  // result directly or if any of its dependencies have been invalidated.
  auto PAC = PA.getChecker<LazyValueAnalysis>();
  if (!(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()))
    return true;

  return false;
}

void LazyValueInfoWrapperPass::releaseMemory() { Info.releaseMemory(); }

LazyValueInfo LazyValueAnalysis::run(Function &F,
                                     FunctionAnalysisManager &FAM) {
  auto &AC = FAM.getResult<AssumptionAnalysis>(F);

  return LazyValueInfo(&AC, &F.getDataLayout());
}

/// Returns true if we can statically tell that this value will never be a
/// "useful" constant.  In practice, this means we've got something like an
/// alloca or a malloc call for which a comparison against a constant can
/// only be guarding dead code.  Note that we are potentially giving up some
/// precision in dead code (a constant result) in favour of avoiding a
/// expensive search for a easily answered common query.
static bool isKnownNonConstant(Value *V) {
  V = V->stripPointerCasts();
  // The return val of alloc cannot be a Constant.
  if (isa<AllocaInst>(V))
    return true;
  return false;
}

Constant *LazyValueInfo::getConstant(Value *V, Instruction *CxtI) {
  // Bail out early if V is known not to be a Constant.
  if (isKnownNonConstant(V))
    return nullptr;

  BasicBlock *BB = CxtI->getParent();
  ValueLatticeElement Result =
      getOrCreateImpl(BB->getModule()).getValueInBlock(V, BB, CxtI);

  if (Result.isConstant())
    return Result.getConstant();
  if (Result.isConstantRange()) {
    const ConstantRange &CR = Result.getConstantRange();
    if (const APInt *SingleVal = CR.getSingleElement())
      return ConstantInt::get(V->getType(), *SingleVal);
  }
  return nullptr;
}

ConstantRange LazyValueInfo::getConstantRange(Value *V, Instruction *CxtI,
                                              bool UndefAllowed) {
  BasicBlock *BB = CxtI->getParent();
  ValueLatticeElement Result =
      getOrCreateImpl(BB->getModule()).getValueInBlock(V, BB, CxtI);
  return Result.asConstantRange(V->getType(), UndefAllowed);
}

ConstantRange LazyValueInfo::getConstantRangeAtUse(const Use &U,
                                                   bool UndefAllowed) {
  auto *Inst = cast<Instruction>(U.getUser());
  ValueLatticeElement Result =
      getOrCreateImpl(Inst->getModule()).getValueAtUse(U);
  return Result.asConstantRange(U->getType(), UndefAllowed);
}

/// Determine whether the specified value is known to be a
/// constant on the specified edge. Return null if not.
Constant *LazyValueInfo::getConstantOnEdge(Value *V, BasicBlock *FromBB,
                                           BasicBlock *ToBB,
                                           Instruction *CxtI) {
  Module *M = FromBB->getModule();
  ValueLatticeElement Result =
      getOrCreateImpl(M).getValueOnEdge(V, FromBB, ToBB, CxtI);

  if (Result.isConstant())
    return Result.getConstant();
  if (Result.isConstantRange()) {
    const ConstantRange &CR = Result.getConstantRange();
    if (const APInt *SingleVal = CR.getSingleElement())
      return ConstantInt::get(V->getType(), *SingleVal);
  }
  return nullptr;
}

ConstantRange LazyValueInfo::getConstantRangeOnEdge(Value *V,
                                                    BasicBlock *FromBB,
                                                    BasicBlock *ToBB,
                                                    Instruction *CxtI) {
  Module *M = FromBB->getModule();
  ValueLatticeElement Result =
      getOrCreateImpl(M).getValueOnEdge(V, FromBB, ToBB, CxtI);
  // TODO: Should undef be allowed here?
  return Result.asConstantRange(V->getType(), /*UndefAllowed*/ true);
}

static Constant *getPredicateResult(CmpInst::Predicate Pred, Constant *C,
                                    const ValueLatticeElement &Val,
                                    const DataLayout &DL) {
  // If we know the value is a constant, evaluate the conditional.
  if (Val.isConstant())
    return ConstantFoldCompareInstOperands(Pred, Val.getConstant(), C, DL);

  Type *ResTy = CmpInst::makeCmpResultType(C->getType());
  if (Val.isConstantRange()) {
    const ConstantRange &CR = Val.getConstantRange();
    ConstantRange RHS = C->toConstantRange();
    if (CR.icmp(Pred, RHS))
      return ConstantInt::getTrue(ResTy);
    if (CR.icmp(CmpInst::getInversePredicate(Pred), RHS))
      return ConstantInt::getFalse(ResTy);
    return nullptr;
  }

  if (Val.isNotConstant()) {
    // If this is an equality comparison, we can try to fold it knowing that
    // "V != C1".
    if (Pred == ICmpInst::ICMP_EQ) {
      // !C1 == C -> false iff C1 == C.
      Constant *Res = ConstantFoldCompareInstOperands(
          ICmpInst::ICMP_NE, Val.getNotConstant(), C, DL);
      if (Res && Res->isNullValue())
        return ConstantInt::getFalse(ResTy);
    } else if (Pred == ICmpInst::ICMP_NE) {
      // !C1 != C -> true iff C1 == C.
      Constant *Res = ConstantFoldCompareInstOperands(
          ICmpInst::ICMP_NE, Val.getNotConstant(), C, DL);
      if (Res && Res->isNullValue())
        return ConstantInt::getTrue(ResTy);
    }
    return nullptr;
  }

  return nullptr;
}

/// Determine whether the specified value comparison with a constant is known to
/// be true or false on the specified CFG edge. Pred is a CmpInst predicate.
Constant *LazyValueInfo::getPredicateOnEdge(CmpInst::Predicate Pred, Value *V,
                                            Constant *C, BasicBlock *FromBB,
                                            BasicBlock *ToBB,
                                            Instruction *CxtI) {
  Module *M = FromBB->getModule();
  ValueLatticeElement Result =
      getOrCreateImpl(M).getValueOnEdge(V, FromBB, ToBB, CxtI);

  return getPredicateResult(Pred, C, Result, M->getDataLayout());
}

Constant *LazyValueInfo::getPredicateAt(CmpInst::Predicate Pred, Value *V,
                                        Constant *C, Instruction *CxtI,
                                        bool UseBlockValue) {
  // Is or is not NonNull are common predicates being queried. If
  // isKnownNonZero can tell us the result of the predicate, we can
  // return it quickly. But this is only a fastpath, and falling
  // through would still be correct.
  Module *M = CxtI->getModule();
  const DataLayout &DL = M->getDataLayout();
  if (V->getType()->isPointerTy() && C->isNullValue() &&
      isKnownNonZero(V->stripPointerCastsSameRepresentation(), DL)) {
    Type *ResTy = CmpInst::makeCmpResultType(C->getType());
    if (Pred == ICmpInst::ICMP_EQ)
      return ConstantInt::getFalse(ResTy);
    else if (Pred == ICmpInst::ICMP_NE)
      return ConstantInt::getTrue(ResTy);
  }

  auto &Impl = getOrCreateImpl(M);
  ValueLatticeElement Result =
      UseBlockValue ? Impl.getValueInBlock(V, CxtI->getParent(), CxtI)
                    : Impl.getValueAt(V, CxtI);
  Constant *Ret = getPredicateResult(Pred, C, Result, DL);
  if (Ret)
    return Ret;

  // Note: The following bit of code is somewhat distinct from the rest of LVI;
  // LVI as a whole tries to compute a lattice value which is conservatively
  // correct at a given location.  In this case, we have a predicate which we
  // weren't able to prove about the merged result, and we're pushing that
  // predicate back along each incoming edge to see if we can prove it
  // separately for each input.  As a motivating example, consider:
  // bb1:
  //   %v1 = ... ; constantrange<1, 5>
  //   br label %merge
  // bb2:
  //   %v2 = ... ; constantrange<10, 20>
  //   br label %merge
  // merge:
  //   %phi = phi [%v1, %v2] ; constantrange<1,20>
  //   %pred = icmp eq i32 %phi, 8
  // We can't tell from the lattice value for '%phi' that '%pred' is false
  // along each path, but by checking the predicate over each input separately,
  // we can.
  // We limit the search to one step backwards from the current BB and value.
  // We could consider extending this to search further backwards through the
  // CFG and/or value graph, but there are non-obvious compile time vs quality
  // tradeoffs.
  BasicBlock *BB = CxtI->getParent();

  // Function entry or an unreachable block.  Bail to avoid confusing
  // analysis below.
  pred_iterator PI = pred_begin(BB), PE = pred_end(BB);
  if (PI == PE)
    return nullptr;

  // If V is a PHI node in the same block as the context, we need to ask
  // questions about the predicate as applied to the incoming value along
  // each edge. This is useful for eliminating cases where the predicate is
  // known along all incoming edges.
  if (auto *PHI = dyn_cast<PHINode>(V))
    if (PHI->getParent() == BB) {
      Constant *Baseline = nullptr;
      for (unsigned i = 0, e = PHI->getNumIncomingValues(); i < e; i++) {
        Value *Incoming = PHI->getIncomingValue(i);
        BasicBlock *PredBB = PHI->getIncomingBlock(i);
        // Note that PredBB may be BB itself.
        Constant *Result =
            getPredicateOnEdge(Pred, Incoming, C, PredBB, BB, CxtI);

        // Keep going as long as we've seen a consistent known result for
        // all inputs.
        Baseline = (i == 0) ? Result /* First iteration */
                            : (Baseline == Result ? Baseline
                                                  : nullptr); /* All others */
        if (!Baseline)
          break;
      }
      if (Baseline)
        return Baseline;
    }

  // For a comparison where the V is outside this block, it's possible
  // that we've branched on it before. Look to see if the value is known
  // on all incoming edges.
  if (!isa<Instruction>(V) || cast<Instruction>(V)->getParent() != BB) {
    // For predecessor edge, determine if the comparison is true or false
    // on that edge. If they're all true or all false, we can conclude
    // the value of the comparison in this block.
    Constant *Baseline = getPredicateOnEdge(Pred, V, C, *PI, BB, CxtI);
    if (Baseline) {
      // Check that all remaining incoming values match the first one.
      while (++PI != PE) {
        Constant *Ret = getPredicateOnEdge(Pred, V, C, *PI, BB, CxtI);
        if (Ret != Baseline)
          break;
      }
      // If we terminated early, then one of the values didn't match.
      if (PI == PE) {
        return Baseline;
      }
    }
  }

  return nullptr;
}

Constant *LazyValueInfo::getPredicateAt(CmpInst::Predicate Pred, Value *LHS,
                                        Value *RHS, Instruction *CxtI,
                                        bool UseBlockValue) {
  if (auto *C = dyn_cast<Constant>(RHS))
    return getPredicateAt(Pred, LHS, C, CxtI, UseBlockValue);
  if (auto *C = dyn_cast<Constant>(LHS))
    return getPredicateAt(CmpInst::getSwappedPredicate(Pred), RHS, C, CxtI,
                          UseBlockValue);

  // Got two non-Constant values. Try to determine the comparison results based
  // on the block values of the two operands, e.g. because they have
  // non-overlapping ranges.
  if (UseBlockValue) {
    Module *M = CxtI->getModule();
    ValueLatticeElement L =
        getOrCreateImpl(M).getValueInBlock(LHS, CxtI->getParent(), CxtI);
    if (L.isOverdefined())
      return nullptr;

    ValueLatticeElement R =
        getOrCreateImpl(M).getValueInBlock(RHS, CxtI->getParent(), CxtI);
    Type *Ty = CmpInst::makeCmpResultType(LHS->getType());
    return L.getCompare(Pred, Ty, R, M->getDataLayout());
  }
  return nullptr;
}

void LazyValueInfo::threadEdge(BasicBlock *PredBB, BasicBlock *OldSucc,
                               BasicBlock *NewSucc) {
  if (auto *Impl = getImpl())
    Impl->threadEdge(PredBB, OldSucc, NewSucc);
}

void LazyValueInfo::forgetValue(Value *V) {
  if (auto *Impl = getImpl())
    Impl->forgetValue(V);
}

void LazyValueInfo::eraseBlock(BasicBlock *BB) {
  if (auto *Impl = getImpl())
    Impl->eraseBlock(BB);
}

void LazyValueInfo::clear() {
  if (auto *Impl = getImpl())
    Impl->clear();
}

void LazyValueInfo::printLVI(Function &F, DominatorTree &DTree, raw_ostream &OS) {
  if (auto *Impl = getImpl())
    Impl->printLVI(F, DTree, OS);
}

// Print the LVI for the function arguments at the start of each basic block.
void LazyValueInfoAnnotatedWriter::emitBasicBlockStartAnnot(
    const BasicBlock *BB, formatted_raw_ostream &OS) {
  // Find if there are latticevalues defined for arguments of the function.
  auto *F = BB->getParent();
  for (const auto &Arg : F->args()) {
    ValueLatticeElement Result = LVIImpl->getValueInBlock(
        const_cast<Argument *>(&Arg), const_cast<BasicBlock *>(BB));
    if (Result.isUnknown())
      continue;
    OS << "; LatticeVal for: '" << Arg << "' is: " << Result << "\n";
  }
}

// This function prints the LVI analysis for the instruction I at the beginning
// of various basic blocks. It relies on calculated values that are stored in
// the LazyValueInfoCache, and in the absence of cached values, recalculate the
// LazyValueInfo for `I`, and print that info.
void LazyValueInfoAnnotatedWriter::emitInstructionAnnot(
    const Instruction *I, formatted_raw_ostream &OS) {

  auto *ParentBB = I->getParent();
  SmallPtrSet<const BasicBlock*, 16> BlocksContainingLVI;
  // We can generate (solve) LVI values only for blocks that are dominated by
  // the I's parent. However, to avoid generating LVI for all dominating blocks,
  // that contain redundant/uninteresting information, we print LVI for
  // blocks that may use this LVI information (such as immediate successor
  // blocks, and blocks that contain uses of `I`).
  auto printResult = [&](const BasicBlock *BB) {
    if (!BlocksContainingLVI.insert(BB).second)
      return;
    ValueLatticeElement Result = LVIImpl->getValueInBlock(
        const_cast<Instruction *>(I), const_cast<BasicBlock *>(BB));
      OS << "; LatticeVal for: '" << *I << "' in BB: '";
      BB->printAsOperand(OS, false);
      OS << "' is: " << Result << "\n";
  };

  printResult(ParentBB);
  // Print the LVI analysis results for the immediate successor blocks, that
  // are dominated by `ParentBB`.
  for (const auto *BBSucc : successors(ParentBB))
    if (DT.dominates(ParentBB, BBSucc))
      printResult(BBSucc);

  // Print LVI in blocks where `I` is used.
  for (const auto *U : I->users())
    if (auto *UseI = dyn_cast<Instruction>(U))
      if (!isa<PHINode>(UseI) || DT.dominates(ParentBB, UseI->getParent()))
        printResult(UseI->getParent());

}

PreservedAnalyses LazyValueInfoPrinterPass::run(Function &F,
                                                FunctionAnalysisManager &AM) {
  OS << "LVI for function '" << F.getName() << "':\n";
  auto &LVI = AM.getResult<LazyValueAnalysis>(F);
  auto &DTree = AM.getResult<DominatorTreeAnalysis>(F);
  LVI.printLVI(F, DTree, OS);
  return PreservedAnalyses::all();
}
