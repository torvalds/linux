//===- MemorySSA.cpp - Memory SSA Builder ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MemorySSA class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/MemorySSA.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/Analysis/IteratedDominanceFrontier.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Use.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iterator>
#include <memory>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "memoryssa"

static cl::opt<std::string>
    DotCFGMSSA("dot-cfg-mssa",
               cl::value_desc("file name for generated dot file"),
               cl::desc("file name for generated dot file"), cl::init(""));

INITIALIZE_PASS_BEGIN(MemorySSAWrapperPass, "memoryssa", "Memory SSA", false,
                      true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_END(MemorySSAWrapperPass, "memoryssa", "Memory SSA", false,
                    true)

static cl::opt<unsigned> MaxCheckLimit(
    "memssa-check-limit", cl::Hidden, cl::init(100),
    cl::desc("The maximum number of stores/phis MemorySSA"
             "will consider trying to walk past (default = 100)"));

// Always verify MemorySSA if expensive checking is enabled.
#ifdef EXPENSIVE_CHECKS
bool llvm::VerifyMemorySSA = true;
#else
bool llvm::VerifyMemorySSA = false;
#endif

static cl::opt<bool, true>
    VerifyMemorySSAX("verify-memoryssa", cl::location(VerifyMemorySSA),
                     cl::Hidden, cl::desc("Enable verification of MemorySSA."));

const static char LiveOnEntryStr[] = "liveOnEntry";

namespace {

/// An assembly annotator class to print Memory SSA information in
/// comments.
class MemorySSAAnnotatedWriter : public AssemblyAnnotationWriter {
  const MemorySSA *MSSA;

public:
  MemorySSAAnnotatedWriter(const MemorySSA *M) : MSSA(M) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(BB))
      OS << "; " << *MA << "\n";
  }

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I))
      OS << "; " << *MA << "\n";
  }
};

/// An assembly annotator class to print Memory SSA information in
/// comments.
class MemorySSAWalkerAnnotatedWriter : public AssemblyAnnotationWriter {
  MemorySSA *MSSA;
  MemorySSAWalker *Walker;
  BatchAAResults BAA;

public:
  MemorySSAWalkerAnnotatedWriter(MemorySSA *M)
      : MSSA(M), Walker(M->getWalker()), BAA(M->getAA()) {}

  void emitBasicBlockStartAnnot(const BasicBlock *BB,
                                formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(BB))
      OS << "; " << *MA << "\n";
  }

  void emitInstructionAnnot(const Instruction *I,
                            formatted_raw_ostream &OS) override {
    if (MemoryAccess *MA = MSSA->getMemoryAccess(I)) {
      MemoryAccess *Clobber = Walker->getClobberingMemoryAccess(MA, BAA);
      OS << "; " << *MA;
      if (Clobber) {
        OS << " - clobbered by ";
        if (MSSA->isLiveOnEntryDef(Clobber))
          OS << LiveOnEntryStr;
        else
          OS << *Clobber;
      }
      OS << "\n";
    }
  }
};

} // namespace

namespace {

/// Our current alias analysis API differentiates heavily between calls and
/// non-calls, and functions called on one usually assert on the other.
/// This class encapsulates the distinction to simplify other code that wants
/// "Memory affecting instructions and related data" to use as a key.
/// For example, this class is used as a densemap key in the use optimizer.
class MemoryLocOrCall {
public:
  bool IsCall = false;

  MemoryLocOrCall(MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}
  MemoryLocOrCall(const MemoryUseOrDef *MUD)
      : MemoryLocOrCall(MUD->getMemoryInst()) {}

  MemoryLocOrCall(Instruction *Inst) {
    if (auto *C = dyn_cast<CallBase>(Inst)) {
      IsCall = true;
      Call = C;
    } else {
      IsCall = false;
      // There is no such thing as a memorylocation for a fence inst, and it is
      // unique in that regard.
      if (!isa<FenceInst>(Inst))
        Loc = MemoryLocation::get(Inst);
    }
  }

  explicit MemoryLocOrCall(const MemoryLocation &Loc) : Loc(Loc) {}

  const CallBase *getCall() const {
    assert(IsCall);
    return Call;
  }

  MemoryLocation getLoc() const {
    assert(!IsCall);
    return Loc;
  }

  bool operator==(const MemoryLocOrCall &Other) const {
    if (IsCall != Other.IsCall)
      return false;

    if (!IsCall)
      return Loc == Other.Loc;

    if (Call->getCalledOperand() != Other.Call->getCalledOperand())
      return false;

    return Call->arg_size() == Other.Call->arg_size() &&
           std::equal(Call->arg_begin(), Call->arg_end(),
                      Other.Call->arg_begin());
  }

private:
  union {
    const CallBase *Call;
    MemoryLocation Loc;
  };
};

} // end anonymous namespace

namespace llvm {

template <> struct DenseMapInfo<MemoryLocOrCall> {
  static inline MemoryLocOrCall getEmptyKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getEmptyKey());
  }

  static inline MemoryLocOrCall getTombstoneKey() {
    return MemoryLocOrCall(DenseMapInfo<MemoryLocation>::getTombstoneKey());
  }

  static unsigned getHashValue(const MemoryLocOrCall &MLOC) {
    if (!MLOC.IsCall)
      return hash_combine(
          MLOC.IsCall,
          DenseMapInfo<MemoryLocation>::getHashValue(MLOC.getLoc()));

    hash_code hash =
        hash_combine(MLOC.IsCall, DenseMapInfo<const Value *>::getHashValue(
                                      MLOC.getCall()->getCalledOperand()));

    for (const Value *Arg : MLOC.getCall()->args())
      hash = hash_combine(hash, DenseMapInfo<const Value *>::getHashValue(Arg));
    return hash;
  }

  static bool isEqual(const MemoryLocOrCall &LHS, const MemoryLocOrCall &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

/// This does one-way checks to see if Use could theoretically be hoisted above
/// MayClobber. This will not check the other way around.
///
/// This assumes that, for the purposes of MemorySSA, Use comes directly after
/// MayClobber, with no potentially clobbering operations in between them.
/// (Where potentially clobbering ops are memory barriers, aliased stores, etc.)
static bool areLoadsReorderable(const LoadInst *Use,
                                const LoadInst *MayClobber) {
  bool VolatileUse = Use->isVolatile();
  bool VolatileClobber = MayClobber->isVolatile();
  // Volatile operations may never be reordered with other volatile operations.
  if (VolatileUse && VolatileClobber)
    return false;
  // Otherwise, volatile doesn't matter here. From the language reference:
  // 'optimizers may change the order of volatile operations relative to
  // non-volatile operations.'"

  // If a load is seq_cst, it cannot be moved above other loads. If its ordering
  // is weaker, it can be moved above other loads. We just need to be sure that
  // MayClobber isn't an acquire load, because loads can't be moved above
  // acquire loads.
  //
  // Note that this explicitly *does* allow the free reordering of monotonic (or
  // weaker) loads of the same address.
  bool SeqCstUse = Use->getOrdering() == AtomicOrdering::SequentiallyConsistent;
  bool MayClobberIsAcquire = isAtLeastOrStrongerThan(MayClobber->getOrdering(),
                                                     AtomicOrdering::Acquire);
  return !(SeqCstUse || MayClobberIsAcquire);
}

template <typename AliasAnalysisType>
static bool
instructionClobbersQuery(const MemoryDef *MD, const MemoryLocation &UseLoc,
                         const Instruction *UseInst, AliasAnalysisType &AA) {
  Instruction *DefInst = MD->getMemoryInst();
  assert(DefInst && "Defining instruction not actually an instruction");

  if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(DefInst)) {
    // These intrinsics will show up as affecting memory, but they are just
    // markers, mostly.
    //
    // FIXME: We probably don't actually want MemorySSA to model these at all
    // (including creating MemoryAccesses for them): we just end up inventing
    // clobbers where they don't really exist at all. Please see D43269 for
    // context.
    switch (II->getIntrinsicID()) {
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::assume:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::pseudoprobe:
      return false;
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_label:
    case Intrinsic::dbg_value:
      llvm_unreachable("debuginfo shouldn't have associated defs!");
    default:
      break;
    }
  }

  if (auto *CB = dyn_cast_or_null<CallBase>(UseInst)) {
    ModRefInfo I = AA.getModRefInfo(DefInst, CB);
    return isModOrRefSet(I);
  }

  if (auto *DefLoad = dyn_cast<LoadInst>(DefInst))
    if (auto *UseLoad = dyn_cast_or_null<LoadInst>(UseInst))
      return !areLoadsReorderable(UseLoad, DefLoad);

  ModRefInfo I = AA.getModRefInfo(DefInst, UseLoc);
  return isModSet(I);
}

template <typename AliasAnalysisType>
static bool instructionClobbersQuery(MemoryDef *MD, const MemoryUseOrDef *MU,
                                     const MemoryLocOrCall &UseMLOC,
                                     AliasAnalysisType &AA) {
  // FIXME: This is a temporary hack to allow a single instructionClobbersQuery
  // to exist while MemoryLocOrCall is pushed through places.
  if (UseMLOC.IsCall)
    return instructionClobbersQuery(MD, MemoryLocation(), MU->getMemoryInst(),
                                    AA);
  return instructionClobbersQuery(MD, UseMLOC.getLoc(), MU->getMemoryInst(),
                                  AA);
}

// Return true when MD may alias MU, return false otherwise.
bool MemorySSAUtil::defClobbersUseOrDef(MemoryDef *MD, const MemoryUseOrDef *MU,
                                        AliasAnalysis &AA) {
  return instructionClobbersQuery(MD, MU, MemoryLocOrCall(MU), AA);
}

namespace {

struct UpwardsMemoryQuery {
  // True if our original query started off as a call
  bool IsCall = false;
  // The pointer location we started the query with. This will be empty if
  // IsCall is true.
  MemoryLocation StartingLoc;
  // This is the instruction we were querying about.
  const Instruction *Inst = nullptr;
  // The MemoryAccess we actually got called with, used to test local domination
  const MemoryAccess *OriginalAccess = nullptr;
  bool SkipSelfAccess = false;

  UpwardsMemoryQuery() = default;

  UpwardsMemoryQuery(const Instruction *Inst, const MemoryAccess *Access)
      : IsCall(isa<CallBase>(Inst)), Inst(Inst), OriginalAccess(Access) {
    if (!IsCall)
      StartingLoc = MemoryLocation::get(Inst);
  }
};

} // end anonymous namespace

template <typename AliasAnalysisType>
static bool isUseTriviallyOptimizableToLiveOnEntry(AliasAnalysisType &AA,
                                                   const Instruction *I) {
  // If the memory can't be changed, then loads of the memory can't be
  // clobbered.
  if (auto *LI = dyn_cast<LoadInst>(I)) {
    return I->hasMetadata(LLVMContext::MD_invariant_load) ||
           !isModSet(AA.getModRefInfoMask(MemoryLocation::get(LI)));
  }
  return false;
}

/// Verifies that `Start` is clobbered by `ClobberAt`, and that nothing
/// inbetween `Start` and `ClobberAt` can clobbers `Start`.
///
/// This is meant to be as simple and self-contained as possible. Because it
/// uses no cache, etc., it can be relatively expensive.
///
/// \param Start     The MemoryAccess that we want to walk from.
/// \param ClobberAt A clobber for Start.
/// \param StartLoc  The MemoryLocation for Start.
/// \param MSSA      The MemorySSA instance that Start and ClobberAt belong to.
/// \param Query     The UpwardsMemoryQuery we used for our search.
/// \param AA        The AliasAnalysis we used for our search.
/// \param AllowImpreciseClobber Always false, unless we do relaxed verify.

