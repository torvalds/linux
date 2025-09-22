//===- llvm/Analysis/LoopCacheAnalysis.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interface for the loop cache analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPCACHEANALYSIS_H
#define LLVM_ANALYSIS_LOOPCACHEANALYSIS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include <optional>

namespace llvm {

class AAResults;
class DependenceInfo;
class Instruction;
class LPMUpdater;
class raw_ostream;
class LoopInfo;
class Loop;
class ScalarEvolution;
class SCEV;
class TargetTransformInfo;

using CacheCostTy = int64_t;
using LoopVectorTy = SmallVector<Loop *, 8>;

/// Represents a memory reference as a base pointer and a set of indexing
/// operations. For example given the array reference A[i][2j+1][3k+2] in a
/// 3-dim loop nest:
///   for(i=0;i<n;++i)
///     for(j=0;j<m;++j)
///       for(k=0;k<o;++k)
///         ... A[i][2j+1][3k+2] ...
/// We expect:
///   BasePointer -> A
///   Subscripts -> [{0,+,1}<%for.i>][{1,+,2}<%for.j>][{2,+,3}<%for.k>]
///   Sizes -> [m][o][4]
class IndexedReference {
  friend raw_ostream &operator<<(raw_ostream &OS, const IndexedReference &R);

public:
  /// Construct an indexed reference given a \p StoreOrLoadInst instruction.
  IndexedReference(Instruction &StoreOrLoadInst, const LoopInfo &LI,
                   ScalarEvolution &SE);

  bool isValid() const { return IsValid; }
  const SCEV *getBasePointer() const { return BasePointer; }
  size_t getNumSubscripts() const { return Subscripts.size(); }
  const SCEV *getSubscript(unsigned SubNum) const {
    assert(SubNum < getNumSubscripts() && "Invalid subscript number");
    return Subscripts[SubNum];
  }
  const SCEV *getFirstSubscript() const {
    assert(!Subscripts.empty() && "Expecting non-empty container");
    return Subscripts.front();
  }
  const SCEV *getLastSubscript() const {
    assert(!Subscripts.empty() && "Expecting non-empty container");
    return Subscripts.back();
  }

  /// Return true/false if the current object and the indexed reference \p Other
  /// are/aren't in the same cache line of size \p CLS. Two references are in
  /// the same chace line iff the distance between them in the innermost
  /// dimension is less than the cache line size. Return std::nullopt if unsure.
  std::optional<bool> hasSpacialReuse(const IndexedReference &Other,
                                      unsigned CLS, AAResults &AA) const;

  /// Return true if the current object and the indexed reference \p Other
  /// have distance smaller than \p MaxDistance in the dimension associated with
  /// the given loop \p L. Return false if the distance is not smaller than \p
  /// MaxDistance and std::nullopt if unsure.
  std::optional<bool> hasTemporalReuse(const IndexedReference &Other,
                                       unsigned MaxDistance, const Loop &L,
                                       DependenceInfo &DI, AAResults &AA) const;

  /// Compute the cost of the reference w.r.t. the given loop \p L when it is
  /// considered in the innermost position in the loop nest.
  /// The cost is defined as:
  ///   - equal to one if the reference is loop invariant, or
  ///   - equal to '(TripCount * stride) / cache_line_size' if:
  ///     + the reference stride is less than the cache line size, and
  ///     + the coefficient of this loop's index variable used in all other
  ///       subscripts is zero
  ///   - or otherwise equal to 'TripCount'.
  CacheCostTy computeRefCost(const Loop &L, unsigned CLS) const;

private:
  /// Attempt to delinearize the indexed reference.
  bool delinearize(const LoopInfo &LI);

  /// Attempt to delinearize \p AccessFn for fixed-size arrays.
  bool tryDelinearizeFixedSize(const SCEV *AccessFn,
                               SmallVectorImpl<const SCEV *> &Subscripts);

  /// Return true if the index reference is invariant with respect to loop \p L.
  bool isLoopInvariant(const Loop &L) const;

