//===- ConstructCompositionT.h -- Composing compound constructs -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Given a list of leaf construct, each with a set of clauses, generate the
// compound construct whose leaf constructs are the given list, and whose clause
// list is the merged lists of individual leaf clauses.
//
// *** At the moment it assumes that the individual constructs and their clauses
// *** are a subset of those created by splitting a valid compound construct.
//===----------------------------------------------------------------------===//
#ifndef LLVM_FRONTEND_OPENMP_CONSTRUCTCOMPOSITIONT_H
#define LLVM_FRONTEND_OPENMP_CONSTRUCTCOMPOSITIONT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Frontend/OpenMP/ClauseT.h"
#include "llvm/Frontend/OpenMP/OMP.h"

#include <iterator>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tomp {
template <typename ClauseType> struct ConstructCompositionT {
  using ClauseTy = ClauseType;

  using TypeTy = typename ClauseTy::TypeTy;
  using IdTy = typename ClauseTy::IdTy;
  using ExprTy = typename ClauseTy::ExprTy;

  ConstructCompositionT(uint32_t version,
                        llvm::ArrayRef<DirectiveWithClauses<ClauseTy>> leafs);

  DirectiveWithClauses<ClauseTy> merged;

private:
  // Use an ordered container, since we beed to maintain the order in which
  // clauses are added to it. This is to avoid non-deterministic output.
  using ClauseSet = ListT<ClauseTy>;

  enum class Presence {
    All,  // Clause is preesnt on all leaf constructs that allow it.
    Some, // Clause is present on some, but not on all constructs.
    None, // Clause is absent on all constructs.
  };

  template <typename S>
  ClauseTy makeClause(llvm::omp::Clause clauseId, S &&specific) {
    return typename ClauseTy::BaseT{clauseId, std::move(specific)};
  }

  llvm::omp::Directive
  makeCompound(llvm::ArrayRef<DirectiveWithClauses<ClauseTy>> parts);

  Presence checkPresence(llvm::omp::Clause clauseId);

  // There are clauses that need special handling:
  // 1. "if": the "directive-name-modifier" on the merged clause may need
  // to be set appropriately.
  // 2. "reduction": implies "privateness" of all objects (incompatible
  // with "shared"); there are rules for merging modifiers
  void mergeIf();
  void mergeReduction();
  void mergeDSA();

  uint32_t version;
  llvm::ArrayRef<DirectiveWithClauses<ClauseTy>> leafs;

  // clause id -> set of leaf constructs that contain it
  std::unordered_map<llvm::omp::Clause, llvm::BitVector> clausePresence;
  // clause id -> set of instances of that clause
  std::unordered_map<llvm::omp::Clause, ClauseSet> clauseSets;
};

template <typename C>
ConstructCompositionT<C>::ConstructCompositionT(
    uint32_t version, llvm::ArrayRef<DirectiveWithClauses<C>> leafs)
    : version(version), leafs(leafs) {
  // Merge the list of constructs with clauses into a compound construct
  // with a single list of clauses.
  // The intended use of this function is in splitting compound constructs,
  // while preserving composite constituent constructs:
  // Step 1: split compound construct into leaf constructs.
  // Step 2: identify composite sub-construct, and merge the constituent leafs.
  //
  // *** At the moment it assumes that the individual constructs and their
  // *** clauses are a subset of those created by splitting a valid compound
  // *** construct.
  //
  // 1. Deduplicate clauses
  //    - exact duplicates: e.g. shared(x) shared(x) -> shared(x)
  //    - special cases of clauses differing in modifier:
  //      (a) reduction: inscan + (none|default) = inscan
  //      (b) reduction: task + (none|default) = task
  //      (c) combine repeated "if" clauses if possible
  // 2. Merge DSA clauses: e.g. private(x) private(y) -> private(x, y).
  // 3. Resolve potential DSA conflicts (typically due to implied clauses).

  if (leafs.empty())
    return;

  merged.id = makeCompound(leafs);

  // Populate the two maps:
  for (const auto &[index, leaf] : llvm::enumerate(leafs)) {
    for (const auto &clause : leaf.clauses) {
      // Update clausePresence.
      auto &pset = clausePresence[clause.id];
      if (pset.size() < leafs.size())
        pset.resize(leafs.size());
      pset.set(index);
      // Update clauseSets.
      ClauseSet &cset = clauseSets[clause.id];
      if (!llvm::is_contained(cset, clause))
        cset.push_back(clause);
    }
  }

  mergeIf();
  mergeReduction();
  mergeDSA();

  // Fir the rest of the clauses, just copy them.
  for (auto &[id, clauses] : clauseSets) {
    // Skip clauses we've already dealt with.
    switch (id) {
    case llvm::omp::Clause::OMPC_if:
    case llvm::omp::Clause::OMPC_reduction:
    case llvm::omp::Clause::OMPC_shared:
    case llvm::omp::Clause::OMPC_private:
    case llvm::omp::Clause::OMPC_firstprivate:
    case llvm::omp::Clause::OMPC_lastprivate:
      continue;
    default:
      break;
    }
    llvm::append_range(merged.clauses, clauses);
  }
}