LLVM_ATTRIBUTE_UNUSED static void
checkClobberSanity(const MemoryAccess *Start, MemoryAccess *ClobberAt,
                   const MemoryLocation &StartLoc, const MemorySSA &MSSA,
                   const UpwardsMemoryQuery &Query, BatchAAResults &AA,
                   bool AllowImpreciseClobber = false) {
  assert(MSSA.dominates(ClobberAt, Start) && "Clobber doesn't dominate start?");

  if (MSSA.isLiveOnEntryDef(Start)) {
    assert(MSSA.isLiveOnEntryDef(ClobberAt) &&
           "liveOnEntry must clobber itself");
    return;
  }

  bool FoundClobber = false;
  DenseSet<ConstMemoryAccessPair> VisitedPhis;
  SmallVector<ConstMemoryAccessPair, 8> Worklist;
  Worklist.emplace_back(Start, StartLoc);
  // Walk all paths from Start to ClobberAt, while looking for clobbers. If one
  // is found, complain.
  while (!Worklist.empty()) {
    auto MAP = Worklist.pop_back_val();
    // All we care about is that nothing from Start to ClobberAt clobbers Start.
    // We learn nothing from revisiting nodes.
    if (!VisitedPhis.insert(MAP).second)
      continue;

    for (const auto *MA : def_chain(MAP.first)) {
      if (MA == ClobberAt) {
        if (const auto *MD = dyn_cast<MemoryDef>(MA)) {
          // instructionClobbersQuery isn't essentially free, so don't use `|=`,
          // since it won't let us short-circuit.
          //
          // Also, note that this can't be hoisted out of the `Worklist` loop,
          // since MD may only act as a clobber for 1 of N MemoryLocations.
          FoundClobber = FoundClobber || MSSA.isLiveOnEntryDef(MD);
          if (!FoundClobber) {
            if (instructionClobbersQuery(MD, MAP.second, Query.Inst, AA))
              FoundClobber = true;
          }
        }
        break;
      }

      // We should never hit liveOnEntry, unless it's the clobber.
      assert(!MSSA.isLiveOnEntryDef(MA) && "Hit liveOnEntry before clobber?");

      if (const auto *MD = dyn_cast<MemoryDef>(MA)) {
        // If Start is a Def, skip self.
        if (MD == Start)
          continue;

        assert(!instructionClobbersQuery(MD, MAP.second, Query.Inst, AA) &&
               "Found clobber before reaching ClobberAt!");
        continue;
      }

      if (const auto *MU = dyn_cast<MemoryUse>(MA)) {
        (void)MU;
        assert (MU == Start &&
                "Can only find use in def chain if Start is a use");
        continue;
      }

      assert(isa<MemoryPhi>(MA));

      // Add reachable phi predecessors
      for (auto ItB = upward_defs_begin(
                    {const_cast<MemoryAccess *>(MA), MAP.second},
                    MSSA.getDomTree()),
                ItE = upward_defs_end();
           ItB != ItE; ++ItB)
        if (MSSA.getDomTree().isReachableFromEntry(ItB.getPhiArgBlock()))
          Worklist.emplace_back(*ItB);
    }
  }

  // If the verify is done following an optimization, it's possible that
  // ClobberAt was a conservative clobbering, that we can now infer is not a
  // true clobbering access. Don't fail the verify if that's the case.
  // We do have accesses that claim they're optimized, but could be optimized
  // further. Updating all these can be expensive, so allow it for now (FIXME).
  if (AllowImpreciseClobber)
    return;

  // If ClobberAt is a MemoryPhi, we can assume something above it acted as a
  // clobber. Otherwise, `ClobberAt` should've acted as a clobber at some point.
  assert((isa<MemoryPhi>(ClobberAt) || FoundClobber) &&
         "ClobberAt never acted as a clobber");
}

namespace {

/// Our algorithm for walking (and trying to optimize) clobbers, all wrapped up
/// in one class.
class ClobberWalker {
  /// Save a few bytes by using unsigned instead of size_t.
  using ListIndex = unsigned;

  /// Represents a span of contiguous MemoryDefs, potentially ending in a
  /// MemoryPhi.
  struct DefPath {
    MemoryLocation Loc;
    // Note that, because we always walk in reverse, Last will always dominate
    // First. Also note that First and Last are inclusive.
    MemoryAccess *First;
    MemoryAccess *Last;
    std::optional<ListIndex> Previous;

    DefPath(const MemoryLocation &Loc, MemoryAccess *First, MemoryAccess *Last,
            std::optional<ListIndex> Previous)
        : Loc(Loc), First(First), Last(Last), Previous(Previous) {}

    DefPath(const MemoryLocation &Loc, MemoryAccess *Init,
            std::optional<ListIndex> Previous)
        : DefPath(Loc, Init, Init, Previous) {}
  };

  const MemorySSA &MSSA;
  DominatorTree &DT;
  BatchAAResults *AA;
  UpwardsMemoryQuery *Query;
  unsigned *UpwardWalkLimit;

  // Phi optimization bookkeeping:
  // List of DefPath to process during the current phi optimization walk.
  SmallVector<DefPath, 32> Paths;
  // List of visited <Access, Location> pairs; we can skip paths already
  // visited with the same memory location.
  DenseSet<ConstMemoryAccessPair> VisitedPhis;

  /// Find the nearest def or phi that `From` can legally be optimized to.
  const MemoryAccess *getWalkTarget(const MemoryPhi *From) const {
    assert(From->getNumOperands() && "Phi with no operands?");

    BasicBlock *BB = From->getBlock();
    MemoryAccess *Result = MSSA.getLiveOnEntryDef();
    DomTreeNode *Node = DT.getNode(BB);
    while ((Node = Node->getIDom())) {
      auto *Defs = MSSA.getBlockDefs(Node->getBlock());
      if (Defs)
        return &*Defs->rbegin();
    }
    return Result;
  }

  /// Result of calling walkToPhiOrClobber.
  struct UpwardsWalkResult {
    /// The "Result" of the walk. Either a clobber, the last thing we walked, or
    /// both. Include alias info when clobber found.
    MemoryAccess *Result;
    bool IsKnownClobber;
  };

  /// Walk to the next Phi or Clobber in the def chain starting at Desc.Last.
  /// This will update Desc.Last as it walks. It will (optionally) also stop at
  /// StopAt.
  ///
  /// This does not test for whether StopAt is a clobber
  UpwardsWalkResult
  walkToPhiOrClobber(DefPath &Desc, const MemoryAccess *StopAt = nullptr,
                     const MemoryAccess *SkipStopAt = nullptr) const {
    assert(!isa<MemoryUse>(Desc.Last) && "Uses don't exist in my world");
    assert(UpwardWalkLimit && "Need a valid walk limit");
    bool LimitAlreadyReached = false;
    // (*UpwardWalkLimit) may be 0 here, due to the loop in tryOptimizePhi. Set
    // it to 1. This will not do any alias() calls. It either returns in the
    // first iteration in the loop below, or is set back to 0 if all def chains
    // are free of MemoryDefs.
    if (!*UpwardWalkLimit) {
      *UpwardWalkLimit = 1;
      LimitAlreadyReached = true;
    }

    for (MemoryAccess *Current : def_chain(Desc.Last)) {
      Desc.Last = Current;
      if (Current == StopAt || Current == SkipStopAt)
        return {Current, false};

      if (auto *MD = dyn_cast<MemoryDef>(Current)) {
        if (MSSA.isLiveOnEntryDef(MD))
          return {MD, true};

        if (!--*UpwardWalkLimit)
          return {Current, true};

        if (instructionClobbersQuery(MD, Desc.Loc, Query->Inst, *AA))
          return {MD, true};
      }
    }

    if (LimitAlreadyReached)
      *UpwardWalkLimit = 0;

    assert(isa<MemoryPhi>(Desc.Last) &&
           "Ended at a non-clobber that's not a phi?");
    return {Desc.Last, false};
  }

  void addSearches(MemoryPhi *Phi, SmallVectorImpl<ListIndex> &PausedSearches,
                   ListIndex PriorNode) {
    auto UpwardDefsBegin = upward_defs_begin({Phi, Paths[PriorNode].Loc}, DT);
    auto UpwardDefs = make_range(UpwardDefsBegin, upward_defs_end());
    for (const MemoryAccessPair &P : UpwardDefs) {
      PausedSearches.push_back(Paths.size());
      Paths.emplace_back(P.second, P.first, PriorNode);
    }
  }

  /// Represents a search that terminated after finding a clobber. This clobber
  /// may or may not be present in the path of defs from LastNode..SearchStart,
  /// since it may have been retrieved from cache.
  struct TerminatedPath {
    MemoryAccess *Clobber;
    ListIndex LastNode;
  };

  /// Get an access that keeps us from optimizing to the given phi.
  ///
  /// PausedSearches is an array of indices into the Paths array. Its incoming
  /// value is the indices of searches that stopped at the last phi optimization
  /// target. It's left in an unspecified state.
  ///
  /// If this returns std::nullopt, NewPaused is a vector of searches that
  /// terminated at StopWhere. Otherwise, NewPaused is left in an unspecified
  /// state.
  std::optional<TerminatedPath>
  getBlockingAccess(const MemoryAccess *StopWhere,
                    SmallVectorImpl<ListIndex> &PausedSearches,
                    SmallVectorImpl<ListIndex> &NewPaused,
                    SmallVectorImpl<TerminatedPath> &Terminated) {
    assert(!PausedSearches.empty() && "No searches to continue?");

    // BFS vs DFS really doesn't make a difference here, so just do a DFS with
    // PausedSearches as our stack.
    while (!PausedSearches.empty()) {
      ListIndex PathIndex = PausedSearches.pop_back_val();
      DefPath &Node = Paths[PathIndex];

      // If we've already visited this path with this MemoryLocation, we don't
      // need to do so again.
      //
      // NOTE: That we just drop these paths on the ground makes caching
      // behavior sporadic. e.g. given a diamond:
      //  A
      // B C
      //  D
      //
      // ...If we walk D, B, A, C, we'll only cache the result of phi
      // optimization for A, B, and D; C will be skipped because it dies here.
      // This arguably isn't the worst thing ever, since:
      //   - We generally query things in a top-down order, so if we got below D
      //     without needing cache entries for {C, MemLoc}, then chances are
      //     that those cache entries would end up ultimately unused.
      //   - We still cache things for A, so C only needs to walk up a bit.
      // If this behavior becomes problematic, we can fix without a ton of extra
      // work.
      if (!VisitedPhis.insert({Node.Last, Node.Loc}).second)
        continue;

      const MemoryAccess *SkipStopWhere = nullptr;
      if (Query->SkipSelfAccess && Node.Loc == Query->StartingLoc) {
        assert(isa<MemoryDef>(Query->OriginalAccess));
        SkipStopWhere = Query->OriginalAccess;
      }

      UpwardsWalkResult Res = walkToPhiOrClobber(Node,
                                                 /*StopAt=*/StopWhere,
                                                 /*SkipStopAt=*/SkipStopWhere);
      if (Res.IsKnownClobber) {
        assert(Res.Result != StopWhere && Res.Result != SkipStopWhere);

        // If this wasn't a cache hit, we hit a clobber when walking. That's a
        // failure.
        TerminatedPath Term{Res.Result, PathIndex};
        if (!MSSA.dominates(Res.Result, StopWhere))
          return Term;

        // Otherwise, it's a valid thing to potentially optimize to.
        Terminated.push_back(Term);
        continue;
      }

      if (Res.Result == StopWhere || Res.Result == SkipStopWhere) {
        // We've hit our target. Save this path off for if we want to continue
        // walking. If we are in the mode of skipping the OriginalAccess, and
        // we've reached back to the OriginalAccess, do not save path, we've
        // just looped back to self.
        if (Res.Result != SkipStopWhere)
          NewPaused.push_back(PathIndex);
        continue;
      }

      assert(!MSSA.isLiveOnEntryDef(Res.Result) && "liveOnEntry is a clobber");
      addSearches(cast<MemoryPhi>(Res.Result), PausedSearches, PathIndex);
    }

    return std::nullopt;
  }

  template <typename T, typename Walker>
  struct generic_def_path_iterator
      : public iterator_facade_base<generic_def_path_iterator<T, Walker>,
                                    std::forward_iterator_tag, T *> {
    generic_def_path_iterator() = default;
    generic_def_path_iterator(Walker *W, ListIndex N) : W(W), N(N) {}

    T &operator*() const { return curNode(); }

    generic_def_path_iterator &operator++() {
      N = curNode().Previous;
      return *this;
    }

    bool operator==(const generic_def_path_iterator &O) const {
      if (N.has_value() != O.N.has_value())
        return false;
      return !N || *N == *O.N;
    }

  private:
    T &curNode() const { return W->Paths[*N]; }

    Walker *W = nullptr;
    std::optional<ListIndex> N;
  };

  using def_path_iterator = generic_def_path_iterator<DefPath, ClobberWalker>;
  using const_def_path_iterator =
      generic_def_path_iterator<const DefPath, const ClobberWalker>;

  iterator_range<def_path_iterator> def_path(ListIndex From) {
    return make_range(def_path_iterator(this, From), def_path_iterator());
  }

  iterator_range<const_def_path_iterator> const_def_path(ListIndex From) const {
    return make_range(const_def_path_iterator(this, From),
                      const_def_path_iterator());
  }

  struct OptznResult {
    /// The path that contains our result.
    TerminatedPath PrimaryClobber;
    /// The paths that we can legally cache back from, but that aren't
    /// necessarily the result of the Phi optimization.
    SmallVector<TerminatedPath, 4> OtherClobbers;
  };