  /// Return true if the indexed reference is 'consecutive' in loop \p L.
  /// An indexed reference is 'consecutive' if the only coefficient that uses
  /// the loop induction variable is the rightmost one, and the access stride is
  /// smaller than the cache line size \p CLS. Provide a valid \p Stride value
  /// if the indexed reference is 'consecutive'.
  bool isConsecutive(const Loop &L, const SCEV *&Stride, unsigned CLS) const;

  /// Retrieve the index of the subscript corresponding to the given loop \p
  /// L. Return a zero-based positive index if the subscript index is
  /// succesfully located and a negative value otherwise. For example given the
  /// indexed reference 'A[i][2j+1][3k+2]', the call
  /// 'getSubscriptIndex(loop-k)' would return value 2.
  int getSubscriptIndex(const Loop &L) const;

  /// Return the coefficient used in the rightmost dimension.
  const SCEV *getLastCoefficient() const;

  /// Return true if the coefficient corresponding to induction variable of
  /// loop \p L in the given \p Subscript is zero or is loop invariant in \p L.
  bool isCoeffForLoopZeroOrInvariant(const SCEV &Subscript,
                                     const Loop &L) const;

  /// Verify that the given \p Subscript is 'well formed' (must be a simple add
  /// recurrence).
  bool isSimpleAddRecurrence(const SCEV &Subscript, const Loop &L) const;

  /// Return true if the given reference \p Other is definetely aliased with
  /// the indexed reference represented by this class.
  bool isAliased(const IndexedReference &Other, AAResults &AA) const;

private:
  /// True if the reference can be delinearized, false otherwise.
  bool IsValid = false;

  /// Represent the memory reference instruction.
  Instruction &StoreOrLoadInst;

  /// The base pointer of the memory reference.
  const SCEV *BasePointer = nullptr;

  /// The subscript (indexes) of the memory reference.
  SmallVector<const SCEV *, 3> Subscripts;

  /// The dimensions of the memory reference.
  SmallVector<const SCEV *, 3> Sizes;

  ScalarEvolution &SE;
};

/// A reference group represents a set of memory references that exhibit
/// temporal or spacial reuse. Two references belong to the same
/// reference group with respect to a inner loop L iff:
/// 1. they have a loop independent dependency, or
/// 2. they have a loop carried dependence with a small dependence distance
///    (e.g. less than 2) carried by the inner loop, or
/// 3. they refer to the same array, and the subscript in their innermost
///    dimension is less than or equal to 'd' (where 'd' is less than the cache
///    line size)
///
/// Intuitively a reference group represents memory references that access
/// the same cache line. Conditions 1,2 above account for temporal reuse, while
/// contition 3 accounts for spacial reuse.
using ReferenceGroupTy = SmallVector<std::unique_ptr<IndexedReference>, 8>;
using ReferenceGroupsTy = SmallVector<ReferenceGroupTy, 8>;

/// \c CacheCost represents the estimated cost of a inner loop as the number of
/// cache lines used by the memory references it contains.
/// The 'cache cost' of a loop 'L' in a loop nest 'LN' is computed as the sum of
/// the cache costs of all of its reference groups when the loop is considered
/// to be in the innermost position in the nest.
/// A reference group represents memory references that fall into the same cache
/// line. Each reference group is analysed with respect to the innermost loop in
/// a loop nest. The cost of a reference is defined as follow:
///  - one if it is loop invariant w.r.t the innermost loop,
///  - equal to the loop trip count divided by the cache line times the
///    reference stride if the reference stride is less than the cache line
///    size (CLS), and the coefficient of this loop's index variable used in all
///    other subscripts is zero (e.g. RefCost = TripCount/(CLS/RefStride))
///  - equal to the innermost loop trip count if the reference stride is greater
///    or equal to the cache line size CLS.
class CacheCost {
  friend raw_ostream &operator<<(raw_ostream &OS, const CacheCost &CC);
  using LoopTripCountTy = std::pair<const Loop *, unsigned>;
  using LoopCacheCostTy = std::pair<const Loop *, CacheCostTy>;

public:
  static CacheCostTy constexpr InvalidCost = -1;