template <typename C>
llvm::omp::Directive ConstructCompositionT<C>::makeCompound(
    llvm::ArrayRef<DirectiveWithClauses<ClauseTy>> parts) {
  llvm::SmallVector<llvm::omp::Directive> dirIds;
  llvm::transform(parts, std::back_inserter(dirIds),
                  [](auto &&dwc) { return dwc.id; });

  return llvm::omp::getCompoundConstruct(dirIds);
}

template <typename C>
auto ConstructCompositionT<C>::checkPresence(llvm::omp::Clause clauseId)
    -> Presence {
  auto found = clausePresence.find(clauseId);
  if (found == clausePresence.end())
    return Presence::None;

  bool OnAll = true, OnNone = true;
  for (const auto &[index, leaf] : llvm::enumerate(leafs)) {
    if (!llvm::omp::isAllowedClauseForDirective(leaf.id, clauseId, version))
      continue;

    if (found->second.test(index))
      OnNone = false;
    else
      OnAll = false;
  }

  if (OnNone)
    return Presence::None;
  if (OnAll)
    return Presence::All;
  return Presence::Some;
}

template <typename C> void ConstructCompositionT<C>::mergeIf() {
  using IfTy = tomp::clause::IfT<TypeTy, IdTy, ExprTy>;
  // Deal with the "if" clauses. If it's on all leafs that allow it, then it
  // will apply to the compound construct. Otherwise it will apply to the
  // single (assumed) leaf construct.
  // This assumes that the "if" clauses have the same expression.
  Presence presence = checkPresence(llvm::omp::Clause::OMPC_if);
  if (presence == Presence::None)
    return;

  const ClauseTy &some = *clauseSets[llvm::omp::Clause::OMPC_if].begin();
  const auto &someIf = std::get<IfTy>(some.u);

  if (presence == Presence::All) {
    // Create "if" without "directive-name-modifier".
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_if,
                   IfTy{{/*DirectiveNameModifier=*/std::nullopt,
                         /*IfExpression=*/std::get<typename IfTy::IfExpression>(
                             someIf.t)}}));
  } else {
    // Find out where it's present and create "if" with the corresponding
    // "directive-name-modifier".
    int Idx = clausePresence[llvm::omp::Clause::OMPC_if].find_first();
    assert(Idx >= 0);
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_if,
                   IfTy{{/*DirectiveNameModifier=*/leafs[Idx].id,
                         /*IfExpression=*/std::get<typename IfTy::IfExpression>(
                             someIf.t)}}));
  }
}