  ListIndex defPathIndex(const DefPath &N) const {
    // The assert looks nicer if we don't need to do &N
    const DefPath *NP = &N;
    assert(!Paths.empty() && NP >= &Paths.front() && NP <= &Paths.back() &&
           "Out of bounds DefPath!");
    return NP - &Paths.front();
  }

  /// Try to optimize a phi as best as we can. Returns a SmallVector of Paths
  /// that act as legal clobbers. Note that this won't return *all* clobbers.
  ///
  /// Phi optimization algorithm tl;dr:
  ///   - Find the earliest def/phi, A, we can optimize to
  ///   - Find if all paths from the starting memory access ultimately reach A
  ///     - If not, optimization isn't possible.
  ///     - Otherwise, walk from A to another clobber or phi, A'.
  ///       - If A' is a def, we're done.
  ///       - If A' is a phi, try to optimize it.
  ///
  /// A path is a series of {MemoryAccess, MemoryLocation} pairs. A path
  /// terminates when a MemoryAccess that clobbers said MemoryLocation is found.
  OptznResult tryOptimizePhi(MemoryPhi *Phi, MemoryAccess *Start,
                             const MemoryLocation &Loc) {
    assert(Paths.empty() && VisitedPhis.empty() &&
           "Reset the optimization state.");

    Paths.emplace_back(Loc, Start, Phi, std::nullopt);
    // Stores how many "valid" optimization nodes we had prior to calling
    // addSearches/getBlockingAccess. Necessary for caching if we had a blocker.
    auto PriorPathsSize = Paths.size();

    SmallVector<ListIndex, 16> PausedSearches;
    SmallVector<ListIndex, 8> NewPaused;
    SmallVector<TerminatedPath, 4> TerminatedPaths;

    addSearches(Phi, PausedSearches, 0);

    // Moves the TerminatedPath with the "most dominated" Clobber to the end of
    // Paths.
    auto MoveDominatedPathToEnd = [&](SmallVectorImpl<TerminatedPath> &Paths) {
      assert(!Paths.empty() && "Need a path to move");
      auto Dom = Paths.begin();
      for (auto I = std::next(Dom), E = Paths.end(); I != E; ++I)
        if (!MSSA.dominates(I->Clobber, Dom->Clobber))
          Dom = I;
      auto Last = Paths.end() - 1;
      if (Last != Dom)
        std::iter_swap(Last, Dom);
    };

    MemoryPhi *Current = Phi;
    while (true) {
      assert(!MSSA.isLiveOnEntryDef(Current) &&
             "liveOnEntry wasn't treated as a clobber?");

      const auto *Target = getWalkTarget(Current);
      // If a TerminatedPath doesn't dominate Target, then it wasn't a legal
      // optimization for the prior phi.
      assert(all_of(TerminatedPaths, [&](const TerminatedPath &P) {
        return MSSA.dominates(P.Clobber, Target);
      }));

      // FIXME: This is broken, because the Blocker may be reported to be
      // liveOnEntry, and we'll happily wait for that to disappear (read: never)
      // For the moment, this is fine, since we do nothing with blocker info.
      if (std::optional<TerminatedPath> Blocker = getBlockingAccess(
              Target, PausedSearches, NewPaused, TerminatedPaths)) {

        // Find the node we started at. We can't search based on N->Last, since
        // we may have gone around a loop with a different MemoryLocation.
        auto Iter = find_if(def_path(Blocker->LastNode), [&](const DefPath &N) {
          return defPathIndex(N) < PriorPathsSize;
        });
        assert(Iter != def_path_iterator());

        DefPath &CurNode = *Iter;
        assert(CurNode.Last == Current);

        // Two things:
        // A. We can't reliably cache all of NewPaused back. Consider a case
        //    where we have two paths in NewPaused; one of which can't optimize
        //    above this phi, whereas the other can. If we cache the second path
        //    back, we'll end up with suboptimal cache entries. We can handle
        //    cases like this a bit better when we either try to find all
        //    clobbers that block phi optimization, or when our cache starts
        //    supporting unfinished searches.
        // B. We can't reliably cache TerminatedPaths back here without doing
        //    extra checks; consider a case like:
        //       T
        //      / \
        //     D   C
        //      \ /
        //       S
        //    Where T is our target, C is a node with a clobber on it, D is a
        //    diamond (with a clobber *only* on the left or right node, N), and
        //    S is our start. Say we walk to D, through the node opposite N
        //    (read: ignoring the clobber), and see a cache entry in the top
        //    node of D. That cache entry gets put into TerminatedPaths. We then
        //    walk up to C (N is later in our worklist), find the clobber, and
        //    quit. If we append TerminatedPaths to OtherClobbers, we'll cache
        //    the bottom part of D to the cached clobber, ignoring the clobber
        //    in N. Again, this problem goes away if we start tracking all
        //    blockers for a given phi optimization.
        TerminatedPath Result{CurNode.Last, defPathIndex(CurNode)};
        return {Result, {}};
      }

      // If there's nothing left to search, then all paths led to valid clobbers
      // that we got from our cache; pick the nearest to the start, and allow
      // the rest to be cached back.
      if (NewPaused.empty()) {
        MoveDominatedPathToEnd(TerminatedPaths);
        TerminatedPath Result = TerminatedPaths.pop_back_val();
        return {Result, std::move(TerminatedPaths)};
      }

      MemoryAccess *DefChainEnd = nullptr;
      SmallVector<TerminatedPath, 4> Clobbers;
      for (ListIndex Paused : NewPaused) {
        UpwardsWalkResult WR = walkToPhiOrClobber(Paths[Paused]);
        if (WR.IsKnownClobber)
          Clobbers.push_back({WR.Result, Paused});
        else
          // Micro-opt: If we hit the end of the chain, save it.
          DefChainEnd = WR.Result;
      }

      if (!TerminatedPaths.empty()) {
        // If we couldn't find the dominating phi/liveOnEntry in the above loop,
        // do it now.
        if (!DefChainEnd)
          for (auto *MA : def_chain(const_cast<MemoryAccess *>(Target)))
            DefChainEnd = MA;
        assert(DefChainEnd && "Failed to find dominating phi/liveOnEntry");

        // If any of the terminated paths don't dominate the phi we'll try to
        // optimize, we need to figure out what they are and quit.
        const BasicBlock *ChainBB = DefChainEnd->getBlock();
        for (const TerminatedPath &TP : TerminatedPaths) {
          // Because we know that DefChainEnd is as "high" as we can go, we
          // don't need local dominance checks; BB dominance is sufficient.
          if (DT.dominates(ChainBB, TP.Clobber->getBlock()))
            Clobbers.push_back(TP);
        }
      }

      // If we have clobbers in the def chain, find the one closest to Current
      // and quit.
      if (!Clobbers.empty()) {
        MoveDominatedPathToEnd(Clobbers);
        TerminatedPath Result = Clobbers.pop_back_val();
        return {Result, std::move(Clobbers)};
      }

      assert(all_of(NewPaused,
                    [&](ListIndex I) { return Paths[I].Last == DefChainEnd; }));

      // Because liveOnEntry is a clobber, this must be a phi.
      auto *DefChainPhi = cast<MemoryPhi>(DefChainEnd);

      PriorPathsSize = Paths.size();
      PausedSearches.clear();
      for (ListIndex I : NewPaused)
        addSearches(DefChainPhi, PausedSearches, I);
      NewPaused.clear();

      Current = DefChainPhi;
    }
  }

  void verifyOptResult(const OptznResult &R) const {
    assert(all_of(R.OtherClobbers, [&](const TerminatedPath &P) {
      return MSSA.dominates(P.Clobber, R.PrimaryClobber.Clobber);
    }));
  }

  void resetPhiOptznState() {
    Paths.clear();
    VisitedPhis.clear();
  }

public:
  ClobberWalker(const MemorySSA &MSSA, DominatorTree &DT)
      : MSSA(MSSA), DT(DT) {}

  /// Finds the nearest clobber for the given query, optimizing phis if
  /// possible.
  MemoryAccess *findClobber(BatchAAResults &BAA, MemoryAccess *Start,
                            UpwardsMemoryQuery &Q, unsigned &UpWalkLimit) {
    AA = &BAA;
    Query = &Q;
    UpwardWalkLimit = &UpWalkLimit;
    // Starting limit must be > 0.
    if (!UpWalkLimit)
      UpWalkLimit++;

    MemoryAccess *Current = Start;
    // This walker pretends uses don't exist. If we're handed one, silently grab
    // its def. (This has the nice side-effect of ensuring we never cache uses)
    if (auto *MU = dyn_cast<MemoryUse>(Start))
      Current = MU->getDefiningAccess();

    DefPath FirstDesc(Q.StartingLoc, Current, Current, std::nullopt);
    // Fast path for the overly-common case (no crazy phi optimization
    // necessary)
    UpwardsWalkResult WalkResult = walkToPhiOrClobber(FirstDesc);
    MemoryAccess *Result;
    if (WalkResult.IsKnownClobber) {
      Result = WalkResult.Result;
    } else {
      OptznResult OptRes = tryOptimizePhi(cast<MemoryPhi>(FirstDesc.Last),
                                          Current, Q.StartingLoc);
      verifyOptResult(OptRes);
      resetPhiOptznState();
      Result = OptRes.PrimaryClobber.Clobber;
    }

#ifdef EXPENSIVE_CHECKS
    if (!Q.SkipSelfAccess && *UpwardWalkLimit > 0)
      checkClobberSanity(Current, Result, Q.StartingLoc, MSSA, Q, BAA);
#endif
    return Result;
  }
};

struct RenamePassData {
  DomTreeNode *DTN;
  DomTreeNode::const_iterator ChildIt;
  MemoryAccess *IncomingVal;

  RenamePassData(DomTreeNode *D, DomTreeNode::const_iterator It,
                 MemoryAccess *M)
      : DTN(D), ChildIt(It), IncomingVal(M) {}

  void swap(RenamePassData &RHS) {
    std::swap(DTN, RHS.DTN);
    std::swap(ChildIt, RHS.ChildIt);
    std::swap(IncomingVal, RHS.IncomingVal);
  }
};

} // end anonymous namespace

namespace llvm {

class MemorySSA::ClobberWalkerBase {
  ClobberWalker Walker;
  MemorySSA *MSSA;

public:
  ClobberWalkerBase(MemorySSA *M, DominatorTree *D) : Walker(*M, *D), MSSA(M) {}

  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *,
                                              const MemoryLocation &,
                                              BatchAAResults &, unsigned &);
  // Third argument (bool), defines whether the clobber search should skip the
  // original queried access. If true, there will be a follow-up query searching
  // for a clobber access past "self". Note that the Optimized access is not
  // updated if a new clobber is found by this SkipSelf search. If this
  // additional query becomes heavily used we may decide to cache the result.
  // Walker instantiations will decide how to set the SkipSelf bool.
  MemoryAccess *getClobberingMemoryAccessBase(MemoryAccess *, BatchAAResults &,
                                              unsigned &, bool,
                                              bool UseInvariantGroup = true);
};

/// A MemorySSAWalker that does AA walks to disambiguate accesses. It no
/// longer does caching on its own, but the name has been retained for the
/// moment.
class MemorySSA::CachingWalker final : public MemorySSAWalker {
  ClobberWalkerBase *Walker;

public:
  CachingWalker(MemorySSA *M, ClobberWalkerBase *W)
      : MemorySSAWalker(M), Walker(W) {}
  ~CachingWalker() override = default;

  using MemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA, BatchAAResults &BAA,
                                          unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, BAA, UWL, false);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          BatchAAResults &BAA, unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, Loc, BAA, UWL);
  }
  // This method is not accessible outside of this file.
  MemoryAccess *getClobberingMemoryAccessWithoutInvariantGroup(
      MemoryAccess *MA, BatchAAResults &BAA, unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, BAA, UWL, false, false);
  }

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          BatchAAResults &BAA) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, BAA, UpwardWalkLimit);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          BatchAAResults &BAA) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, Loc, BAA, UpwardWalkLimit);
  }

  void invalidateInfo(MemoryAccess *MA) override {
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
      MUD->resetOptimized();
  }
};

class MemorySSA::SkipSelfWalker final : public MemorySSAWalker {
  ClobberWalkerBase *Walker;

public:
  SkipSelfWalker(MemorySSA *M, ClobberWalkerBase *W)
      : MemorySSAWalker(M), Walker(W) {}
  ~SkipSelfWalker() override = default;

