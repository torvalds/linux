//===- llvm/Analysis/AliasAnalysis.h - Alias Analysis Interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the generic AliasAnalysis interface, which is used as the
// common interface used by all clients of alias analysis information, and
// implemented by all alias analysis implementations.  Mod/Ref information is
// also captured by this interface.
//
// Implementations of this interface must implement the various virtual methods,
// which automatically provides functionality for the entire suite of client
// APIs.
//
// This API identifies memory regions with the MemoryLocation class. The pointer
// component specifies the base memory address of the region. The Size specifies
// the maximum size (in address units) of the memory region, or
// MemoryLocation::UnknownSize if the size is not known. The TBAA tag
// identifies the "type" of the memory reference; see the
// TypeBasedAliasAnalysis class for details.
//
// Some non-obvious details include:
//  - Pointers that point to two completely different objects in memory never
//    alias, regardless of the value of the Size component.
//  - NoAlias doesn't imply inequal pointers. The most obvious example of this
//    is two pointers to constant memory. Even if they are equal, constant
//    memory is never stored to, so there will never be any dependencies.
//    In this and other situations, the pointers may be both NoAlias and
//    MustAlias at the same time. The current API can only return one result,
//    though this is rarely a problem in practice.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ALIASANALYSIS_H
#define LLVM_ANALYSIS_ALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/ModRef.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace llvm {

class AnalysisUsage;
class AtomicCmpXchgInst;
class BasicBlock;
class CatchPadInst;
class CatchReturnInst;
class DominatorTree;
class FenceInst;
class Function;
class LoopInfo;
class PreservedAnalyses;
class TargetLibraryInfo;
class Value;

/// The possible results of an alias query.
///
/// These results are always computed between two MemoryLocation objects as
/// a query to some alias analysis.
///
/// Note that these are unscoped enumerations because we would like to support
/// implicitly testing a result for the existence of any possible aliasing with
/// a conversion to bool, but an "enum class" doesn't support this. The
/// canonical names from the literature are suffixed and unique anyways, and so
/// they serve as global constants in LLVM for these results.
///
/// See docs/AliasAnalysis.html for more information on the specific meanings
/// of these values.
class AliasResult {
private:
  static const int OffsetBits = 23;
  static const int AliasBits = 8;
  static_assert(AliasBits + 1 + OffsetBits <= 32,
                "AliasResult size is intended to be 4 bytes!");

  unsigned int Alias : AliasBits;
  unsigned int HasOffset : 1;
  signed int Offset : OffsetBits;

public:
  enum Kind : uint8_t {
    /// The two locations do not alias at all.
    ///
    /// This value is arranged to convert to false, while all other values
    /// convert to true. This allows a boolean context to convert the result to
    /// a binary flag indicating whether there is the possibility of aliasing.
    NoAlias = 0,
    /// The two locations may or may not alias. This is the least precise
    /// result.
    MayAlias,
    /// The two locations alias, but only due to a partial overlap.
    PartialAlias,
    /// The two locations precisely alias each other.
    MustAlias,
  };
  static_assert(MustAlias < (1 << AliasBits),
                "Not enough bit field size for the enum!");

  explicit AliasResult() = delete;
  constexpr AliasResult(const Kind &Alias)
      : Alias(Alias), HasOffset(false), Offset(0) {}

  operator Kind() const { return static_cast<Kind>(Alias); }

  bool operator==(const AliasResult &Other) const {
    return Alias == Other.Alias && HasOffset == Other.HasOffset &&
           Offset == Other.Offset;
  }
  bool operator!=(const AliasResult &Other) const { return !(*this == Other); }

  bool operator==(Kind K) const { return Alias == K; }
  bool operator!=(Kind K) const { return !(*this == K); }

  constexpr bool hasOffset() const { return HasOffset; }
  constexpr int32_t getOffset() const {
    assert(HasOffset && "No offset!");
    return Offset;
  }
  void setOffset(int32_t NewOffset) {
    if (isInt<OffsetBits>(NewOffset)) {
      HasOffset = true;
      Offset = NewOffset;
    }
  }

  /// Helper for processing AliasResult for swapped memory location pairs.
  void swap(bool DoSwap = true) {
    if (DoSwap && hasOffset())
      setOffset(-getOffset());
  }
};

static_assert(sizeof(AliasResult) == 4,
              "AliasResult size is intended to be 4 bytes!");

