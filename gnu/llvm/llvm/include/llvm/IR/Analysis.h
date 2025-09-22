//===- Analysis.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Pass manager infrastructure for declaring and invalidating analyses.
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_ANALYSIS_H
#define LLVM_IR_ANALYSIS_H

#include "llvm/ADT/SmallPtrSet.h"

namespace llvm {

class Function;
class Module;

/// A special type used by analysis passes to provide an address that
/// identifies that particular analysis pass type.
///
/// Analysis passes should have a static data member of this type and derive
/// from the \c AnalysisInfoMixin to get a static ID method used to identify
/// the analysis in the pass management infrastructure.
struct alignas(8) AnalysisKey {};

/// A special type used to provide an address that identifies a set of related
/// analyses.  These sets are primarily used below to mark sets of analyses as
/// preserved.
///
/// For example, a transformation can indicate that it preserves the CFG of a
/// function by preserving the appropriate AnalysisSetKey.  An analysis that
/// depends only on the CFG can then check if that AnalysisSetKey is preserved;
/// if it is, the analysis knows that it itself is preserved.
struct alignas(8) AnalysisSetKey {};

/// This templated class represents "all analyses that operate over \<a
/// particular IR unit\>" (e.g. a Function or a Module) in instances of
/// PreservedAnalysis.
///
/// This lets a transformation say e.g. "I preserved all function analyses".
///
/// Note that you must provide an explicit instantiation declaration and
/// definition for this template in order to get the correct behavior on
/// Windows. Otherwise, the address of SetKey will not be stable.
template <typename IRUnitT> class AllAnalysesOn {
public:
  static AnalysisSetKey *ID() { return &SetKey; }

private:
  static AnalysisSetKey SetKey;
};

template <typename IRUnitT> AnalysisSetKey AllAnalysesOn<IRUnitT>::SetKey;

extern template class AllAnalysesOn<Module>;
extern template class AllAnalysesOn<Function>;

/// Represents analyses that only rely on functions' control flow.
///
/// This can be used with \c PreservedAnalyses to mark the CFG as preserved and
/// to query whether it has been preserved.
///
/// The CFG of a function is defined as the set of basic blocks and the edges
/// between them. Changing the set of basic blocks in a function is enough to
/// mutate the CFG. Mutating the condition of a branch or argument of an
/// invoked function does not mutate the CFG, but changing the successor labels
/// of those instructions does.
class CFGAnalyses {
public:
  static AnalysisSetKey *ID() { return &SetKey; }

private:
  static AnalysisSetKey SetKey;
};

/// A set of analyses that are preserved following a run of a transformation
/// pass.
///
/// Transformation passes build and return these objects to communicate which
/// analyses are still valid after the transformation. For most passes this is
/// fairly simple: if they don't change anything all analyses are preserved,
/// otherwise only a short list of analyses that have been explicitly updated
/// are preserved.
///
/// This class also lets transformation passes mark abstract *sets* of analyses
/// as preserved. A transformation that (say) does not alter the CFG can
/// indicate such by marking a particular AnalysisSetKey as preserved, and
/// then analyses can query whether that AnalysisSetKey is preserved.
///
/// Finally, this class can represent an "abandoned" analysis, which is
/// not preserved even if it would be covered by some abstract set of analyses.
///
/// Given a `PreservedAnalyses` object, an analysis will typically want to
/// figure out whether it is preserved. In the example below, MyAnalysisType is
/// preserved if it's not abandoned, and (a) it's explicitly marked as
/// preserved, (b), the set AllAnalysesOn<MyIRUnit> is preserved, or (c) both
/// AnalysisSetA and AnalysisSetB are preserved.
///
/// ```
///   auto PAC = PA.getChecker<MyAnalysisType>();
///   if (PAC.preserved() || PAC.preservedSet<AllAnalysesOn<MyIRUnit>>() ||
///       (PAC.preservedSet<AnalysisSetA>() &&
///        PAC.preservedSet<AnalysisSetB>())) {
///     // The analysis has been successfully preserved ...
///   }
/// ```
class PreservedAnalyses {
public:
  /// Convenience factory function for the empty preserved set.
  static PreservedAnalyses none() { return PreservedAnalyses(); }