  using MemorySSAWalker::getClobberingMemoryAccess;

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA, BatchAAResults &BAA,
                                          unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, BAA, UWL, true);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          BatchAAResults &BAA, unsigned &UWL) {
    return Walker->getClobberingMemoryAccessBase(MA, Loc, BAA, UWL);
  }

  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          BatchAAResults &BAA) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, BAA, UpwardWalkLimit);
  }
  MemoryAccess *getClobberingMemoryAccess(MemoryAccess *MA,
                                          const MemoryLocation &Loc,
                                          BatchAAResults &BAA) override {
    unsigned UpwardWalkLimit = MaxCheckLimit;
    return getClobberingMemoryAccess(MA, Loc, BAA, UpwardWalkLimit);
  }

  void invalidateInfo(MemoryAccess *MA) override {
    if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
      MUD->resetOptimized();
  }
};

} // end namespace llvm

void MemorySSA::renameSuccessorPhis(BasicBlock *BB, MemoryAccess *IncomingVal,
                                    bool RenameAllUses) {
  // Pass through values to our successors
  for (const BasicBlock *S : successors(BB)) {
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    if (RenameAllUses) {
      bool ReplacementDone = false;
      for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I)
        if (Phi->getIncomingBlock(I) == BB) {
          Phi->setIncomingValue(I, IncomingVal);
          ReplacementDone = true;
        }
      (void) ReplacementDone;
      assert(ReplacementDone && "Incomplete phi during partial rename");
    } else
      Phi->addIncoming(IncomingVal, BB);
  }
}

/// Rename a single basic block into MemorySSA form.
/// Uses the standard SSA renaming algorithm.
/// \returns The new incoming value.
MemoryAccess *MemorySSA::renameBlock(BasicBlock *BB, MemoryAccess *IncomingVal,
                                     bool RenameAllUses) {
  auto It = PerBlockAccesses.find(BB);
  // Skip most processing if the list is empty.
  if (It != PerBlockAccesses.end()) {
    AccessList *Accesses = It->second.get();
    for (MemoryAccess &L : *Accesses) {
      if (MemoryUseOrDef *MUD = dyn_cast<MemoryUseOrDef>(&L)) {
        if (MUD->getDefiningAccess() == nullptr || RenameAllUses)
          MUD->setDefiningAccess(IncomingVal);
        if (isa<MemoryDef>(&L))
          IncomingVal = &L;
      } else {
        IncomingVal = &L;
      }
    }
  }
  return IncomingVal;
}

/// This is the standard SSA renaming algorithm.
///
/// We walk the dominator tree in preorder, renaming accesses, and then filling
/// in phi nodes in our successors.
void MemorySSA::renamePass(DomTreeNode *Root, MemoryAccess *IncomingVal,
                           SmallPtrSetImpl<BasicBlock *> &Visited,
                           bool SkipVisited, bool RenameAllUses) {
  assert(Root && "Trying to rename accesses in an unreachable block");

  SmallVector<RenamePassData, 32> WorkStack;
  // Skip everything if we already renamed this block and we are skipping.
  // Note: You can't sink this into the if, because we need it to occur
  // regardless of whether we skip blocks or not.
  bool AlreadyVisited = !Visited.insert(Root->getBlock()).second;
  if (SkipVisited && AlreadyVisited)
    return;

  IncomingVal = renameBlock(Root->getBlock(), IncomingVal, RenameAllUses);
  renameSuccessorPhis(Root->getBlock(), IncomingVal, RenameAllUses);
  WorkStack.push_back({Root, Root->begin(), IncomingVal});

  while (!WorkStack.empty()) {
    DomTreeNode *Node = WorkStack.back().DTN;
    DomTreeNode::const_iterator ChildIt = WorkStack.back().ChildIt;
    IncomingVal = WorkStack.back().IncomingVal;

    if (ChildIt == Node->end()) {
      WorkStack.pop_back();
    } else {
      DomTreeNode *Child = *ChildIt;
      ++WorkStack.back().ChildIt;
      BasicBlock *BB = Child->getBlock();
      // Note: You can't sink this into the if, because we need it to occur
      // regardless of whether we skip blocks or not.
      AlreadyVisited = !Visited.insert(BB).second;
      if (SkipVisited && AlreadyVisited) {
        // We already visited this during our renaming, which can happen when
        // being asked to rename multiple blocks. Figure out the incoming val,
        // which is the last def.
        // Incoming value can only change if there is a block def, and in that
        // case, it's the last block def in the list.
        if (auto *BlockDefs = getWritableBlockDefs(BB))
          IncomingVal = &*BlockDefs->rbegin();
      } else
        IncomingVal = renameBlock(BB, IncomingVal, RenameAllUses);
      renameSuccessorPhis(BB, IncomingVal, RenameAllUses);
      WorkStack.push_back({Child, Child->begin(), IncomingVal});
    }
  }
}

/// This handles unreachable block accesses by deleting phi nodes in
/// unreachable blocks, and marking all other unreachable MemoryAccess's as
/// being uses of the live on entry definition.
void MemorySSA::markUnreachableAsLiveOnEntry(BasicBlock *BB) {
  assert(!DT->isReachableFromEntry(BB) &&
         "Reachable block found while handling unreachable blocks");

  // Make sure phi nodes in our reachable successors end up with a
  // LiveOnEntryDef for our incoming edge, even though our block is forward
  // unreachable.  We could just disconnect these blocks from the CFG fully,
  // but we do not right now.
  for (const BasicBlock *S : successors(BB)) {
    if (!DT->isReachableFromEntry(S))
      continue;
    auto It = PerBlockAccesses.find(S);
    // Rename the phi nodes in our successor block
    if (It == PerBlockAccesses.end() || !isa<MemoryPhi>(It->second->front()))
      continue;
    AccessList *Accesses = It->second.get();
    auto *Phi = cast<MemoryPhi>(&Accesses->front());
    Phi->addIncoming(LiveOnEntryDef.get(), BB);
  }

  auto It = PerBlockAccesses.find(BB);
  if (It == PerBlockAccesses.end())
    return;

  auto &Accesses = It->second;
  for (auto AI = Accesses->begin(), AE = Accesses->end(); AI != AE;) {
    auto Next = std::next(AI);
    // If we have a phi, just remove it. We are going to replace all
    // users with live on entry.
    if (auto *UseOrDef = dyn_cast<MemoryUseOrDef>(AI))
      UseOrDef->setDefiningAccess(LiveOnEntryDef.get());
    else
      Accesses->erase(AI);
    AI = Next;
  }
}

MemorySSA::MemorySSA(Function &Func, AliasAnalysis *AA, DominatorTree *DT)
    : DT(DT), F(&Func), LiveOnEntryDef(nullptr), Walker(nullptr),
      SkipWalker(nullptr) {
  // Build MemorySSA using a batch alias analysis. This reuses the internal
  // state that AA collects during an alias()/getModRefInfo() call. This is
  // safe because there are no CFG changes while building MemorySSA and can
  // significantly reduce the time spent by the compiler in AA, because we will
  // make queries about all the instructions in the Function.
  assert(AA && "No alias analysis?");
  BatchAAResults BatchAA(*AA);
  buildMemorySSA(BatchAA, iterator_range(F->begin(), F->end()));
  // Intentionally leave AA to nullptr while building so we don't accidentally
  // use non-batch AliasAnalysis.
  this->AA = AA;
  // Also create the walker here.
  getWalker();
}

MemorySSA::MemorySSA(Loop &L, AliasAnalysis *AA, DominatorTree *DT)
    : DT(DT), L(&L), LiveOnEntryDef(nullptr), Walker(nullptr),
      SkipWalker(nullptr) {
  // Build MemorySSA using a batch alias analysis. This reuses the internal
  // state that AA collects during an alias()/getModRefInfo() call. This is
  // safe because there are no CFG changes while building MemorySSA and can
  // significantly reduce the time spent by the compiler in AA, because we will
  // make queries about all the instructions in the Function.
  assert(AA && "No alias analysis?");
  BatchAAResults BatchAA(*AA);
  buildMemorySSA(
      BatchAA, map_range(L.blocks(), [](const BasicBlock *BB) -> BasicBlock & {
        return *const_cast<BasicBlock *>(BB);
      }));
  // Intentionally leave AA to nullptr while building so we don't accidentally
  // use non-batch AliasAnalysis.
  this->AA = AA;
  // Also create the walker here.
  getWalker();
}

MemorySSA::~MemorySSA() {
  // Drop all our references
  for (const auto &Pair : PerBlockAccesses)
    for (MemoryAccess &MA : *Pair.second)
      MA.dropAllReferences();
}

MemorySSA::AccessList *MemorySSA::getOrCreateAccessList(const BasicBlock *BB) {
  auto Res = PerBlockAccesses.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = std::make_unique<AccessList>();
  return Res.first->second.get();
}

MemorySSA::DefsList *MemorySSA::getOrCreateDefsList(const BasicBlock *BB) {
  auto Res = PerBlockDefs.insert(std::make_pair(BB, nullptr));

  if (Res.second)
    Res.first->second = std::make_unique<DefsList>();
  return Res.first->second.get();
}

namespace llvm {

/// This class is a batch walker of all MemoryUse's in the program, and points
/// their defining access at the thing that actually clobbers them.  Because it
/// is a batch walker that touches everything, it does not operate like the
/// other walkers.  This walker is basically performing a top-down SSA renaming
/// pass, where the version stack is used as the cache.  This enables it to be
/// significantly more time and memory efficient than using the regular walker,
/// which is walking bottom-up.
class MemorySSA::OptimizeUses {
public:
  OptimizeUses(MemorySSA *MSSA, CachingWalker *Walker, BatchAAResults *BAA,
               DominatorTree *DT)
      : MSSA(MSSA), Walker(Walker), AA(BAA), DT(DT) {}

  void optimizeUses();

private:
  /// This represents where a given memorylocation is in the stack.
  struct MemlocStackInfo {
    // This essentially is keeping track of versions of the stack. Whenever
    // the stack changes due to pushes or pops, these versions increase.
    unsigned long StackEpoch;
    unsigned long PopEpoch;
    // This is the lower bound of places on the stack to check. It is equal to
    // the place the last stack walk ended.
    // Note: Correctness depends on this being initialized to 0, which densemap
    // does
    unsigned long LowerBound;
    const BasicBlock *LowerBoundBlock;
    // This is where the last walk for this memory location ended.
    unsigned long LastKill;
    bool LastKillValid;
  };

  void optimizeUsesInBlock(const BasicBlock *, unsigned long &, unsigned long &,
                           SmallVectorImpl<MemoryAccess *> &,
                           DenseMap<MemoryLocOrCall, MemlocStackInfo> &);

  MemorySSA *MSSA;
  CachingWalker *Walker;
  BatchAAResults *AA;
  DominatorTree *DT;
};

} // end namespace llvm

