//===- llvm/Analysis/AssumptionCache.h - Track @llvm.assume -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that keeps track of @llvm.assume intrinsics in
// the functions of a module (allowing assumptions within any function to be
// found cheaply by other parts of the optimizer).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ASSUMPTIONCACHE_H
#define LLVM_ANALYSIS_ASSUMPTIONCACHE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include <memory>

namespace llvm {

class AssumeInst;
class Function;
class raw_ostream;
class TargetTransformInfo;
class Value;

/// A cache of \@llvm.assume calls within a function.
///
/// This cache provides fast lookup of assumptions within a function by caching
/// them and amortizing the cost of scanning for them across all queries. Passes
/// that create new assumptions are required to call registerAssumption() to
/// register any new \@llvm.assume calls that they create. Deletions of
/// \@llvm.assume calls do not require special handling.
class AssumptionCache {
public:
  /// Value of ResultElem::Index indicating that the argument to the call of the
  /// llvm.assume.
  enum : unsigned { ExprResultIdx = std::numeric_limits<unsigned>::max() };

  struct ResultElem {
    WeakVH Assume;

    /// contains either ExprResultIdx or the index of the operand bundle
    /// containing the knowledge.
    unsigned Index;
    operator Value *() const { return Assume; }
  };

private:
  /// The function for which this cache is handling assumptions.
  ///
  /// We track this to lazily populate our assumptions.
  Function &F;

  TargetTransformInfo *TTI;

  /// Vector of weak value handles to calls of the \@llvm.assume
  /// intrinsic.
  SmallVector<ResultElem, 4> AssumeHandles;

  class AffectedValueCallbackVH final : public CallbackVH {
    AssumptionCache *AC;

    void deleted() override;
    void allUsesReplacedWith(Value *) override;

  public:
    using DMI = DenseMapInfo<Value *>;

    AffectedValueCallbackVH(Value *V, AssumptionCache *AC = nullptr)
        : CallbackVH(V), AC(AC) {}
  };

  friend AffectedValueCallbackVH;

  /// A map of values about which an assumption might be providing
  /// information to the relevant set of assumptions.
  using AffectedValuesMap =
      DenseMap<AffectedValueCallbackVH, SmallVector<ResultElem, 1>,
               AffectedValueCallbackVH::DMI>;
  AffectedValuesMap AffectedValues;

  /// Get the vector of assumptions which affect a value from the cache.
  SmallVector<ResultElem, 1> &getOrInsertAffectedValues(Value *V);

  /// Move affected values in the cache for OV to be affected values for NV.
  void transferAffectedValuesInCache(Value *OV, Value *NV);

  /// Flag tracking whether we have scanned the function yet.
  ///
  /// We want to be as lazy about this as possible, and so we scan the function
  /// at the last moment.
  bool Scanned = false;

  /// Scan the function for assumptions and add them to the cache.
  void scanFunction();

public:
  /// Construct an AssumptionCache from a function by scanning all of
  /// its instructions.
  AssumptionCache(Function &F, TargetTransformInfo *TTI = nullptr)
      : F(F), TTI(TTI) {}

  /// This cache is designed to be self-updating and so it should never be
  /// invalidated.
  bool invalidate(Function &, const PreservedAnalyses &,
                  FunctionAnalysisManager::Invalidator &) {
    return false;
  }

  /// Add an \@llvm.assume intrinsic to this function's cache.
  ///
  /// The call passed in must be an instruction within this function and must
  /// not already be in the cache.
  void registerAssumption(AssumeInst *CI);

  /// Remove an \@llvm.assume intrinsic from this function's cache if it has
  /// been added to the cache earlier.
  void unregisterAssumption(AssumeInst *CI);

  /// Update the cache of values being affected by this assumption (i.e.
  /// the values about which this assumption provides information).
  void updateAffectedValues(AssumeInst *CI);

  /// Clear the cache of \@llvm.assume intrinsics for a function.
  ///
  /// It will be re-scanned the next time it is requested.
  void clear() {
    AssumeHandles.clear();
    AffectedValues.clear();
    Scanned = false;
  }