  /// Construct a special preserved set that preserves all passes.
  static PreservedAnalyses all() {
    PreservedAnalyses PA;
    PA.PreservedIDs.insert(&AllAnalysesKey);
    return PA;
  }

  /// Construct a preserved analyses object with a single preserved set.
  template <typename AnalysisSetT> static PreservedAnalyses allInSet() {
    PreservedAnalyses PA;
    PA.preserveSet<AnalysisSetT>();
    return PA;
  }

  /// Mark an analysis as preserved.
  template <typename AnalysisT> void preserve() { preserve(AnalysisT::ID()); }

  /// Given an analysis's ID, mark the analysis as preserved, adding it
  /// to the set.
  void preserve(AnalysisKey *ID) {
    // Clear this ID from the explicit not-preserved set if present.
    NotPreservedAnalysisIDs.erase(ID);

    // If we're not already preserving all analyses (other than those in
    // NotPreservedAnalysisIDs).
    if (!areAllPreserved())
      PreservedIDs.insert(ID);
  }

  /// Mark an analysis set as preserved.
  template <typename AnalysisSetT> void preserveSet() {
    preserveSet(AnalysisSetT::ID());
  }

  /// Mark an analysis set as preserved using its ID.
  void preserveSet(AnalysisSetKey *ID) {
    // If we're not already in the saturated 'all' state, add this set.
    if (!areAllPreserved())
      PreservedIDs.insert(ID);
  }

  /// Mark an analysis as abandoned.
  ///
  /// An abandoned analysis is not preserved, even if it is nominally covered
  /// by some other set or was previously explicitly marked as preserved.
  ///
  /// Note that you can only abandon a specific analysis, not a *set* of
  /// analyses.
  template <typename AnalysisT> void abandon() { abandon(AnalysisT::ID()); }

  /// Mark an analysis as abandoned using its ID.
  ///
  /// An abandoned analysis is not preserved, even if it is nominally covered
  /// by some other set or was previously explicitly marked as preserved.
  ///
  /// Note that you can only abandon a specific analysis, not a *set* of
  /// analyses.
  void abandon(AnalysisKey *ID) {
    PreservedIDs.erase(ID);
    NotPreservedAnalysisIDs.insert(ID);
  }

  /// Intersect this set with another in place.
  ///
  /// This is a mutating operation on this preserved set, removing all
  /// preserved passes which are not also preserved in the argument.
  void intersect(const PreservedAnalyses &Arg) {
    if (Arg.areAllPreserved())
      return;
    if (areAllPreserved()) {
      *this = Arg;
      return;
    }
    // The intersection requires the *union* of the explicitly not-preserved
    // IDs and the *intersection* of the preserved IDs.
    for (auto *ID : Arg.NotPreservedAnalysisIDs) {
      PreservedIDs.erase(ID);
      NotPreservedAnalysisIDs.insert(ID);
    }
    PreservedIDs.remove_if(
        [&](void *ID) { return !Arg.PreservedIDs.contains(ID); });
  }

  /// Intersect this set with a temporary other set in place.
  ///
  /// This is a mutating operation on this preserved set, removing all
  /// preserved passes which are not also preserved in the argument.
  void intersect(PreservedAnalyses &&Arg) {
    if (Arg.areAllPreserved())
      return;
    if (areAllPreserved()) {
      *this = std::move(Arg);
      return;
    }
    // The intersection requires the *union* of the explicitly not-preserved
    // IDs and the *intersection* of the preserved IDs.
    for (auto *ID : Arg.NotPreservedAnalysisIDs) {
      PreservedIDs.erase(ID);
      NotPreservedAnalysisIDs.insert(ID);
    }
    PreservedIDs.remove_if(
        [&](void *ID) { return !Arg.PreservedIDs.contains(ID); });
  }

  /// A checker object that makes it easy to query for whether an analysis or
  /// some set covering it is preserved.
  class PreservedAnalysisChecker {
    friend class PreservedAnalyses;