/// Optimize the uses in a given block This is basically the SSA renaming
/// algorithm, with one caveat: We are able to use a single stack for all
/// MemoryUses.  This is because the set of *possible* reaching MemoryDefs is
/// the same for every MemoryUse.  The *actual* clobbering MemoryDef is just
/// going to be some position in that stack of possible ones.
///
/// We track the stack positions that each MemoryLocation needs
/// to check, and last ended at.  This is because we only want to check the
/// things that changed since last time.  The same MemoryLocation should
/// get clobbered by the same store (getModRefInfo does not use invariantness or
/// things like this, and if they start, we can modify MemoryLocOrCall to
/// include relevant data)
void MemorySSA::OptimizeUses::optimizeUsesInBlock(
    const BasicBlock *BB, unsigned long &StackEpoch, unsigned long &PopEpoch,
    SmallVectorImpl<MemoryAccess *> &VersionStack,
    DenseMap<MemoryLocOrCall, MemlocStackInfo> &LocStackInfo) {

  /// If no accesses, nothing to do.
  MemorySSA::AccessList *Accesses = MSSA->getWritableBlockAccesses(BB);
  if (Accesses == nullptr)
    return;

  // Pop everything that doesn't dominate the current block off the stack,
  // increment the PopEpoch to account for this.
  while (true) {
    assert(
        !VersionStack.empty() &&
        "Version stack should have liveOnEntry sentinel dominating everything");
    BasicBlock *BackBlock = VersionStack.back()->getBlock();
    if (DT->dominates(BackBlock, BB))
      break;
    while (VersionStack.back()->getBlock() == BackBlock)
      VersionStack.pop_back();
    ++PopEpoch;
  }

  for (MemoryAccess &MA : *Accesses) {
    auto *MU = dyn_cast<MemoryUse>(&MA);
    if (!MU) {
      VersionStack.push_back(&MA);
      ++StackEpoch;
      continue;
    }

    if (MU->isOptimized())
      continue;

    MemoryLocOrCall UseMLOC(MU);
    auto &LocInfo = LocStackInfo[UseMLOC];
    // If the pop epoch changed, it means we've removed stuff from top of
    // stack due to changing blocks. We may have to reset the lower bound or
    // last kill info.
    if (LocInfo.PopEpoch != PopEpoch) {
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
      // If the lower bound was in something that no longer dominates us, we
      // have to reset it.
      // We can't simply track stack size, because the stack may have had
      // pushes/pops in the meantime.
      // XXX: This is non-optimal, but only is slower cases with heavily
      // branching dominator trees.  To get the optimal number of queries would
      // be to make lowerbound and lastkill a per-loc stack, and pop it until
      // the top of that stack dominates us.  This does not seem worth it ATM.
      // A much cheaper optimization would be to always explore the deepest
      // branch of the dominator tree first. This will guarantee this resets on
      // the smallest set of blocks.
      if (LocInfo.LowerBoundBlock && LocInfo.LowerBoundBlock != BB &&
          !DT->dominates(LocInfo.LowerBoundBlock, BB)) {
        // Reset the lower bound of things to check.
        // TODO: Some day we should be able to reset to last kill, rather than
        // 0.
        LocInfo.LowerBound = 0;
        LocInfo.LowerBoundBlock = VersionStack[0]->getBlock();
        LocInfo.LastKillValid = false;
      }
    } else if (LocInfo.StackEpoch != StackEpoch) {
      // If all that has changed is the StackEpoch, we only have to check the
      // new things on the stack, because we've checked everything before.  In
      // this case, the lower bound of things to check remains the same.
      LocInfo.PopEpoch = PopEpoch;
      LocInfo.StackEpoch = StackEpoch;
    }
    if (!LocInfo.LastKillValid) {
      LocInfo.LastKill = VersionStack.size() - 1;
      LocInfo.LastKillValid = true;
    }

    // At this point, we should have corrected last kill and LowerBound to be
    // in bounds.
    assert(LocInfo.LowerBound < VersionStack.size() &&
           "Lower bound out of range");
    assert(LocInfo.LastKill < VersionStack.size() &&
           "Last kill info out of range");
    // In any case, the new upper bound is the top of the stack.
    unsigned long UpperBound = VersionStack.size() - 1;

    if (UpperBound - LocInfo.LowerBound > MaxCheckLimit) {
      LLVM_DEBUG(dbgs() << "MemorySSA skipping optimization of " << *MU << " ("
                        << *(MU->getMemoryInst()) << ")"
                        << " because there are "
                        << UpperBound - LocInfo.LowerBound
                        << " stores to disambiguate\n");
      // Because we did not walk, LastKill is no longer valid, as this may
      // have been a kill.
      LocInfo.LastKillValid = false;
      continue;
    }
    bool FoundClobberResult = false;
    unsigned UpwardWalkLimit = MaxCheckLimit;
    while (UpperBound > LocInfo.LowerBound) {
      if (isa<MemoryPhi>(VersionStack[UpperBound])) {
        // For phis, use the walker, see where we ended up, go there.
        // The invariant.group handling in MemorySSA is ad-hoc and doesn't
        // support updates, so don't use it to optimize uses.
        MemoryAccess *Result =
            Walker->getClobberingMemoryAccessWithoutInvariantGroup(
                MU, *AA, UpwardWalkLimit);
        // We are guaranteed to find it or something is wrong.
        while (VersionStack[UpperBound] != Result) {
          assert(UpperBound != 0);
          --UpperBound;
        }
        FoundClobberResult = true;
        break;
      }

      MemoryDef *MD = cast<MemoryDef>(VersionStack[UpperBound]);
      if (instructionClobbersQuery(MD, MU, UseMLOC, *AA)) {
        FoundClobberResult = true;
        break;
      }
      --UpperBound;
    }

    // At the end of this loop, UpperBound is either a clobber, or lower bound
    // PHI walking may cause it to be < LowerBound, and in fact, < LastKill.
    if (FoundClobberResult || UpperBound < LocInfo.LastKill) {
      MU->setDefiningAccess(VersionStack[UpperBound], true);
      LocInfo.LastKill = UpperBound;
    } else {
      // Otherwise, we checked all the new ones, and now we know we can get to
      // LastKill.
      MU->setDefiningAccess(VersionStack[LocInfo.LastKill], true);
    }
    LocInfo.LowerBound = VersionStack.size() - 1;
    LocInfo.LowerBoundBlock = BB;
  }
}

/// Optimize uses to point to their actual clobbering definitions.
void MemorySSA::OptimizeUses::optimizeUses() {
  SmallVector<MemoryAccess *, 16> VersionStack;
  DenseMap<MemoryLocOrCall, MemlocStackInfo> LocStackInfo;
  VersionStack.push_back(MSSA->getLiveOnEntryDef());

  unsigned long StackEpoch = 1;
  unsigned long PopEpoch = 1;
  // We perform a non-recursive top-down dominator tree walk.
  for (const auto *DomNode : depth_first(DT->getRootNode()))
    optimizeUsesInBlock(DomNode->getBlock(), StackEpoch, PopEpoch, VersionStack,
                        LocStackInfo);
}

void MemorySSA::placePHINodes(
    const SmallPtrSetImpl<BasicBlock *> &DefiningBlocks) {
  // Determine where our MemoryPhi's should go
  ForwardIDFCalculator IDFs(*DT);
  IDFs.setDefiningBlocks(DefiningBlocks);
  SmallVector<BasicBlock *, 32> IDFBlocks;
  IDFs.calculate(IDFBlocks);

  // Now place MemoryPhi nodes.
  for (auto &BB : IDFBlocks)
    createMemoryPhi(BB);
}

template <typename IterT>
void MemorySSA::buildMemorySSA(BatchAAResults &BAA, IterT Blocks) {
  // We create an access to represent "live on entry", for things like
  // arguments or users of globals, where the memory they use is defined before
  // the beginning of the function. We do not actually insert it into the IR.
  // We do not define a live on exit for the immediate uses, and thus our
  // semantics do *not* imply that something with no immediate uses can simply
  // be removed.
  BasicBlock &StartingPoint = *Blocks.begin();
  LiveOnEntryDef.reset(new MemoryDef(StartingPoint.getContext(), nullptr,
                                     nullptr, &StartingPoint, NextID++));

  // We maintain lists of memory accesses per-block, trading memory for time. We
  // could just look up the memory access for every possible instruction in the
  // stream.
  SmallPtrSet<BasicBlock *, 32> DefiningBlocks;
  // Go through each block, figure out where defs occur, and chain together all
  // the accesses.
  for (BasicBlock &B : Blocks) {
    bool InsertIntoDef = false;
    AccessList *Accesses = nullptr;
    DefsList *Defs = nullptr;
    for (Instruction &I : B) {
      MemoryUseOrDef *MUD = createNewAccess(&I, &BAA);
      if (!MUD)
        continue;

      if (!Accesses)
        Accesses = getOrCreateAccessList(&B);
      Accesses->push_back(MUD);
      if (isa<MemoryDef>(MUD)) {
        InsertIntoDef = true;
        if (!Defs)
          Defs = getOrCreateDefsList(&B);
        Defs->push_back(*MUD);
      }
    }
    if (InsertIntoDef)
      DefiningBlocks.insert(&B);
  }
  placePHINodes(DefiningBlocks);

  // Now do regular SSA renaming on the MemoryDef/MemoryUse. Visited will get
  // filled in with all blocks.
  SmallPtrSet<BasicBlock *, 16> Visited;
  if (L) {
    // Only building MemorySSA for a single loop. placePHINodes may have
    // inserted a MemoryPhi in the loop's preheader. As this is outside the
    // scope of the loop, set them to LiveOnEntry.
    if (auto *P = getMemoryAccess(L->getLoopPreheader())) {
      for (Use &U : make_early_inc_range(P->uses()))
        U.set(LiveOnEntryDef.get());
      removeFromLists(P);
    }
    // Now rename accesses in the loop. Populate Visited with the exit blocks of
    // the loop, to limit the scope of the renaming.
    SmallVector<BasicBlock *> ExitBlocks;
    L->getExitBlocks(ExitBlocks);
    Visited.insert(ExitBlocks.begin(), ExitBlocks.end());
    renamePass(DT->getNode(L->getLoopPreheader()), LiveOnEntryDef.get(),
               Visited);
  } else {
    renamePass(DT->getRootNode(), LiveOnEntryDef.get(), Visited);
  }

  // Mark the uses in unreachable blocks as live on entry, so that they go
  // somewhere.
  for (auto &BB : Blocks)
    if (!Visited.count(&BB))
      markUnreachableAsLiveOnEntry(&BB);
}

MemorySSAWalker *MemorySSA::getWalker() { return getWalkerImpl(); }

MemorySSA::CachingWalker *MemorySSA::getWalkerImpl() {
  if (Walker)
    return Walker.get();

  if (!WalkerBase)
    WalkerBase = std::make_unique<ClobberWalkerBase>(this, DT);

  Walker = std::make_unique<CachingWalker>(this, WalkerBase.get());
  return Walker.get();
}

MemorySSAWalker *MemorySSA::getSkipSelfWalker() {
  if (SkipWalker)
    return SkipWalker.get();

  if (!WalkerBase)
    WalkerBase = std::make_unique<ClobberWalkerBase>(this, DT);

  SkipWalker = std::make_unique<SkipSelfWalker>(this, WalkerBase.get());
  return SkipWalker.get();
 }