/// << operator for AliasResult.
raw_ostream &operator<<(raw_ostream &OS, AliasResult AR);

/// Virtual base class for providers of capture information.
struct CaptureInfo {
  virtual ~CaptureInfo() = 0;

  /// Check whether Object is not captured before instruction I. If OrAt is
  /// true, captures by instruction I itself are also considered.
  ///
  /// If I is nullptr, then captures at any point will be considered.
  virtual bool isNotCapturedBefore(const Value *Object, const Instruction *I,
                                   bool OrAt) = 0;
};

/// Context-free CaptureInfo provider, which computes and caches whether an
/// object is captured in the function at all, but does not distinguish whether
/// it was captured before or after the context instruction.
class SimpleCaptureInfo final : public CaptureInfo {
  SmallDenseMap<const Value *, bool, 8> IsCapturedCache;

public:
  bool isNotCapturedBefore(const Value *Object, const Instruction *I,
                           bool OrAt) override;
};

/// Context-sensitive CaptureInfo provider, which computes and caches the
/// earliest common dominator closure of all captures. It provides a good
/// approximation to a precise "captures before" analysis.
class EarliestEscapeInfo final : public CaptureInfo {
  DominatorTree &DT;
  const LoopInfo *LI;

  /// Map from identified local object to an instruction before which it does
  /// not escape, or nullptr if it never escapes. The "earliest" instruction
  /// may be a conservative approximation, e.g. the first instruction in the
  /// function is always a legal choice.
  DenseMap<const Value *, Instruction *> EarliestEscapes;

  /// Reverse map from instruction to the objects it is the earliest escape for.
  /// This is used for cache invalidation purposes.
  DenseMap<Instruction *, TinyPtrVector<const Value *>> Inst2Obj;

public:
  EarliestEscapeInfo(DominatorTree &DT, const LoopInfo *LI = nullptr)
      : DT(DT), LI(LI) {}

  bool isNotCapturedBefore(const Value *Object, const Instruction *I,
                           bool OrAt) override;

  void removeInstruction(Instruction *I);
};

/// Cache key for BasicAA results. It only includes the pointer and size from
/// MemoryLocation, as BasicAA is AATags independent. Additionally, it includes
/// the value of MayBeCrossIteration, which may affect BasicAA results.
struct AACacheLoc {
  using PtrTy = PointerIntPair<const Value *, 1, bool>;
  PtrTy Ptr;
  LocationSize Size;

  AACacheLoc(PtrTy Ptr, LocationSize Size) : Ptr(Ptr), Size(Size) {}
  AACacheLoc(const Value *Ptr, LocationSize Size, bool MayBeCrossIteration)
      : Ptr(Ptr, MayBeCrossIteration), Size(Size) {}
};

template <> struct DenseMapInfo<AACacheLoc> {
  static inline AACacheLoc getEmptyKey() {
    return {DenseMapInfo<AACacheLoc::PtrTy>::getEmptyKey(),
            DenseMapInfo<LocationSize>::getEmptyKey()};
  }
  static inline AACacheLoc getTombstoneKey() {
    return {DenseMapInfo<AACacheLoc::PtrTy>::getTombstoneKey(),
            DenseMapInfo<LocationSize>::getTombstoneKey()};
  }
  static unsigned getHashValue(const AACacheLoc &Val) {
    return DenseMapInfo<AACacheLoc::PtrTy>::getHashValue(Val.Ptr) ^
           DenseMapInfo<LocationSize>::getHashValue(Val.Size);
  }
  static bool isEqual(const AACacheLoc &LHS, const AACacheLoc &RHS) {
    return LHS.Ptr == RHS.Ptr && LHS.Size == RHS.Size;
  }
};

class AAResults;

/// This class stores info we want to provide to or retain within an alias
/// query. By default, the root query is stateless and starts with a freshly
/// constructed info object. Specific alias analyses can use this query info to
/// store per-query state that is important for recursive or nested queries to
/// avoid recomputing. To enable preserving this state across multiple queries
/// where safe (due to the IR not changing), use a `BatchAAResults` wrapper.
/// The information stored in an `AAQueryInfo` is currently limitted to the
/// caches used by BasicAA, but can further be extended to fit other AA needs.
class AAQueryInfo {
public:
  using LocPair = std::pair<AACacheLoc, AACacheLoc>;
  struct CacheEntry {
    /// Cache entry is neither an assumption nor does it use a (non-definitive)
    /// assumption.
    static constexpr int Definitive = -2;
    /// Cache entry is not an assumption itself, but may be using an assumption
    /// from higher up the stack.
    static constexpr int AssumptionBased = -1;