    const PreservedAnalyses &PA;
    AnalysisKey *const ID;
    const bool IsAbandoned;

    /// A PreservedAnalysisChecker is tied to a particular Analysis because
    /// `preserved()` and `preservedSet()` both return false if the Analysis
    /// was abandoned.
    PreservedAnalysisChecker(const PreservedAnalyses &PA, AnalysisKey *ID)
        : PA(PA), ID(ID), IsAbandoned(PA.NotPreservedAnalysisIDs.count(ID)) {}

  public:
    /// Returns true if the checker's analysis was not abandoned and either
    ///  - the analysis is explicitly preserved or
    ///  - all analyses are preserved.
    bool preserved() {
      return !IsAbandoned && (PA.PreservedIDs.count(&AllAnalysesKey) ||
                              PA.PreservedIDs.count(ID));
    }

    /// Return true if the checker's analysis was not abandoned, i.e. it was not
    /// explicitly invalidated. Even if the analysis is not explicitly
    /// preserved, if the analysis is known stateless, then it is preserved.
    bool preservedWhenStateless() { return !IsAbandoned; }

    /// Returns true if the checker's analysis was not abandoned and either
    ///  - \p AnalysisSetT is explicitly preserved or
    ///  - all analyses are preserved.
    template <typename AnalysisSetT> bool preservedSet() {
      AnalysisSetKey *SetID = AnalysisSetT::ID();
      return !IsAbandoned && (PA.PreservedIDs.count(&AllAnalysesKey) ||
                              PA.PreservedIDs.count(SetID));
    }
  };

  /// Build a checker for this `PreservedAnalyses` and the specified analysis
  /// type.
  ///
  /// You can use the returned object to query whether an analysis was
  /// preserved. See the example in the comment on `PreservedAnalysis`.
  template <typename AnalysisT> PreservedAnalysisChecker getChecker() const {
    return PreservedAnalysisChecker(*this, AnalysisT::ID());
  }

  /// Build a checker for this `PreservedAnalyses` and the specified analysis
  /// ID.
  ///
  /// You can use the returned object to query whether an analysis was
  /// preserved. See the example in the comment on `PreservedAnalysis`.
  PreservedAnalysisChecker getChecker(AnalysisKey *ID) const {
    return PreservedAnalysisChecker(*this, ID);
  }

  /// Test whether all analyses are preserved (and none are abandoned).
  ///
  /// This is used primarily to optimize for the common case of a transformation
  /// which makes no changes to the IR.
  bool areAllPreserved() const {
    return NotPreservedAnalysisIDs.empty() &&
           PreservedIDs.count(&AllAnalysesKey);
  }

  /// Directly test whether a set of analyses is preserved.
  ///
  /// This is only true when no analyses have been explicitly abandoned.
  template <typename AnalysisSetT> bool allAnalysesInSetPreserved() const {
    return allAnalysesInSetPreserved(AnalysisSetT::ID());
  }

  /// Directly test whether a set of analyses is preserved.
  ///
  /// This is only true when no analyses have been explicitly abandoned.
  bool allAnalysesInSetPreserved(AnalysisSetKey *SetID) const {
    return NotPreservedAnalysisIDs.empty() &&
           (PreservedIDs.count(&AllAnalysesKey) || PreservedIDs.count(SetID));
  }

private:
  /// A special key used to indicate all analyses.
  static AnalysisSetKey AllAnalysesKey;

  /// The IDs of analyses and analysis sets that are preserved.
  SmallPtrSet<void *, 2> PreservedIDs;

  /// The IDs of explicitly not-preserved analyses.
  ///
  /// If an analysis in this set is covered by a set in `PreservedIDs`, we
  /// consider it not-preserved. That is, `NotPreservedAnalysisIDs` always
  /// "wins" over analysis sets in `PreservedIDs`.
  ///
  /// Also, a given ID should never occur both here and in `PreservedIDs`.
  SmallPtrSet<AnalysisKey *, 2> NotPreservedAnalysisIDs;
};
} // namespace llvm

#endif