// This is a helper function used by the creation routines. It places NewAccess
// into the access and defs lists for a given basic block, at the given
// insertion point.
void MemorySSA::insertIntoListsForBlock(MemoryAccess *NewAccess,
                                        const BasicBlock *BB,
                                        InsertionPlace Point) {
  auto *Accesses = getOrCreateAccessList(BB);
  if (Point == Beginning) {
    // If it's a phi node, it goes first, otherwise, it goes after any phi
    // nodes.
    if (isa<MemoryPhi>(NewAccess)) {
      Accesses->push_front(NewAccess);
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_front(*NewAccess);
    } else {
      auto AI = find_if_not(
          *Accesses, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
      Accesses->insert(AI, NewAccess);
      if (!isa<MemoryUse>(NewAccess)) {
        auto *Defs = getOrCreateDefsList(BB);
        auto DI = find_if_not(
            *Defs, [](const MemoryAccess &MA) { return isa<MemoryPhi>(MA); });
        Defs->insert(DI, *NewAccess);
      }
    }
  } else {
    Accesses->push_back(NewAccess);
    if (!isa<MemoryUse>(NewAccess)) {
      auto *Defs = getOrCreateDefsList(BB);
      Defs->push_back(*NewAccess);
    }
  }
  BlockNumberingValid.erase(BB);
}

void MemorySSA::insertIntoListsBefore(MemoryAccess *What, const BasicBlock *BB,
                                      AccessList::iterator InsertPt) {
  auto *Accesses = getWritableBlockAccesses(BB);
  bool WasEnd = InsertPt == Accesses->end();
  Accesses->insert(AccessList::iterator(InsertPt), What);
  if (!isa<MemoryUse>(What)) {
    auto *Defs = getOrCreateDefsList(BB);
    // If we got asked to insert at the end, we have an easy job, just shove it
    // at the end. If we got asked to insert before an existing def, we also get
    // an iterator. If we got asked to insert before a use, we have to hunt for
    // the next def.
    if (WasEnd) {
      Defs->push_back(*What);
    } else if (isa<MemoryDef>(InsertPt)) {
      Defs->insert(InsertPt->getDefsIterator(), *What);
    } else {
      while (InsertPt != Accesses->end() && !isa<MemoryDef>(InsertPt))
        ++InsertPt;
      // Either we found a def, or we are inserting at the end
      if (InsertPt == Accesses->end())
        Defs->push_back(*What);
      else
        Defs->insert(InsertPt->getDefsIterator(), *What);
    }
  }
  BlockNumberingValid.erase(BB);
}

void MemorySSA::prepareForMoveTo(MemoryAccess *What, BasicBlock *BB) {
  // Keep it in the lookup tables, remove from the lists
  removeFromLists(What, false);

  // Note that moving should implicitly invalidate the optimized state of a
  // MemoryUse (and Phis can't be optimized). However, it doesn't do so for a
  // MemoryDef.
  if (auto *MD = dyn_cast<MemoryDef>(What))
    MD->resetOptimized();
  What->setBlock(BB);
}

// Move What before Where in the IR.  The end result is that What will belong to
// the right lists and have the right Block set, but will not otherwise be
// correct. It will not have the right defining access, and if it is a def,
// things below it will not properly be updated.
void MemorySSA::moveTo(MemoryUseOrDef *What, BasicBlock *BB,
                       AccessList::iterator Where) {
  prepareForMoveTo(What, BB);
  insertIntoListsBefore(What, BB, Where);
}

void MemorySSA::moveTo(MemoryAccess *What, BasicBlock *BB,
                       InsertionPlace Point) {
  if (isa<MemoryPhi>(What)) {
    assert(Point == Beginning &&
           "Can only move a Phi at the beginning of the block");
    // Update lookup table entry
    ValueToMemoryAccess.erase(What->getBlock());
    bool Inserted = ValueToMemoryAccess.insert({BB, What}).second;
    (void)Inserted;
    assert(Inserted && "Cannot move a Phi to a block that already has one");
  }

  prepareForMoveTo(What, BB);
  insertIntoListsForBlock(What, BB, Point);
}

MemoryPhi *MemorySSA::createMemoryPhi(BasicBlock *BB) {
  assert(!getMemoryAccess(BB) && "MemoryPhi already exists for this BB");
  MemoryPhi *Phi = new MemoryPhi(BB->getContext(), BB, NextID++);
  // Phi's always are placed at the front of the block.
  insertIntoListsForBlock(Phi, BB, Beginning);
  ValueToMemoryAccess[BB] = Phi;
  return Phi;
}

MemoryUseOrDef *MemorySSA::createDefinedAccess(Instruction *I,
                                               MemoryAccess *Definition,
                                               const MemoryUseOrDef *Template,
                                               bool CreationMustSucceed) {
  assert(!isa<PHINode>(I) && "Cannot create a defined access for a PHI");
  MemoryUseOrDef *NewAccess = createNewAccess(I, AA, Template);
  if (CreationMustSucceed)
    assert(NewAccess != nullptr && "Tried to create a memory access for a "
                                   "non-memory touching instruction");
  if (NewAccess) {
    assert((!Definition || !isa<MemoryUse>(Definition)) &&
           "A use cannot be a defining access");
    NewAccess->setDefiningAccess(Definition);
  }
  return NewAccess;
}

// Return true if the instruction has ordering constraints.
// Note specifically that this only considers stores and loads
// because others are still considered ModRef by getModRefInfo.
static inline bool isOrdered(const Instruction *I) {
  if (auto *SI = dyn_cast<StoreInst>(I)) {
    if (!SI->isUnordered())
      return true;
  } else if (auto *LI = dyn_cast<LoadInst>(I)) {
    if (!LI->isUnordered())
      return true;
  }
  return false;
}

/// Helper function to create new memory accesses
template <typename AliasAnalysisType>
MemoryUseOrDef *MemorySSA::createNewAccess(Instruction *I,
                                           AliasAnalysisType *AAP,
                                           const MemoryUseOrDef *Template) {
  // The assume intrinsic has a control dependency which we model by claiming
  // that it writes arbitrarily. Debuginfo intrinsics may be considered
  // clobbers when we have a nonstandard AA pipeline. Ignore these fake memory
  // dependencies here.
  // FIXME: Replace this special casing with a more accurate modelling of
  // assume's control dependency.
  if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I)) {
    switch (II->getIntrinsicID()) {
    default:
      break;
    case Intrinsic::allow_runtime_check:
    case Intrinsic::allow_ubsan_check:
    case Intrinsic::assume:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::pseudoprobe:
      return nullptr;
    }
  }

  // Using a nonstandard AA pipelines might leave us with unexpected modref
  // results for I, so add a check to not model instructions that may not read
  // from or write to memory. This is necessary for correctness.
  if (!I->mayReadFromMemory() && !I->mayWriteToMemory())
    return nullptr;

  bool Def, Use;
  if (Template) {
    Def = isa<MemoryDef>(Template);
    Use = isa<MemoryUse>(Template);
#if !defined(NDEBUG)
    ModRefInfo ModRef = AAP->getModRefInfo(I, std::nullopt);
    bool DefCheck, UseCheck;
    DefCheck = isModSet(ModRef) || isOrdered(I);
    UseCheck = isRefSet(ModRef);
    // Memory accesses should only be reduced and can not be increased since AA
    // just might return better results as a result of some transformations.
    assert((Def == DefCheck || !DefCheck) &&
           "Memory accesses should only be reduced");
    if (!Def && Use != UseCheck) {
      // New Access should not have more power than template access
      assert(!UseCheck && "Invalid template");
    }
#endif
  } else {
    // Find out what affect this instruction has on memory.
    ModRefInfo ModRef = AAP->getModRefInfo(I, std::nullopt);
    // The isOrdered check is used to ensure that volatiles end up as defs
    // (atomics end up as ModRef right now anyway).  Until we separate the
    // ordering chain from the memory chain, this enables people to see at least
    // some relative ordering to volatiles.  Note that getClobberingMemoryAccess
    // will still give an answer that bypasses other volatile loads.  TODO:
    // Separate memory aliasing and ordering into two different chains so that
    // we can precisely represent both "what memory will this read/write/is
    // clobbered by" and "what instructions can I move this past".
    Def = isModSet(ModRef) || isOrdered(I);
    Use = isRefSet(ModRef);
  }

  // It's possible for an instruction to not modify memory at all. During
  // construction, we ignore them.
  if (!Def && !Use)
    return nullptr;

  MemoryUseOrDef *MUD;
  if (Def) {
    MUD = new MemoryDef(I->getContext(), nullptr, I, I->getParent(), NextID++);
  } else {
    MUD = new MemoryUse(I->getContext(), nullptr, I, I->getParent());
    if (isUseTriviallyOptimizableToLiveOnEntry(*AAP, I)) {
      MemoryAccess *LiveOnEntry = getLiveOnEntryDef();
      MUD->setOptimized(LiveOnEntry);
    }
  }
  ValueToMemoryAccess[I] = MUD;
  return MUD;
}