  /// Construct a CacheCost object for the loop nest described by \p Loops.
  /// The optional parameter \p TRT can be used to specify the max. distance
  /// between array elements accessed in a loop so that the elements are
  /// classified to have temporal reuse.
  CacheCost(const LoopVectorTy &Loops, const LoopInfo &LI, ScalarEvolution &SE,
            TargetTransformInfo &TTI, AAResults &AA, DependenceInfo &DI,
            std::optional<unsigned> TRT = std::nullopt);

  /// Create a CacheCost for the loop nest rooted by \p Root.
  /// The optional parameter \p TRT can be used to specify the max. distance
  /// between array elements accessed in a loop so that the elements are
  /// classified to have temporal reuse.
  static std::unique_ptr<CacheCost>
  getCacheCost(Loop &Root, LoopStandardAnalysisResults &AR, DependenceInfo &DI,
               std::optional<unsigned> TRT = std::nullopt);

  /// Return the estimated cost of loop \p L if the given loop is part of the
  /// loop nest associated with this object. Return -1 otherwise.
  CacheCostTy getLoopCost(const Loop &L) const {
    auto IT = llvm::find_if(LoopCosts, [&L](const LoopCacheCostTy &LCC) {
      return LCC.first == &L;
    });
    return (IT != LoopCosts.end()) ? (*IT).second : -1;
  }

  /// Return the estimated ordered loop costs.
  ArrayRef<LoopCacheCostTy> getLoopCosts() const { return LoopCosts; }

private:
  /// Calculate the cache footprint of each loop in the nest (when it is
  /// considered to be in the innermost position).
  void calculateCacheFootprint();

  /// Partition store/load instructions in the loop nest into reference groups.
  /// Two or more memory accesses belong in the same reference group if they
  /// share the same cache line.
  bool populateReferenceGroups(ReferenceGroupsTy &RefGroups) const;

  /// Calculate the cost of the given loop \p L assuming it is the innermost
  /// loop in nest.
  CacheCostTy computeLoopCacheCost(const Loop &L,
                                   const ReferenceGroupsTy &RefGroups) const;

  /// Compute the cost of a representative reference in reference group \p RG
  /// when the given loop \p L is considered as the innermost loop in the nest.
  /// The computed cost is an estimate for the number of cache lines used by the
  /// reference group. The representative reference cost is defined as:
  ///   - equal to one if the reference is loop invariant, or
  ///   - equal to '(TripCount * stride) / cache_line_size' if (a) loop \p L's
  ///     induction variable is used only in the reference subscript associated
  ///     with loop \p L, and (b) the reference stride is less than the cache
  ///     line size, or
  ///   - TripCount otherwise
  CacheCostTy computeRefGroupCacheCost(const ReferenceGroupTy &RG,
                                       const Loop &L) const;

  /// Sort the LoopCosts vector by decreasing cache cost.
  void sortLoopCosts() {
    stable_sort(LoopCosts,
                [](const LoopCacheCostTy &A, const LoopCacheCostTy &B) {
                  return A.second > B.second;
                });
  }

private:
  /// Loops in the loop nest associated with this object.
  LoopVectorTy Loops;

  /// Trip counts for the loops in the loop nest associated with this object.
  SmallVector<LoopTripCountTy, 3> TripCounts;

  /// Cache costs for the loops in the loop nest associated with this object.
  SmallVector<LoopCacheCostTy, 3> LoopCosts;

  /// The max. distance between array elements accessed in a loop so that the
  /// elements are classified to have temporal reuse.
  std::optional<unsigned> TRT;

  const LoopInfo &LI;
  ScalarEvolution &SE;
  TargetTransformInfo &TTI;
  AAResults &AA;
  DependenceInfo &DI;
};

raw_ostream &operator<<(raw_ostream &OS, const IndexedReference &R);
raw_ostream &operator<<(raw_ostream &OS, const CacheCost &CC);

/// Printer pass for the \c CacheCost results.
class LoopCachePrinterPass : public PassInfoMixin<LoopCachePrinterPass> {
  raw_ostream &OS;

public:
  explicit LoopCachePrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPCACHEANALYSIS_H