    AliasResult Result;
    /// Number of times a NoAlias assumption has been used, 0 for assumptions
    /// that have not been used. Can also take one of the Definitive or
    /// AssumptionBased values documented above.
    int NumAssumptionUses;

    /// Whether this is a definitive (non-assumption) result.
    bool isDefinitive() const { return NumAssumptionUses == Definitive; }
    /// Whether this is an assumption that has not been proven yet.
    bool isAssumption() const { return NumAssumptionUses >= 0; }
  };

  // Alias analysis result aggregration using which this query is performed.
  // Can be used to perform recursive queries.
  AAResults &AAR;

  using AliasCacheT = SmallDenseMap<LocPair, CacheEntry, 8>;
  AliasCacheT AliasCache;

  CaptureInfo *CI;

  /// Query depth used to distinguish recursive queries.
  unsigned Depth = 0;

  /// How many active NoAlias assumption uses there are.
  int NumAssumptionUses = 0;

  /// Location pairs for which an assumption based result is currently stored.
  /// Used to remove all potentially incorrect results from the cache if an
  /// assumption is disproven.
  SmallVector<AAQueryInfo::LocPair, 4> AssumptionBasedResults;

  /// Tracks whether the accesses may be on different cycle iterations.
  ///
  /// When interpret "Value" pointer equality as value equality we need to make
  /// sure that the "Value" is not part of a cycle. Otherwise, two uses could
  /// come from different "iterations" of a cycle and see different values for
  /// the same "Value" pointer.
  ///
  /// The following example shows the problem:
  ///   %p = phi(%alloca1, %addr2)
  ///   %l = load %ptr
  ///   %addr1 = gep, %alloca2, 0, %l
  ///   %addr2 = gep  %alloca2, 0, (%l + 1)
  ///      alias(%p, %addr1) -> MayAlias !
  ///   store %l, ...
  bool MayBeCrossIteration = false;

  /// Whether alias analysis is allowed to use the dominator tree, for use by
  /// passes that lazily update the DT while performing AA queries.
  bool UseDominatorTree = true;

  AAQueryInfo(AAResults &AAR, CaptureInfo *CI) : AAR(AAR), CI(CI) {}
};

/// AAQueryInfo that uses SimpleCaptureInfo.
class SimpleAAQueryInfo : public AAQueryInfo {
  SimpleCaptureInfo CI;

public:
  SimpleAAQueryInfo(AAResults &AAR) : AAQueryInfo(AAR, &CI) {}
};

class BatchAAResults;

class AAResults {
public:
  // Make these results default constructable and movable. We have to spell
  // these out because MSVC won't synthesize them.
  AAResults(const TargetLibraryInfo &TLI);
  AAResults(AAResults &&Arg);
  ~AAResults();

  /// Register a specific AA result.
  template <typename AAResultT> void addAAResult(AAResultT &AAResult) {
    // FIXME: We should use a much lighter weight system than the usual
    // polymorphic pattern because we don't own AAResult. It should
    // ideally involve two pointers and no separate allocation.
    AAs.emplace_back(new Model<AAResultT>(AAResult, *this));
  }

  /// Register a function analysis ID that the results aggregation depends on.
  ///
  /// This is used in the new pass manager to implement the invalidation logic
  /// where we must invalidate the results aggregation if any of our component
  /// analyses become invalid.
  void addAADependencyID(AnalysisKey *ID) { AADeps.push_back(ID); }