/// Properly remove \p MA from all of MemorySSA's lookup tables.
void MemorySSA::removeFromLookups(MemoryAccess *MA) {
  assert(MA->use_empty() &&
         "Trying to remove memory access that still has uses");
  BlockNumbering.erase(MA);
  if (auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
    MUD->setDefiningAccess(nullptr);
  // Invalidate our walker's cache if necessary
  if (!isa<MemoryUse>(MA))
    getWalker()->invalidateInfo(MA);

  Value *MemoryInst;
  if (const auto *MUD = dyn_cast<MemoryUseOrDef>(MA))
    MemoryInst = MUD->getMemoryInst();
  else
    MemoryInst = MA->getBlock();

  auto VMA = ValueToMemoryAccess.find(MemoryInst);
  if (VMA->second == MA)
    ValueToMemoryAccess.erase(VMA);
}

/// Properly remove \p MA from all of MemorySSA's lists.
///
/// Because of the way the intrusive list and use lists work, it is important to
/// do removal in the right order.
/// ShouldDelete defaults to true, and will cause the memory access to also be
/// deleted, not just removed.
void MemorySSA::removeFromLists(MemoryAccess *MA, bool ShouldDelete) {
  BasicBlock *BB = MA->getBlock();
  // The access list owns the reference, so we erase it from the non-owning list
  // first.
  if (!isa<MemoryUse>(MA)) {
    auto DefsIt = PerBlockDefs.find(BB);
    std::unique_ptr<DefsList> &Defs = DefsIt->second;
    Defs->remove(*MA);
    if (Defs->empty())
      PerBlockDefs.erase(DefsIt);
  }

  // The erase call here will delete it. If we don't want it deleted, we call
  // remove instead.
  auto AccessIt = PerBlockAccesses.find(BB);
  std::unique_ptr<AccessList> &Accesses = AccessIt->second;
  if (ShouldDelete)
    Accesses->erase(MA);
  else
    Accesses->remove(MA);

  if (Accesses->empty()) {
    PerBlockAccesses.erase(AccessIt);
    BlockNumberingValid.erase(BB);
  }
}

void MemorySSA::print(raw_ostream &OS) const {
  MemorySSAAnnotatedWriter Writer(this);
  Function *F = this->F;
  if (L)
    F = L->getHeader()->getParent();
  F->print(OS, &Writer);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MemorySSA::dump() const { print(dbgs()); }
#endif

void MemorySSA::verifyMemorySSA(VerificationLevel VL) const {
#if !defined(NDEBUG) && defined(EXPENSIVE_CHECKS)
  VL = VerificationLevel::Full;
#endif

#ifndef NDEBUG
  if (F) {
    auto Blocks = iterator_range(F->begin(), F->end());
    verifyOrderingDominationAndDefUses(Blocks, VL);
    verifyDominationNumbers(Blocks);
    if (VL == VerificationLevel::Full)
      verifyPrevDefInPhis(Blocks);
  } else {
    assert(L && "must either have loop or function");
    auto Blocks =
        map_range(L->blocks(), [](const BasicBlock *BB) -> BasicBlock & {
          return *const_cast<BasicBlock *>(BB);
        });
    verifyOrderingDominationAndDefUses(Blocks, VL);
    verifyDominationNumbers(Blocks);
    if (VL == VerificationLevel::Full)
      verifyPrevDefInPhis(Blocks);
  }
#endif
  // Previously, the verification used to also verify that the clobberingAccess
  // cached by MemorySSA is the same as the clobberingAccess found at a later
  // query to AA. This does not hold true in general due to the current fragility
  // of BasicAA which has arbitrary caps on the things it analyzes before giving
  // up. As a result, transformations that are correct, will lead to BasicAA
  // returning different Alias answers before and after that transformation.
  // Invalidating MemorySSA is not an option, as the results in BasicAA can be so
  // random, in the worst case we'd need to rebuild MemorySSA from scratch after
  // every transformation, which defeats the purpose of using it. For such an
  // example, see test4 added in D51960.
}

template <typename IterT>
void MemorySSA::verifyPrevDefInPhis(IterT Blocks) const {
  for (const BasicBlock &BB : Blocks) {
    if (MemoryPhi *Phi = getMemoryAccess(&BB)) {
      for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I) {
        auto *Pred = Phi->getIncomingBlock(I);
        auto *IncAcc = Phi->getIncomingValue(I);
        // If Pred has no unreachable predecessors, get last def looking at
        // IDoms. If, while walkings IDoms, any of these has an unreachable
        // predecessor, then the incoming def can be any access.
        if (auto *DTNode = DT->getNode(Pred)) {
          while (DTNode) {
            if (auto *DefList = getBlockDefs(DTNode->getBlock())) {
              auto *LastAcc = &*(--DefList->end());
              assert(LastAcc == IncAcc &&
                     "Incorrect incoming access into phi.");
              (void)IncAcc;
              (void)LastAcc;
              break;
            }
            DTNode = DTNode->getIDom();
          }
        } else {
          // If Pred has unreachable predecessors, but has at least a Def, the
          // incoming access can be the last Def in Pred, or it could have been
          // optimized to LoE. After an update, though, the LoE may have been
          // replaced by another access, so IncAcc may be any access.
          // If Pred has unreachable predecessors and no Defs, incoming access
          // should be LoE; However, after an update, it may be any access.
        }
      }
    }
  }
}

/// Verify that all of the blocks we believe to have valid domination numbers
/// actually have valid domination numbers.
template <typename IterT>
void MemorySSA::verifyDominationNumbers(IterT Blocks) const {
  if (BlockNumberingValid.empty())
    return;

  SmallPtrSet<const BasicBlock *, 16> ValidBlocks = BlockNumberingValid;
  for (const BasicBlock &BB : Blocks) {
    if (!ValidBlocks.count(&BB))
      continue;

    ValidBlocks.erase(&BB);

    const AccessList *Accesses = getBlockAccesses(&BB);
    // It's correct to say an empty block has valid numbering.
    if (!Accesses)
      continue;

    // Block numbering starts at 1.
    unsigned long LastNumber = 0;
    for (const MemoryAccess &MA : *Accesses) {
      auto ThisNumberIter = BlockNumbering.find(&MA);
      assert(ThisNumberIter != BlockNumbering.end() &&
             "MemoryAccess has no domination number in a valid block!");

      unsigned long ThisNumber = ThisNumberIter->second;
      assert(ThisNumber > LastNumber &&
             "Domination numbers should be strictly increasing!");
      (void)LastNumber;
      LastNumber = ThisNumber;
    }
  }

  assert(ValidBlocks.empty() &&
         "All valid BasicBlocks should exist in F -- dangling pointers?");
}

/// Verify ordering: the order and existence of MemoryAccesses matches the
/// order and existence of memory affecting instructions.
/// Verify domination: each definition dominates all of its uses.
/// Verify def-uses: the immediate use information - walk all the memory
/// accesses and verifying that, for each use, it appears in the appropriate
/// def's use list
template <typename IterT>
void MemorySSA::verifyOrderingDominationAndDefUses(IterT Blocks,
                                                   VerificationLevel VL) const {
  // Walk all the blocks, comparing what the lookups think and what the access
  // lists think, as well as the order in the blocks vs the order in the access
  // lists.
  SmallVector<MemoryAccess *, 32> ActualAccesses;
  SmallVector<MemoryAccess *, 32> ActualDefs;
  for (BasicBlock &B : Blocks) {
    const AccessList *AL = getBlockAccesses(&B);
    const auto *DL = getBlockDefs(&B);
    MemoryPhi *Phi = getMemoryAccess(&B);
    if (Phi) {
      // Verify ordering.
      ActualAccesses.push_back(Phi);
      ActualDefs.push_back(Phi);
      // Verify domination
      for (const Use &U : Phi->uses()) {
        assert(dominates(Phi, U) && "Memory PHI does not dominate it's uses");
        (void)U;
      }
      // Verify def-uses for full verify.
      if (VL == VerificationLevel::Full) {
        assert(Phi->getNumOperands() == pred_size(&B) &&
               "Incomplete MemoryPhi Node");
        for (unsigned I = 0, E = Phi->getNumIncomingValues(); I != E; ++I) {
          verifyUseInDefs(Phi->getIncomingValue(I), Phi);
          assert(is_contained(predecessors(&B), Phi->getIncomingBlock(I)) &&
                 "Incoming phi block not a block predecessor");
        }
      }
    }

    for (Instruction &I : B) {
      MemoryUseOrDef *MA = getMemoryAccess(&I);
      assert((!MA || (AL && (isa<MemoryUse>(MA) || DL))) &&
             "We have memory affecting instructions "
             "in this block but they are not in the "
             "access list or defs list");
      if (MA) {
        // Verify ordering.
        ActualAccesses.push_back(MA);
        if (MemoryAccess *MD = dyn_cast<MemoryDef>(MA)) {
          // Verify ordering.
          ActualDefs.push_back(MA);
          // Verify domination.
          for (const Use &U : MD->uses()) {
            assert(dominates(MD, U) &&
                   "Memory Def does not dominate it's uses");
            (void)U;
          }
        }
        // Verify def-uses for full verify.
        if (VL == VerificationLevel::Full)
          verifyUseInDefs(MA->getDefiningAccess(), MA);
      }
    }
    // Either we hit the assert, really have no accesses, or we have both
    // accesses and an access list. Same with defs.
    if (!AL && !DL)
      continue;
    // Verify ordering.
    assert(AL->size() == ActualAccesses.size() &&
           "We don't have the same number of accesses in the block as on the "
           "access list");
    assert((DL || ActualDefs.size() == 0) &&
           "Either we should have a defs list, or we should have no defs");
    assert((!DL || DL->size() == ActualDefs.size()) &&
           "We don't have the same number of defs in the block as on the "
           "def list");
    auto ALI = AL->begin();
    auto AAI = ActualAccesses.begin();
    while (ALI != AL->end() && AAI != ActualAccesses.end()) {
      assert(&*ALI == *AAI && "Not the same accesses in the same order");
      ++ALI;
      ++AAI;
    }
    ActualAccesses.clear();
    if (DL) {
      auto DLI = DL->begin();
      auto ADI = ActualDefs.begin();
      while (DLI != DL->end() && ADI != ActualDefs.end()) {
        assert(&*DLI == *ADI && "Not the same defs in the same order");
        ++DLI;
        ++ADI;
      }
    }
    ActualDefs.clear();
  }
}

/// Verify the def-use lists in MemorySSA, by verifying that \p Use
/// appears in the use list of \p Def.
void MemorySSA::verifyUseInDefs(MemoryAccess *Def, MemoryAccess *Use) const {
  // The live on entry use may cause us to get a NULL def here
  if (!Def)
    assert(isLiveOnEntryDef(Use) &&
           "Null def but use not point to live on entry def");
  else
    assert(is_contained(Def->users(), Use) &&
           "Did not find use in def's use list");
}

/// Perform a local numbering on blocks so that instruction ordering can be
/// determined in constant time.
/// TODO: We currently just number in order.  If we numbered by N, we could
/// allow at least N-1 sequences of insertBefore or insertAfter (and at least
/// log2(N) sequences of mixed before and after) without needing to invalidate
/// the numbering.
void MemorySSA::renumberBlock(const BasicBlock *B) const {
  // The pre-increment ensures the numbers really start at 1.
  unsigned long CurrentNumber = 0;
  const AccessList *AL = getBlockAccesses(B);
  assert(AL != nullptr && "Asking to renumber an empty block");
  for (const auto &I : *AL)
    BlockNumbering[&I] = ++CurrentNumber;
  BlockNumberingValid.insert(B);
}

/// Determine, for two memory accesses in the same block,
/// whether \p Dominator dominates \p Dominatee.
/// \returns True if \p Dominator dominates \p Dominatee.
bool MemorySSA::locallyDominates(const MemoryAccess *Dominator,
                                 const MemoryAccess *Dominatee) const {
  const BasicBlock *DominatorBlock = Dominator->getBlock();

  assert((DominatorBlock == Dominatee->getBlock()) &&
         "Asking for local domination when accesses are in different blocks!");
  // A node dominates itself.
  if (Dominatee == Dominator)
    return true;

  // When Dominatee is defined on function entry, it is not dominated by another
  // memory access.
  if (isLiveOnEntryDef(Dominatee))
    return false;

  // When Dominator is defined on function entry, it dominates the other memory
  // access.
  if (isLiveOnEntryDef(Dominator))
    return true;

  if (!BlockNumberingValid.count(DominatorBlock))
    renumberBlock(DominatorBlock);

  unsigned long DominatorNum = BlockNumbering.lookup(Dominator);
  // All numbers start with 1
  assert(DominatorNum != 0 && "Block was not numbered properly");
  unsigned long DominateeNum = BlockNumbering.lookup(Dominatee);
  assert(DominateeNum != 0 && "Block was not numbered properly");
  return DominatorNum < DominateeNum;
}

bool MemorySSA::dominates(const MemoryAccess *Dominator,
                          const MemoryAccess *Dominatee) const {
  if (Dominator == Dominatee)
    return true;

  if (isLiveOnEntryDef(Dominatee))
    return false;

  if (Dominator->getBlock() != Dominatee->getBlock())
    return DT->dominates(Dominator->getBlock(), Dominatee->getBlock());
  return locallyDominates(Dominator, Dominatee);
}

bool MemorySSA::dominates(const MemoryAccess *Dominator,
                          const Use &Dominatee) const {
  if (MemoryPhi *MP = dyn_cast<MemoryPhi>(Dominatee.getUser())) {
    BasicBlock *UseBB = MP->getIncomingBlock(Dominatee);
    // The def must dominate the incoming block of the phi.
    if (UseBB != Dominator->getBlock())
      return DT->dominates(Dominator->getBlock(), UseBB);
    // If the UseBB and the DefBB are the same, compare locally.
    return locallyDominates(Dominator, cast<MemoryAccess>(Dominatee));
  }
  // If it's not a PHI node use, the normal dominates can already handle it.
  return dominates(Dominator, cast<MemoryAccess>(Dominatee.getUser()));
}

void MemorySSA::ensureOptimizedUses() {
  if (IsOptimized)
    return;

  BatchAAResults BatchAA(*AA);
  ClobberWalkerBase WalkerBase(this, DT);
  CachingWalker WalkerLocal(this, &WalkerBase);
  OptimizeUses(this, &WalkerLocal, &BatchAA, DT).optimizeUses();
  IsOptimized = true;
}

void MemoryAccess::print(raw_ostream &OS) const {
  switch (getValueID()) {
  case MemoryPhiVal: return static_cast<const MemoryPhi *>(this)->print(OS);
  case MemoryDefVal: return static_cast<const MemoryDef *>(this)->print(OS);
  case MemoryUseVal: return static_cast<const MemoryUse *>(this)->print(OS);
  }
  llvm_unreachable("invalid value id");
}

void MemoryDef::print(raw_ostream &OS) const {
  MemoryAccess *UO = getDefiningAccess();

  auto printID = [&OS](MemoryAccess *A) {
    if (A && A->getID())
      OS << A->getID();
    else
      OS << LiveOnEntryStr;
  };

  OS << getID() << " = MemoryDef(";
  printID(UO);
  OS << ")";

  if (isOptimized()) {
    OS << "->";
    printID(getOptimized());
  }
}

void MemoryPhi::print(raw_ostream &OS) const {
  ListSeparator LS(",");
  OS << getID() << " = MemoryPhi(";
  for (const auto &Op : operands()) {
    BasicBlock *BB = getIncomingBlock(Op);
    MemoryAccess *MA = cast<MemoryAccess>(Op);

    OS << LS << '{';
    if (BB->hasName())
      OS << BB->getName();
    else
      BB->printAsOperand(OS, false);
    OS << ',';
    if (unsigned ID = MA->getID())
      OS << ID;
    else
      OS << LiveOnEntryStr;
    OS << '}';
  }
  OS << ')';
}

void MemoryUse::print(raw_ostream &OS) const {
  MemoryAccess *UO = getDefiningAccess();
  OS << "MemoryUse(";
  if (UO && UO->getID())
    OS << UO->getID();
  else
    OS << LiveOnEntryStr;
  OS << ')';
}

void MemoryAccess::dump() const {
// Cannot completely remove virtual function even in release mode.
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  print(dbgs());
  dbgs() << "\n";
#endif
}

class DOTFuncMSSAInfo {
private:
  const Function &F;
  MemorySSAAnnotatedWriter MSSAWriter;

public:
  DOTFuncMSSAInfo(const Function &F, MemorySSA &MSSA)
      : F(F), MSSAWriter(&MSSA) {}

  const Function *getFunction() { return &F; }
  MemorySSAAnnotatedWriter &getWriter() { return MSSAWriter; }
};

namespace llvm {

template <>
struct GraphTraits<DOTFuncMSSAInfo *> : public GraphTraits<const BasicBlock *> {
  static NodeRef getEntryNode(DOTFuncMSSAInfo *CFGInfo) {
    return &(CFGInfo->getFunction()->getEntryBlock());
  }

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  using nodes_iterator = pointer_iterator<Function::const_iterator>;

  static nodes_iterator nodes_begin(DOTFuncMSSAInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->begin());
  }

  static nodes_iterator nodes_end(DOTFuncMSSAInfo *CFGInfo) {
    return nodes_iterator(CFGInfo->getFunction()->end());
  }

  static size_t size(DOTFuncMSSAInfo *CFGInfo) {
    return CFGInfo->getFunction()->size();
  }
};

template <>
struct DOTGraphTraits<DOTFuncMSSAInfo *> : public DefaultDOTGraphTraits {

  DOTGraphTraits(bool IsSimple = false) : DefaultDOTGraphTraits(IsSimple) {}

  static std::string getGraphName(DOTFuncMSSAInfo *CFGInfo) {
    return "MSSA CFG for '" + CFGInfo->getFunction()->getName().str() +
           "' function";
  }

  std::string getNodeLabel(const BasicBlock *Node, DOTFuncMSSAInfo *CFGInfo) {
    return DOTGraphTraits<DOTFuncInfo *>::getCompleteNodeLabel(
        Node, nullptr,
        [CFGInfo](raw_string_ostream &OS, const BasicBlock &BB) -> void {
          BB.print(OS, &CFGInfo->getWriter(), true, true);
        },
        [](std::string &S, unsigned &I, unsigned Idx) -> void {
          std::string Str = S.substr(I, Idx - I);
          StringRef SR = Str;
          if (SR.count(" = MemoryDef(") || SR.count(" = MemoryPhi(") ||
              SR.count("MemoryUse("))
            return;
          DOTGraphTraits<DOTFuncInfo *>::eraseComment(S, I, Idx);
        });
  }

  static std::string getEdgeSourceLabel(const BasicBlock *Node,
                                        const_succ_iterator I) {
    return DOTGraphTraits<DOTFuncInfo *>::getEdgeSourceLabel(Node, I);
  }

  /// Display the raw branch weights from PGO.
  std::string getEdgeAttributes(const BasicBlock *Node, const_succ_iterator I,
                                DOTFuncMSSAInfo *CFGInfo) {
    return "";
  }

  std::string getNodeAttributes(const BasicBlock *Node,
                                DOTFuncMSSAInfo *CFGInfo) {
    return getNodeLabel(Node, CFGInfo).find(';') != std::string::npos
               ? "style=filled, fillcolor=lightpink"
               : "";
  }
};

} // namespace llvm