template <typename C> void ConstructCompositionT<C>::mergeReduction() {
  Presence presence = checkPresence(llvm::omp::Clause::OMPC_reduction);
  if (presence == Presence::None)
    return;

  using ReductionTy = tomp::clause::ReductionT<TypeTy, IdTy, ExprTy>;
  using ModifierTy = typename ReductionTy::ReductionModifier;
  using IdentifiersTy = typename ReductionTy::ReductionIdentifiers;
  using ListTy = typename ReductionTy::List;
  // There are exceptions on which constructs "reduction" may appear
  // (specifically "parallel", and "teams"). Assume that if "reduction"
  // is present, it can be applied to the compound construct.

  // What's left is to see if there are any modifiers present. Again,
  // assume that there are no conflicting modifiers.
  // There can be, however, multiple reductions on different objects.
  auto equal = [](const ClauseTy &red1, const ClauseTy &red2) {
    // Extract actual reductions.
    const auto r1 = std::get<ReductionTy>(red1.u);
    const auto r2 = std::get<ReductionTy>(red2.u);
    // Compare everything except modifiers.
    if (std::get<IdentifiersTy>(r1.t) != std::get<IdentifiersTy>(r2.t))
      return false;
    if (std::get<ListTy>(r1.t) != std::get<ListTy>(r2.t))
      return false;
    return true;
  };

  auto getModifier = [](const ClauseTy &clause) {
    const ReductionTy &red = std::get<ReductionTy>(clause.u);
    return std::get<std::optional<ModifierTy>>(red.t);
  };

  const ClauseSet &reductions = clauseSets[llvm::omp::Clause::OMPC_reduction];
  std::unordered_set<const ClauseTy *> visited;
  while (reductions.size() != visited.size()) {
    typename ClauseSet::const_iterator first;

    // Find first non-visited reduction.
    for (first = reductions.begin(); first != reductions.end(); ++first) {
      if (visited.count(&*first))
        continue;
      visited.insert(&*first);
      break;
    }

    std::optional<ModifierTy> modifier = getModifier(*first);

    // Visit all other reductions that are "equal" (with respect to the
    // definition above) to "first". Collect modifiers.
    for (auto iter = std::next(first); iter != reductions.end(); ++iter) {
      if (!equal(*first, *iter))
        continue;
      visited.insert(&*iter);
      if (!modifier || *modifier == ModifierTy::Default)
        modifier = getModifier(*iter);
    }

    const auto &firstRed = std::get<ReductionTy>(first->u);
    merged.clauses.emplace_back(makeClause(
        llvm::omp::Clause::OMPC_reduction,
        ReductionTy{
            {/*ReductionModifier=*/modifier,
             /*ReductionIdentifiers=*/std::get<IdentifiersTy>(firstRed.t),
             /*List=*/std::get<ListTy>(firstRed.t)}}));
  }
}