  /// Handle invalidation events in the new pass manager.
  ///
  /// The aggregation is invalidated if any of the underlying analyses is
  /// invalidated.
  bool invalidate(Function &F, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv);

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB);

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, LocationSize V1Size, const Value *V2,
                    LocationSize V2Size) {
    return alias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the primary \c alias interface.
  AliasResult alias(const Value *V1, const Value *V2) {
    return alias(MemoryLocation::getBeforeOrAfter(V1),
                 MemoryLocation::getBeforeOrAfter(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// no-alias.
  bool isNoAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::NoAlias;
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, LocationSize V1Size, const Value *V2,
                 LocationSize V2Size) {
    return isNoAlias(MemoryLocation(V1, V1Size), MemoryLocation(V2, V2Size));
  }

  /// A convenience wrapper around the \c isNoAlias helper interface.
  bool isNoAlias(const Value *V1, const Value *V2) {
    return isNoAlias(MemoryLocation::getBeforeOrAfter(V1),
                     MemoryLocation::getBeforeOrAfter(V2));
  }

  /// A trivial helper function to check to see if the specified pointers are
  /// must-alias.
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::MustAlias;
  }

  /// A convenience wrapper around the \c isMustAlias helper interface.
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(V1, LocationSize::precise(1), V2, LocationSize::precise(1)) ==
           AliasResult::MustAlias;
  }

  /// Checks whether the given location points to constant memory, or if
  /// \p OrLocal is true whether it points to a local alloca.
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false) {
    return isNoModRef(getModRefInfoMask(Loc, OrLocal));
  }

  /// A convenience wrapper around the primary \c pointsToConstantMemory
  /// interface.
  bool pointsToConstantMemory(const Value *P, bool OrLocal = false) {
    return pointsToConstantMemory(MemoryLocation::getBeforeOrAfter(P), OrLocal);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref
  /// from the ModRef info based on the knowledge that the memory location
  /// points to constant and/or locally-invariant memory.
  ///
  /// If IgnoreLocals is true, then this method returns NoModRef for memory
  /// that points to a local alloca.
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                               bool IgnoreLocals = false);

  /// A convenience wrapper around the primary \c getModRefInfoMask
  /// interface.
  ModRefInfo getModRefInfoMask(const Value *P, bool IgnoreLocals = false) {
    return getModRefInfoMask(MemoryLocation::getBeforeOrAfter(P), IgnoreLocals);
  }

  /// Get the ModRef info associated with a pointer argument of a call. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx);

  /// Return the behavior of the given call site.
  MemoryEffects getMemoryEffects(const CallBase *Call);

  /// Return the behavior when calling the given function.
  MemoryEffects getMemoryEffects(const Function *F);

  /// Checks if the specified call is known to never read or write memory.
  ///
  /// Note that if the call only reads from known-constant memory, it is also
  /// legal to return true. Also, calls that unwind the stack are legal for
  /// this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// without worrying about aliasing properties, and many calls have this
  /// property (e.g. calls to 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(const CallBase *Call) {
    return getMemoryEffects(Call).doesNotAccessMemory();
  }

  /// Checks if the specified function is known to never read or write memory.
  ///
  /// Note that if the function only reads from known-constant memory, it is
  /// also legal to return true. Also, function that unwind the stack are legal
  /// for this predicate.
  ///
  /// Many optimizations (such as CSE and LICM) can be performed on such calls
  /// to such functions without worrying about aliasing properties, and many
  /// functions have this property (e.g. 'sin' and 'cos').
  ///
  /// This property corresponds to the GCC 'const' attribute.
  bool doesNotAccessMemory(const Function *F) {
    return getMemoryEffects(F).doesNotAccessMemory();
  }

  /// Checks if the specified call is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Calls that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(const CallBase *Call) {
    return getMemoryEffects(Call).onlyReadsMemory();
  }

  /// Checks if the specified function is known to only read from non-volatile
  /// memory (or not access memory at all).
  ///
  /// Functions that unwind the stack are legal for this predicate.
  ///
  /// This property allows many common optimizations to be performed in the
  /// absence of interfering store instructions, such as CSE of strlen calls.
  ///
  /// This property corresponds to the GCC 'pure' attribute.
  bool onlyReadsMemory(const Function *F) {
    return getMemoryEffects(F).onlyReadsMemory();
  }

  /// Check whether or not an instruction may read or write the optionally
  /// specified memory location.
  ///
  ///
  /// An instruction that doesn't read or write memory may be trivially LICM'd
  /// for example.
  ///
  /// For function calls, this delegates to the alias-analysis specific
  /// call-site mod-ref behavior queries. Otherwise it delegates to the specific
  /// helpers above.
  ModRefInfo getModRefInfo(const Instruction *I,
                           const std::optional<MemoryLocation> &OptLoc) {
    SimpleAAQueryInfo AAQIP(*this);
    return getModRefInfo(I, OptLoc, AAQIP);
  }

  /// A convenience wrapper for constructing the memory location.
  ModRefInfo getModRefInfo(const Instruction *I, const Value *P,
                           LocationSize Size) {
    return getModRefInfo(I, MemoryLocation(P, Size));
  }

  /// Return information about whether a call and an instruction may refer to
  /// the same memory locations.
  ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call);

  /// Return information about whether a particular call site modifies
  /// or reads the specified memory location \p MemLoc before instruction \p I
  /// in a BasicBlock.
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc,
                                DominatorTree *DT) {
    SimpleAAQueryInfo AAQIP(*this);
    return callCapturesBefore(I, MemLoc, DT, AAQIP);
  }

  /// A convenience wrapper to synthesize a memory location.
  ModRefInfo callCapturesBefore(const Instruction *I, const Value *P,
                                LocationSize Size, DominatorTree *DT) {
    return callCapturesBefore(I, MemoryLocation(P, Size), DT);
  }

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Higher level methods for querying mod/ref information.
  /// @{

  /// Check if it is possible for execution of the specified basic block to
  /// modify the location Loc.
  bool canBasicBlockModify(const BasicBlock &BB, const MemoryLocation &Loc);

  /// A convenience wrapper synthesizing a memory location.
  bool canBasicBlockModify(const BasicBlock &BB, const Value *P,
                           LocationSize Size) {
    return canBasicBlockModify(BB, MemoryLocation(P, Size));
  }

  /// Check if it is possible for the execution of the specified instructions
  /// to mod\ref (according to the mode) the location Loc.
  ///
  /// The instructions to consider are all of the instructions in the range of
  /// [I1,I2] INCLUSIVE. I1 and I2 must be in the same basic block.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const MemoryLocation &Loc,
                                 const ModRefInfo Mode);

  /// A convenience wrapper synthesizing a memory location.
  bool canInstructionRangeModRef(const Instruction &I1, const Instruction &I2,
                                 const Value *Ptr, LocationSize Size,
                                 const ModRefInfo Mode) {
    return canInstructionRangeModRef(I1, I2, MemoryLocation(Ptr, Size), Mode);
  }

  // CtxI can be nullptr, in which case the query is whether or not the aliasing
  // relationship holds through the entire function.
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI = nullptr);

  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals = false);
  ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call2,
                           AAQueryInfo &AAQIP);
  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const VAArgInst *V, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const LoadInst *L, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const StoreInst *S, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const FenceInst *S, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const AtomicCmpXchgInst *CX,
                           const MemoryLocation &Loc, AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const AtomicRMWInst *RMW, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const CatchPadInst *I, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const CatchReturnInst *I, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI);
  ModRefInfo getModRefInfo(const Instruction *I,
                           const std::optional<MemoryLocation> &OptLoc,
                           AAQueryInfo &AAQIP);
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc, DominatorTree *DT,
                                AAQueryInfo &AAQIP);
  MemoryEffects getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI);