  /// Access the list of assumption handles currently tracked for this
  /// function.
  ///
  /// Note that these produce weak handles that may be null. The caller must
  /// handle that case.
  /// FIXME: We should replace this with pointee_iterator<filter_iterator<...>>
  /// when we can write that to filter out the null values. Then caller code
  /// will become simpler.
  MutableArrayRef<ResultElem> assumptions() {
    if (!Scanned)
      scanFunction();
    return AssumeHandles;
  }

  /// Access the list of assumptions which affect this value.
  MutableArrayRef<ResultElem> assumptionsFor(const Value *V) {
    if (!Scanned)
      scanFunction();

    auto AVI = AffectedValues.find_as(const_cast<Value *>(V));
    if (AVI == AffectedValues.end())
      return MutableArrayRef<ResultElem>();

    return AVI->second;
  }
};

/// A function analysis which provides an \c AssumptionCache.
///
/// This analysis is intended for use with the new pass manager and will vend
/// assumption caches for a given function.
class AssumptionAnalysis : public AnalysisInfoMixin<AssumptionAnalysis> {
  friend AnalysisInfoMixin<AssumptionAnalysis>;

  static AnalysisKey Key;

public:
  using Result = AssumptionCache;

  AssumptionCache run(Function &F, FunctionAnalysisManager &);
};

/// Printer pass for the \c AssumptionAnalysis results.
class AssumptionPrinterPass : public PassInfoMixin<AssumptionPrinterPass> {
  raw_ostream &OS;

public:
  explicit AssumptionPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

/// An immutable pass that tracks lazily created \c AssumptionCache
/// objects.
///
/// This is essentially a workaround for the legacy pass manager's weaknesses
/// which associates each assumption cache with Function and clears it if the
/// function is deleted. The nature of the AssumptionCache is that it is not
/// invalidated by any changes to the function body and so this is sufficient
/// to be conservatively correct.
class AssumptionCacheTracker : public ImmutablePass {
  /// A callback value handle applied to function objects, which we use to
  /// delete our cache of intrinsics for a function when it is deleted.
  class FunctionCallbackVH final : public CallbackVH {
    AssumptionCacheTracker *ACT;

    void deleted() override;

  public:
    using DMI = DenseMapInfo<Value *>;

    FunctionCallbackVH(Value *V, AssumptionCacheTracker *ACT = nullptr)
        : CallbackVH(V), ACT(ACT) {}
  };

  friend FunctionCallbackVH;

  using FunctionCallsMap =
      DenseMap<FunctionCallbackVH, std::unique_ptr<AssumptionCache>,
               FunctionCallbackVH::DMI>;

  FunctionCallsMap AssumptionCaches;

public:
  /// Get the cached assumptions for a function.
  ///
  /// If no assumptions are cached, this will scan the function. Otherwise, the
  /// existing cache will be returned.
  AssumptionCache &getAssumptionCache(Function &F);

  /// Return the cached assumptions for a function if it has already been
  /// scanned. Otherwise return nullptr.
  AssumptionCache *lookupAssumptionCache(Function &F);

  AssumptionCacheTracker();
  ~AssumptionCacheTracker() override;

  void releaseMemory() override {
    verifyAnalysis();
    AssumptionCaches.shrink_and_clear();
  }

  void verifyAnalysis() const override;

  bool doFinalization(Module &) override {
    verifyAnalysis();
    return false;
  }

  static char ID; // Pass identification, replacement for typeid
};

template<> struct simplify_type<AssumptionCache::ResultElem> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(AssumptionCache::ResultElem &Val) {
    return Val;
  }
};
template<> struct simplify_type<const AssumptionCache::ResultElem> {
  using SimpleType = /*const*/ Value *;

  static SimpleType getSimplifiedValue(const AssumptionCache::ResultElem &Val) {
    return Val;
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_ASSUMPTIONCACHE_H