template <typename C> void ConstructCompositionT<C>::mergeDSA() {
  using ObjectTy = tomp::type::ObjectT<IdTy, ExprTy>;

  // Resolve data-sharing attributes.
  enum DSA : int {
    None = 0,
    Shared = 1 << 0,
    Private = 1 << 1,
    FirstPrivate = 1 << 2,
    LastPrivate = 1 << 3,
    LastPrivateConditional = 1 << 4,
  };

  // Use ordered containers to avoid non-deterministic output.
  llvm::SmallVector<std::pair<ObjectTy, int>, 8> objectDsa;

  auto getDsa = [&](const ObjectTy &object) -> std::pair<ObjectTy, int> & {
    auto found = llvm::find_if(objectDsa, [&](std::pair<ObjectTy, int> &p) {
      return p.first.id() == object.id();
    });
    if (found != objectDsa.end())
      return *found;
    return objectDsa.emplace_back(object, DSA::None);
  };

  using SharedTy = tomp::clause::SharedT<TypeTy, IdTy, ExprTy>;
  using PrivateTy = tomp::clause::PrivateT<TypeTy, IdTy, ExprTy>;
  using FirstprivateTy = tomp::clause::FirstprivateT<TypeTy, IdTy, ExprTy>;
  using LastprivateTy = tomp::clause::LastprivateT<TypeTy, IdTy, ExprTy>;

  // Visit clauses that affect DSA.
  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_shared]) {
    for (auto &object : std::get<SharedTy>(clause.u).v)
      getDsa(object).second |= DSA::Shared;
  }

  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_private]) {
    for (auto &object : std::get<PrivateTy>(clause.u).v)
      getDsa(object).second |= DSA::Private;
  }

  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_firstprivate]) {
    for (auto &object : std::get<FirstprivateTy>(clause.u).v)
      getDsa(object).second |= DSA::FirstPrivate;
  }

  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_lastprivate]) {
    using ModifierTy = typename LastprivateTy::LastprivateModifier;
    using ListTy = typename LastprivateTy::List;
    const auto &lastp = std::get<LastprivateTy>(clause.u);
    for (auto &object : std::get<ListTy>(lastp.t)) {
      auto &mod = std::get<std::optional<ModifierTy>>(lastp.t);
      if (mod && *mod == ModifierTy::Conditional) {
        getDsa(object).second |= DSA::LastPrivateConditional;
      } else {
        getDsa(object).second |= DSA::LastPrivate;
      }
    }
  }

  // Check other privatizing clauses as well, clear "shared" if set.
  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_in_reduction]) {
    using InReductionTy = tomp::clause::InReductionT<TypeTy, IdTy, ExprTy>;
    using ListTy = typename InReductionTy::List;
    for (auto &object : std::get<ListTy>(std::get<InReductionTy>(clause.u).t))
      getDsa(object).second &= ~DSA::Shared;
  }
  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_linear]) {
    using LinearTy = tomp::clause::LinearT<TypeTy, IdTy, ExprTy>;
    using ListTy = typename LinearTy::List;
    for (auto &object : std::get<ListTy>(std::get<LinearTy>(clause.u).t))
      getDsa(object).second &= ~DSA::Shared;
  }
  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_reduction]) {
    using ReductionTy = tomp::clause::ReductionT<TypeTy, IdTy, ExprTy>;
    using ListTy = typename ReductionTy::List;
    for (auto &object : std::get<ListTy>(std::get<ReductionTy>(clause.u).t))
      getDsa(object).second &= ~DSA::Shared;
  }
  for (auto &clause : clauseSets[llvm::omp::Clause::OMPC_task_reduction]) {
    using TaskReductionTy = tomp::clause::TaskReductionT<TypeTy, IdTy, ExprTy>;
    using ListTy = typename TaskReductionTy::List;
    for (auto &object : std::get<ListTy>(std::get<TaskReductionTy>(clause.u).t))
      getDsa(object).second &= ~DSA::Shared;
  }

  tomp::ListT<ObjectTy> privateObj, sharedObj, firstpObj, lastpObj, lastpcObj;
  for (auto &[object, dsa] : objectDsa) {
    if (dsa &
        (DSA::FirstPrivate | DSA::LastPrivate | DSA::LastPrivateConditional)) {
      if (dsa & DSA::FirstPrivate)
        firstpObj.push_back(object); // no else
      if (dsa & DSA::LastPrivateConditional)
        lastpcObj.push_back(object);
      else if (dsa & DSA::LastPrivate)
        lastpObj.push_back(object);
    } else if (dsa & DSA::Private) {
      privateObj.push_back(object);
    } else if (dsa & DSA::Shared) {
      sharedObj.push_back(object);
    }
  }

  // Materialize each clause.
  if (!privateObj.empty()) {
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_private,
                   PrivateTy{/*List=*/std::move(privateObj)}));
  }
  if (!sharedObj.empty()) {
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_shared,
                   SharedTy{/*List=*/std::move(sharedObj)}));
  }
  if (!firstpObj.empty()) {
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_firstprivate,
                   FirstprivateTy{/*List=*/std::move(firstpObj)}));
  }
  if (!lastpObj.empty()) {
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_lastprivate,
                   LastprivateTy{{/*LastprivateModifier=*/std::nullopt,
                                  /*List=*/std::move(lastpObj)}}));
  }
  if (!lastpcObj.empty()) {
    auto conditional = LastprivateTy::LastprivateModifier::Conditional;
    merged.clauses.emplace_back(
        makeClause(llvm::omp::Clause::OMPC_lastprivate,
                   LastprivateTy{{/*LastprivateModifier=*/conditional,
                                  /*List=*/std::move(lastpcObj)}}));
  }
}
} // namespace tomp

#endif // LLVM_FRONTEND_OPENMP_CONSTRUCTCOMPOSITIONT_H