private:
  class Concept;

  template <typename T> class Model;

  friend class AAResultBase;

  const TargetLibraryInfo &TLI;

  std::vector<std::unique_ptr<Concept>> AAs;

  std::vector<AnalysisKey *> AADeps;

  friend class BatchAAResults;
};

/// This class is a wrapper over an AAResults, and it is intended to be used
/// only when there are no IR changes inbetween queries. BatchAAResults is
/// reusing the same `AAQueryInfo` to preserve the state across queries,
/// esentially making AA work in "batch mode". The internal state cannot be
/// cleared, so to go "out-of-batch-mode", the user must either use AAResults,
/// or create a new BatchAAResults.
class BatchAAResults {
  AAResults &AA;
  AAQueryInfo AAQI;
  SimpleCaptureInfo SimpleCI;

public:
  BatchAAResults(AAResults &AAR) : AA(AAR), AAQI(AAR, &SimpleCI) {}
  BatchAAResults(AAResults &AAR, CaptureInfo *CI) : AA(AAR), AAQI(AAR, CI) {}

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return AA.alias(LocA, LocB, AAQI);
  }
  bool pointsToConstantMemory(const MemoryLocation &Loc, bool OrLocal = false) {
    return isNoModRef(AA.getModRefInfoMask(Loc, AAQI, OrLocal));
  }
  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                               bool IgnoreLocals = false) {
    return AA.getModRefInfoMask(Loc, AAQI, IgnoreLocals);
  }
  ModRefInfo getModRefInfo(const Instruction *I,
                           const std::optional<MemoryLocation> &OptLoc) {
    return AA.getModRefInfo(I, OptLoc, AAQI);
  }
  ModRefInfo getModRefInfo(const Instruction *I, const CallBase *Call2) {
    return AA.getModRefInfo(I, Call2, AAQI);
  }
  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
    return AA.getArgModRefInfo(Call, ArgIdx);
  }
  MemoryEffects getMemoryEffects(const CallBase *Call) {
    return AA.getMemoryEffects(Call, AAQI);
  }
  bool isMustAlias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    return alias(LocA, LocB) == AliasResult::MustAlias;
  }
  bool isMustAlias(const Value *V1, const Value *V2) {
    return alias(MemoryLocation(V1, LocationSize::precise(1)),
                 MemoryLocation(V2, LocationSize::precise(1))) ==
           AliasResult::MustAlias;
  }
  ModRefInfo callCapturesBefore(const Instruction *I,
                                const MemoryLocation &MemLoc,
                                DominatorTree *DT) {
    return AA.callCapturesBefore(I, MemLoc, DT, AAQI);
  }

  /// Assume that values may come from different cycle iterations.
  void enableCrossIterationMode() {
    AAQI.MayBeCrossIteration = true;
  }

  /// Disable the use of the dominator tree during alias analysis queries.
  void disableDominatorTree() { AAQI.UseDominatorTree = false; }
};