AnalysisKey MemorySSAAnalysis::Key;

MemorySSAAnalysis::Result MemorySSAAnalysis::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
  auto &AA = AM.getResult<AAManager>(F);
  return MemorySSAAnalysis::Result(std::make_unique<MemorySSA>(F, &AA, &DT));
}

bool MemorySSAAnalysis::Result::invalidate(
    Function &F, const PreservedAnalyses &PA,
    FunctionAnalysisManager::Invalidator &Inv) {
  auto PAC = PA.getChecker<MemorySSAAnalysis>();
  return !(PAC.preserved() || PAC.preservedSet<AllAnalysesOn<Function>>()) ||
         Inv.invalidate<AAManager>(F, PA) ||
         Inv.invalidate<DominatorTreeAnalysis>(F, PA);
}

PreservedAnalyses MemorySSAPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  auto &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();
  if (EnsureOptimizedUses)
    MSSA.ensureOptimizedUses();
  if (DotCFGMSSA != "") {
    DOTFuncMSSAInfo CFGInfo(F, MSSA);
    WriteGraph(&CFGInfo, "", false, "MSSA", DotCFGMSSA);
  } else {
    OS << "MemorySSA for function: " << F.getName() << "\n";
    MSSA.print(OS);
  }

  return PreservedAnalyses::all();
}

PreservedAnalyses MemorySSAWalkerPrinterPass::run(Function &F,
                                                  FunctionAnalysisManager &AM) {
  auto &MSSA = AM.getResult<MemorySSAAnalysis>(F).getMSSA();
  OS << "MemorySSA (walker) for function: " << F.getName() << "\n";
  MemorySSAWalkerAnnotatedWriter Writer(&MSSA);
  F.print(OS, &Writer);

  return PreservedAnalyses::all();
}

PreservedAnalyses MemorySSAVerifierPass::run(Function &F,
                                             FunctionAnalysisManager &AM) {
  AM.getResult<MemorySSAAnalysis>(F).getMSSA().verifyMemorySSA();

  return PreservedAnalyses::all();
}

char MemorySSAWrapperPass::ID = 0;

MemorySSAWrapperPass::MemorySSAWrapperPass() : FunctionPass(ID) {
  initializeMemorySSAWrapperPassPass(*PassRegistry::getPassRegistry());
}

void MemorySSAWrapperPass::releaseMemory() { MSSA.reset(); }

void MemorySSAWrapperPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
  AU.addRequiredTransitive<AAResultsWrapperPass>();
}

bool MemorySSAWrapperPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  MSSA.reset(new MemorySSA(F, &AA, &DT));
  return false;
}

void MemorySSAWrapperPass::verifyAnalysis() const {
  if (VerifyMemorySSA)
    MSSA->verifyMemorySSA();
}

void MemorySSAWrapperPass::print(raw_ostream &OS, const Module *M) const {
  MSSA->print(OS);
}

MemorySSAWalker::MemorySSAWalker(MemorySSA *M) : MSSA(M) {}

/// Walk the use-def chains starting at \p StartingAccess and find
/// the MemoryAccess that actually clobbers Loc.
///
/// \returns our clobbering memory access
MemoryAccess *MemorySSA::ClobberWalkerBase::getClobberingMemoryAccessBase(
    MemoryAccess *StartingAccess, const MemoryLocation &Loc,
    BatchAAResults &BAA, unsigned &UpwardWalkLimit) {
  assert(!isa<MemoryUse>(StartingAccess) && "Use cannot be defining access");

  // If location is undefined, conservatively return starting access.
  if (Loc.Ptr == nullptr)
    return StartingAccess;

  Instruction *I = nullptr;
  if (auto *StartingUseOrDef = dyn_cast<MemoryUseOrDef>(StartingAccess)) {
    if (MSSA->isLiveOnEntryDef(StartingUseOrDef))
      return StartingUseOrDef;

    I = StartingUseOrDef->getMemoryInst();

    // Conservatively, fences are always clobbers, so don't perform the walk if
    // we hit a fence.
    if (!isa<CallBase>(I) && I->isFenceLike())
      return StartingUseOrDef;
  }

  UpwardsMemoryQuery Q;
  Q.OriginalAccess = StartingAccess;
  Q.StartingLoc = Loc;
  Q.Inst = nullptr;
  Q.IsCall = false;

  // Unlike the other function, do not walk to the def of a def, because we are
  // handed something we already believe is the clobbering access.
  // We never set SkipSelf to true in Q in this method.
  MemoryAccess *Clobber =
      Walker.findClobber(BAA, StartingAccess, Q, UpwardWalkLimit);
  LLVM_DEBUG({
    dbgs() << "Clobber starting at access " << *StartingAccess << "\n";
    if (I)
      dbgs() << "  for instruction " << *I << "\n";
    dbgs() << "  is " << *Clobber << "\n";
  });
  return Clobber;
}

static const Instruction *
getInvariantGroupClobberingInstruction(Instruction &I, DominatorTree &DT) {
  if (!I.hasMetadata(LLVMContext::MD_invariant_group) || I.isVolatile())
    return nullptr;

  // We consider bitcasts and zero GEPs to be the same pointer value. Start by
  // stripping bitcasts and zero GEPs, then we will recursively look at loads
  // and stores through bitcasts and zero GEPs.
  Value *PointerOperand = getLoadStorePointerOperand(&I)->stripPointerCasts();

  // It's not safe to walk the use list of a global value because function
  // passes aren't allowed to look outside their functions.
  // FIXME: this could be fixed by filtering instructions from outside of
  // current function.
  if (isa<Constant>(PointerOperand))
    return nullptr;

  // Queue to process all pointers that are equivalent to load operand.
  SmallVector<const Value *, 8> PointerUsesQueue;
  PointerUsesQueue.push_back(PointerOperand);

  const Instruction *MostDominatingInstruction = &I;

  // FIXME: This loop is O(n^2) because dominates can be O(n) and in worst case
  // we will see all the instructions. It may not matter in practice. If it
  // does, we will have to support MemorySSA construction and updates.
  while (!PointerUsesQueue.empty()) {
    const Value *Ptr = PointerUsesQueue.pop_back_val();
    assert(Ptr && !isa<GlobalValue>(Ptr) &&
           "Null or GlobalValue should not be inserted");

    for (const User *Us : Ptr->users()) {
      auto *U = dyn_cast<Instruction>(Us);
      if (!U || U == &I || !DT.dominates(U, MostDominatingInstruction))
        continue;

      // Add bitcasts and zero GEPs to queue.
      if (isa<BitCastInst>(U)) {
        PointerUsesQueue.push_back(U);
        continue;
      }
      if (auto *GEP = dyn_cast<GetElementPtrInst>(U)) {
        if (GEP->hasAllZeroIndices())
          PointerUsesQueue.push_back(U);
        continue;
      }

      // If we hit a load/store with an invariant.group metadata and the same
      // pointer operand, we can assume that value pointed to by the pointer
      // operand didn't change.
      if (U->hasMetadata(LLVMContext::MD_invariant_group) &&
          getLoadStorePointerOperand(U) == Ptr && !U->isVolatile()) {
        MostDominatingInstruction = U;
      }
    }
  }
  return MostDominatingInstruction == &I ? nullptr : MostDominatingInstruction;
}

MemoryAccess *MemorySSA::ClobberWalkerBase::getClobberingMemoryAccessBase(
    MemoryAccess *MA, BatchAAResults &BAA, unsigned &UpwardWalkLimit,
    bool SkipSelf, bool UseInvariantGroup) {
  auto *StartingAccess = dyn_cast<MemoryUseOrDef>(MA);
  // If this is a MemoryPhi, we can't do anything.
  if (!StartingAccess)
    return MA;

  if (UseInvariantGroup) {
    if (auto *I = getInvariantGroupClobberingInstruction(
            *StartingAccess->getMemoryInst(), MSSA->getDomTree())) {
      assert(isa<LoadInst>(I) || isa<StoreInst>(I));

      auto *ClobberMA = MSSA->getMemoryAccess(I);
      assert(ClobberMA);
      if (isa<MemoryUse>(ClobberMA))
        return ClobberMA->getDefiningAccess();
      return ClobberMA;
    }
  }

  bool IsOptimized = false;

  // If this is an already optimized use or def, return the optimized result.
  // Note: Currently, we store the optimized def result in a separate field,
  // since we can't use the defining access.
  if (StartingAccess->isOptimized()) {
    if (!SkipSelf || !isa<MemoryDef>(StartingAccess))
      return StartingAccess->getOptimized();
    IsOptimized = true;
  }

  const Instruction *I = StartingAccess->getMemoryInst();
  // We can't sanely do anything with a fence, since they conservatively clobber
  // all memory, and have no locations to get pointers from to try to
  // disambiguate.
  if (!isa<CallBase>(I) && I->isFenceLike())
    return StartingAccess;

  UpwardsMemoryQuery Q(I, StartingAccess);

  if (isUseTriviallyOptimizableToLiveOnEntry(BAA, I)) {
    MemoryAccess *LiveOnEntry = MSSA->getLiveOnEntryDef();
    StartingAccess->setOptimized(LiveOnEntry);
    return LiveOnEntry;
  }

  MemoryAccess *OptimizedAccess;
  if (!IsOptimized) {
    // Start with the thing we already think clobbers this location
    MemoryAccess *DefiningAccess = StartingAccess->getDefiningAccess();

    // At this point, DefiningAccess may be the live on entry def.
    // If it is, we will not get a better result.
    if (MSSA->isLiveOnEntryDef(DefiningAccess)) {
      StartingAccess->setOptimized(DefiningAccess);
      return DefiningAccess;
    }

    OptimizedAccess =
        Walker.findClobber(BAA, DefiningAccess, Q, UpwardWalkLimit);
    StartingAccess->setOptimized(OptimizedAccess);
  } else
    OptimizedAccess = StartingAccess->getOptimized();

  LLVM_DEBUG(dbgs() << "Starting Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *StartingAccess << "\n");
  LLVM_DEBUG(dbgs() << "Optimized Memory SSA clobber for " << *I << " is ");
  LLVM_DEBUG(dbgs() << *OptimizedAccess << "\n");

  MemoryAccess *Result;
  if (SkipSelf && isa<MemoryPhi>(OptimizedAccess) &&
      isa<MemoryDef>(StartingAccess) && UpwardWalkLimit) {
    assert(isa<MemoryDef>(Q.OriginalAccess));
    Q.SkipSelfAccess = true;
    Result = Walker.findClobber(BAA, OptimizedAccess, Q, UpwardWalkLimit);
  } else
    Result = OptimizedAccess;

  LLVM_DEBUG(dbgs() << "Result Memory SSA clobber [SkipSelf = " << SkipSelf);
  LLVM_DEBUG(dbgs() << "] for " << *I << " is " << *Result << "\n");

  return Result;
}

MemoryAccess *
DoNothingMemorySSAWalker::getClobberingMemoryAccess(MemoryAccess *MA,
                                                    BatchAAResults &) {
  if (auto *Use = dyn_cast<MemoryUseOrDef>(MA))
    return Use->getDefiningAccess();
  return MA;
}

MemoryAccess *DoNothingMemorySSAWalker::getClobberingMemoryAccess(
    MemoryAccess *StartingAccess, const MemoryLocation &, BatchAAResults &) {
  if (auto *Use = dyn_cast<MemoryUseOrDef>(StartingAccess))
    return Use->getDefiningAccess();
  return StartingAccess;
}

void MemoryPhi::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryPhi *>(Self);
}

void MemoryDef::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryDef *>(Self);
}

void MemoryUse::deleteMe(DerivedUser *Self) {
  delete static_cast<MemoryUse *>(Self);
}

bool upward_defs_iterator::IsGuaranteedLoopInvariant(const Value *Ptr) const {
  auto IsGuaranteedLoopInvariantBase = [](const Value *Ptr) {
    Ptr = Ptr->stripPointerCasts();
    if (!isa<Instruction>(Ptr))
      return true;
    return isa<AllocaInst>(Ptr);
  };

  Ptr = Ptr->stripPointerCasts();
  if (auto *I = dyn_cast<Instruction>(Ptr)) {
    if (I->getParent()->isEntryBlock())
      return true;
  }
  if (auto *GEP = dyn_cast<GEPOperator>(Ptr)) {
    return IsGuaranteedLoopInvariantBase(GEP->getPointerOperand()) &&
           GEP->hasAllConstantIndices();
  }
  return IsGuaranteedLoopInvariantBase(Ptr);
}