/// Temporary typedef for legacy code that uses a generic \c AliasAnalysis
/// pointer or reference.
using AliasAnalysis = AAResults;

/// A private abstract base class describing the concept of an individual alias
/// analysis implementation.
///
/// This interface is implemented by any \c Model instantiation. It is also the
/// interface which a type used to instantiate the model must provide.
///
/// All of these methods model methods by the same name in the \c
/// AAResults class. Only differences and specifics to how the
/// implementations are called are documented here.
class AAResults::Concept {
public:
  virtual ~Concept() = 0;

  //===--------------------------------------------------------------------===//
  /// \name Alias Queries
  /// @{

  /// The main low level interface to the alias analysis implementation.
  /// Returns an AliasResult indicating whether the two pointers are aliased to
  /// each other. This is the interface that must be implemented by specific
  /// alias analysis implementations.
  virtual AliasResult alias(const MemoryLocation &LocA,
                            const MemoryLocation &LocB, AAQueryInfo &AAQI,
                            const Instruction *CtxI) = 0;

  /// @}
  //===--------------------------------------------------------------------===//
  /// \name Simple mod/ref information
  /// @{

  /// Returns a bitmask that should be unconditionally applied to the ModRef
  /// info of a memory location. This allows us to eliminate Mod and/or Ref from
  /// the ModRef info based on the knowledge that the memory location points to
  /// constant and/or locally-invariant memory.
  virtual ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                       AAQueryInfo &AAQI,
                                       bool IgnoreLocals) = 0;

  /// Get the ModRef info associated with a pointer argument of a callsite. The
  /// result's bits are set to indicate the allowed aliasing ModRef kinds. Note
  /// that these bits do not necessarily account for the overall behavior of
  /// the function, but rather only provide additional per-argument
  /// information.
  virtual ModRefInfo getArgModRefInfo(const CallBase *Call,
                                      unsigned ArgIdx) = 0;

  /// Return the behavior of the given call site.
  virtual MemoryEffects getMemoryEffects(const CallBase *Call,
                                         AAQueryInfo &AAQI) = 0;

  /// Return the behavior when calling the given function.
  virtual MemoryEffects getMemoryEffects(const Function *F) = 0;

  /// getModRefInfo (for call sites) - Return information about whether
  /// a particular call site modifies or reads the specified memory location.
  virtual ModRefInfo getModRefInfo(const CallBase *Call,
                                   const MemoryLocation &Loc,
                                   AAQueryInfo &AAQI) = 0;

  /// Return information about whether two call sites may refer to the same set
  /// of memory locations. See the AA documentation for details:
  ///   http://llvm.org/docs/AliasAnalysis.html#ModRefInfo
  virtual ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                                   AAQueryInfo &AAQI) = 0;

  /// @}
};

/// A private class template which derives from \c Concept and wraps some other
/// type.
///
/// This models the concept by directly forwarding each interface point to the
/// wrapped type which must implement a compatible interface. This provides
/// a type erased binding.
template <typename AAResultT> class AAResults::Model final : public Concept {
  AAResultT &Result;

public:
  explicit Model(AAResultT &Result, AAResults &AAR) : Result(Result) {}
  ~Model() override = default;

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *CtxI) override {
    return Result.alias(LocA, LocB, AAQI, CtxI);
  }

  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals) override {
    return Result.getModRefInfoMask(Loc, AAQI, IgnoreLocals);
  }

  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) override {
    return Result.getArgModRefInfo(Call, ArgIdx);
  }

  MemoryEffects getMemoryEffects(const CallBase *Call,
                                 AAQueryInfo &AAQI) override {
    return Result.getMemoryEffects(Call, AAQI);
  }

  MemoryEffects getMemoryEffects(const Function *F) override {
    return Result.getMemoryEffects(F);
  }

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) override {
    return Result.getModRefInfo(Call, Loc, AAQI);
  }

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI) override {
    return Result.getModRefInfo(Call1, Call2, AAQI);
  }
};

/// A base class to help implement the function alias analysis results concept.
///
/// Because of the nature of many alias analysis implementations, they often
/// only implement a subset of the interface. This base class will attempt to
/// implement the remaining portions of the interface in terms of simpler forms
/// of the interface where possible, and otherwise provide conservatively
/// correct fallback implementations.
///
/// Implementors of an alias analysis should derive from this class, and then
/// override specific methods that they wish to customize. There is no need to
/// use virtual anywhere.
class AAResultBase {
protected:
  explicit AAResultBase() = default;

  // Provide all the copy and move constructors so that derived types aren't
  // constrained.
  AAResultBase(const AAResultBase &Arg) {}
  AAResultBase(AAResultBase &&Arg) {}

public:
  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB,
                    AAQueryInfo &AAQI, const Instruction *I) {
    return AliasResult::MayAlias;
  }

  ModRefInfo getModRefInfoMask(const MemoryLocation &Loc, AAQueryInfo &AAQI,
                               bool IgnoreLocals) {
    return ModRefInfo::ModRef;
  }

  ModRefInfo getArgModRefInfo(const CallBase *Call, unsigned ArgIdx) {
    return ModRefInfo::ModRef;
  }

  MemoryEffects getMemoryEffects(const CallBase *Call, AAQueryInfo &AAQI) {
    return MemoryEffects::unknown();
  }

  MemoryEffects getMemoryEffects(const Function *F) {
    return MemoryEffects::unknown();
  }

  ModRefInfo getModRefInfo(const CallBase *Call, const MemoryLocation &Loc,
                           AAQueryInfo &AAQI) {
    return ModRefInfo::ModRef;
  }

  ModRefInfo getModRefInfo(const CallBase *Call1, const CallBase *Call2,
                           AAQueryInfo &AAQI) {
    return ModRefInfo::ModRef;
  }
};

/// Return true if this pointer is returned by a noalias function.
bool isNoAliasCall(const Value *V);

/// Return true if this pointer refers to a distinct and identifiable object.
/// This returns true for:
///    Global Variables and Functions (but not Global Aliases)
///    Allocas
///    ByVal and NoAlias Arguments
///    NoAlias returns (e.g. calls to malloc)
///
bool isIdentifiedObject(const Value *V);

/// Return true if V is umabigously identified at the function-level.
/// Different IdentifiedFunctionLocals can't alias.
/// Further, an IdentifiedFunctionLocal can not alias with any function
/// arguments other than itself, which is not necessarily true for
/// IdentifiedObjects.
bool isIdentifiedFunctionLocal(const Value *V);

/// Returns true if the pointer is one which would have been considered an
/// escape by isNonEscapingLocalObject.
bool isEscapeSource(const Value *V);

/// Return true if Object memory is not visible after an unwind, in the sense
/// that program semantics cannot depend on Object containing any particular
/// value on unwind. If the RequiresNoCaptureBeforeUnwind out parameter is set
/// to true, then the memory is only not visible if the object has not been
/// captured prior to the unwind. Otherwise it is not visible even if captured.
bool isNotVisibleOnUnwind(const Value *Object,
                          bool &RequiresNoCaptureBeforeUnwind);

/// Return true if the Object is writable, in the sense that any location based
/// on this pointer that can be loaded can also be stored to without trapping.
/// Additionally, at the point Object is declared, stores can be introduced
/// without data races. At later points, this is only the case if the pointer
/// can not escape to a different thread.
///
/// If ExplicitlyDereferenceableOnly is set to true, this property only holds
/// for the part of Object that is explicitly marked as dereferenceable, e.g.
/// using the dereferenceable(N) attribute. It does not necessarily hold for
/// parts that are only known to be dereferenceable due to the presence of
/// loads.
bool isWritableObject(const Value *Object, bool &ExplicitlyDereferenceableOnly);

/// A manager for alias analyses.
///
/// This class can have analyses registered with it and when run, it will run
/// all of them and aggregate their results into single AA results interface
/// that dispatches across all of the alias analysis results available.
///
/// Note that the order in which analyses are registered is very significant.
/// That is the order in which the results will be aggregated and queried.
///
/// This manager effectively wraps the AnalysisManager for registering alias
/// analyses. When you register your alias analysis with this manager, it will
/// ensure the analysis itself is registered with its AnalysisManager.
///
/// The result of this analysis is only invalidated if one of the particular
/// aggregated AA results end up being invalidated. This removes the need to
/// explicitly preserve the results of `AAManager`. Note that analyses should no
/// longer be registered once the `AAManager` is run.
class AAManager : public AnalysisInfoMixin<AAManager> {
public:
  using Result = AAResults;

  /// Register a specific AA result.
  template <typename AnalysisT> void registerFunctionAnalysis() {
    ResultGetters.push_back(&getFunctionAAResultImpl<AnalysisT>);
  }

  /// Register a specific AA result.
  template <typename AnalysisT> void registerModuleAnalysis() {
    ResultGetters.push_back(&getModuleAAResultImpl<AnalysisT>);
  }

  Result run(Function &F, FunctionAnalysisManager &AM);

private:
  friend AnalysisInfoMixin<AAManager>;

  static AnalysisKey Key;

  SmallVector<void (*)(Function &F, FunctionAnalysisManager &AM,
                       AAResults &AAResults),
              4> ResultGetters;

  template <typename AnalysisT>
  static void getFunctionAAResultImpl(Function &F,
                                      FunctionAnalysisManager &AM,
                                      AAResults &AAResults) {
    AAResults.addAAResult(AM.template getResult<AnalysisT>(F));
    AAResults.addAADependencyID(AnalysisT::ID());
  }

  template <typename AnalysisT>
  static void getModuleAAResultImpl(Function &F, FunctionAnalysisManager &AM,
                                    AAResults &AAResults) {
    auto &MAMProxy = AM.getResult<ModuleAnalysisManagerFunctionProxy>(F);
    if (auto *R =
            MAMProxy.template getCachedResult<AnalysisT>(*F.getParent())) {
      AAResults.addAAResult(*R);
      MAMProxy
          .template registerOuterAnalysisInvalidation<AnalysisT, AAManager>();
    }
  }
};

/// A wrapper pass to provide the legacy pass manager access to a suitably
/// prepared AAResults object.
class AAResultsWrapperPass : public FunctionPass {
  std::unique_ptr<AAResults> AAR;

public:
  static char ID;

  AAResultsWrapperPass();

  AAResults &getAAResults() { return *AAR; }
  const AAResults &getAAResults() const { return *AAR; }

  bool runOnFunction(Function &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// A wrapper pass for external alias analyses. This just squirrels away the
/// callback used to run any analyses and register their results.
struct ExternalAAWrapperPass : ImmutablePass {
  using CallbackT = std::function<void(Pass &, Function &, AAResults &)>;

  CallbackT CB;

  static char ID;

  ExternalAAWrapperPass();

  explicit ExternalAAWrapperPass(CallbackT CB);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

/// A wrapper pass around a callback which can be used to populate the
/// AAResults in the AAResultsWrapperPass from an external AA.
///
/// The callback provided here will be used each time we prepare an AAResults
/// object, and will receive a reference to the function wrapper pass, the
/// function, and the AAResults object to populate. This should be used when
/// setting up a custom pass pipeline to inject a hook into the AA results.
ImmutablePass *createExternalAAWrapperPass(
    std::function<void(Pass &, Function &, AAResults &)> Callback);

} // end namespace llvm

#endif // LLVM_ANALYSIS_ALIASANALYSIS_H
