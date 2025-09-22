//===- ConstructDecompositionT.h -- Decomposing compound constructs -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Given a compound construct with a set of clauses, generate the list of
// constituent leaf constructs, each with a list of clauses that apply to it.
//
// Note: Clauses that are not originally present, but that are implied by the
// OpenMP spec are materialized, and are present in the output.
//
// Note: Composite constructs will also be broken up into leaf constructs.
// If composite constructs require processing as a whole, the lists of clauses
// for each leaf constituent should be merged.
//===----------------------------------------------------------------------===//
#ifndef LLVM_FRONTEND_OPENMP_CONSTRUCTDECOMPOSITIONT_H
#define LLVM_FRONTEND_OPENMP_CONSTRUCTDECOMPOSITIONT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Frontend/OpenMP/ClauseT.h"
#include "llvm/Frontend/OpenMP/OMP.h"

#include <iterator>
#include <list>
#include <optional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

static inline llvm::ArrayRef<llvm::omp::Directive> getWorksharing() {
  static llvm::omp::Directive worksharing[] = {
      llvm::omp::Directive::OMPD_do,     llvm::omp::Directive::OMPD_for,
      llvm::omp::Directive::OMPD_scope,  llvm::omp::Directive::OMPD_sections,
      llvm::omp::Directive::OMPD_single, llvm::omp::Directive::OMPD_workshare,
  };
  return worksharing;
}

static inline llvm::ArrayRef<llvm::omp::Directive> getWorksharingLoop() {
  static llvm::omp::Directive worksharingLoop[] = {
      llvm::omp::Directive::OMPD_do,
      llvm::omp::Directive::OMPD_for,
  };
  return worksharingLoop;
}

namespace detail {
template <typename Container, typename Predicate>
typename std::remove_reference_t<Container>::iterator
find_unique(Container &&container, Predicate &&pred) {
  auto first = std::find_if(container.begin(), container.end(), pred);
  if (first == container.end())
    return first;
  auto second = std::find_if(std::next(first), container.end(), pred);
  if (second == container.end())
    return first;
  return container.end();
}
} // namespace detail

namespace tomp {

// ClauseType - Either instance of ClauseT, or a type derived from ClauseT.
//
// This is the clause representation in the code using this infrastructure.
//
// HelperType - A class that implements two member functions:
//
//   // Return the base object of the given object, if any.
//   std::optional<Object> getBaseObject(const Object &object) const
//   // Return the iteration variable of the outermost loop associated
//   // with the construct being worked on, if any.
//   std::optional<Object> getLoopIterVar() const
template <typename ClauseType, typename HelperType>
struct ConstructDecompositionT {
  using ClauseTy = ClauseType;

  using TypeTy = typename ClauseTy::TypeTy;
  using IdTy = typename ClauseTy::IdTy;
  using ExprTy = typename ClauseTy::ExprTy;
  using HelperTy = HelperType;
  using ObjectTy = tomp::ObjectT<IdTy, ExprTy>;

  using ClauseSet = std::unordered_set<const ClauseTy *>;

  ConstructDecompositionT(uint32_t ver, HelperType &helper,
                          llvm::omp::Directive dir,
                          llvm::ArrayRef<ClauseTy> clauses)
      : version(ver), construct(dir), helper(helper) {
    for (const ClauseTy &clause : clauses)
      nodes.push_back(&clause);

    bool success = split();
    if (!success)
      return;

    // Copy the individual leaf directives with their clauses to the
    // output list. Copy by value, since we don't own the storage
    // with the input clauses, and the internal representation uses
    // clause addresses.
    for (auto &leaf : leafs) {
      output.push_back({leaf.id, {}});
      auto &out = output.back();
      for (const ClauseTy *c : leaf.clauses)
        out.clauses.push_back(*c);
    }
  }

  tomp::ListT<DirectiveWithClauses<ClauseType>> output;

private:
  bool split();

  struct LeafReprInternal {
    llvm::omp::Directive id = llvm::omp::Directive::OMPD_unknown;
    tomp::type::ListT<const ClauseTy *> clauses;
  };

  LeafReprInternal *findDirective(llvm::omp::Directive dirId) {
    auto found = llvm::find_if(
        leafs, [&](const LeafReprInternal &leaf) { return leaf.id == dirId; });
    return found != leafs.end() ? &*found : nullptr;
  }

  ClauseSet *findClausesWith(const ObjectTy &object) {
    if (auto found = syms.find(object.id()); found != syms.end())
      return &found->second;
    return nullptr;
  }

  template <typename S>
  ClauseTy *makeClause(llvm::omp::Clause clauseId, S &&specific) {
    implicit.push_back(typename ClauseTy::BaseT{clauseId, std::move(specific)});
    return &implicit.back();
  }

  void addClauseSymsToMap(const ObjectTy &object, const ClauseTy *);
  void addClauseSymsToMap(const tomp::ObjectListT<IdTy, ExprTy> &objects,
                          const ClauseTy *);
  void addClauseSymsToMap(const TypeTy &item, const ClauseTy *);
  void addClauseSymsToMap(const ExprTy &item, const ClauseTy *);
  void addClauseSymsToMap(const tomp::clause::MapT<TypeTy, IdTy, ExprTy> &item,
                          const ClauseTy *);

  template <typename U>
  void addClauseSymsToMap(const std::optional<U> &item, const ClauseTy *);
  template <typename U>
  void addClauseSymsToMap(const tomp::ListT<U> &item, const ClauseTy *);
  template <typename... U, size_t... Is>
  void addClauseSymsToMap(const std::tuple<U...> &item, const ClauseTy *,
                          std::index_sequence<Is...> = {});
  template <typename U>
  std::enable_if_t<std::is_enum_v<llvm::remove_cvref_t<U>>, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  template <typename U>
  std::enable_if_t<llvm::remove_cvref_t<U>::EmptyTrait::value, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  template <typename U>
  std::enable_if_t<llvm::remove_cvref_t<U>::IncompleteTrait::value, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  template <typename U>
  std::enable_if_t<llvm::remove_cvref_t<U>::WrapperTrait::value, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  template <typename U>
  std::enable_if_t<llvm::remove_cvref_t<U>::TupleTrait::value, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  template <typename U>
  std::enable_if_t<llvm::remove_cvref_t<U>::UnionTrait::value, void>
  addClauseSymsToMap(U &&item, const ClauseTy *);

  // Apply a clause to the only directive that allows it. If there are no
  // directives that allow it, or if there is more that one, do not apply
  // anything and return false, otherwise return true.
  bool applyToUnique(const ClauseTy *node);

  // Apply a clause to the first directive in given range that allows it.
  // If such a directive does not exist, return false, otherwise return true.
  template <typename Iterator>
  bool applyToFirst(const ClauseTy *node, llvm::iterator_range<Iterator> range);

  // Apply a clause to the innermost directive that allows it. If such a
  // directive does not exist, return false, otherwise return true.
  bool applyToInnermost(const ClauseTy *node);

  // Apply a clause to the outermost directive that allows it. If such a
  // directive does not exist, return false, otherwise return true.
  bool applyToOutermost(const ClauseTy *node);

  template <typename Predicate>
  bool applyIf(const ClauseTy *node, Predicate shouldApply);

  bool applyToAll(const ClauseTy *node);

  template <typename Clause>
  bool applyClause(Clause &&clause, const ClauseTy *node);

  bool applyClause(const tomp::clause::CollapseT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::PrivateT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool
  applyClause(const tomp::clause::FirstprivateT<TypeTy, IdTy, ExprTy> &clause,
              const ClauseTy *);
  bool
  applyClause(const tomp::clause::LastprivateT<TypeTy, IdTy, ExprTy> &clause,
              const ClauseTy *);
  bool applyClause(const tomp::clause::SharedT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::DefaultT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool
  applyClause(const tomp::clause::ThreadLimitT<TypeTy, IdTy, ExprTy> &clause,
              const ClauseTy *);
  bool applyClause(const tomp::clause::OrderT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::AllocateT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::ReductionT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::IfT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::LinearT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);
  bool applyClause(const tomp::clause::NowaitT<TypeTy, IdTy, ExprTy> &clause,
                   const ClauseTy *);

  uint32_t version;
  llvm::omp::Directive construct;
  HelperType &helper;
  ListT<LeafReprInternal> leafs;
  tomp::ListT<const ClauseTy *> nodes;
  std::list<ClauseTy> implicit; // Container for materialized implicit clauses.
                                // Inserting must preserve element addresses.
  std::unordered_map<IdTy, ClauseSet> syms;
  std::unordered_set<IdTy> mapBases;
};

// Deduction guide
template <typename ClauseType, typename HelperType>
ConstructDecompositionT(uint32_t, HelperType &, llvm::omp::Directive,
                        llvm::ArrayRef<ClauseType>)
    -> ConstructDecompositionT<ClauseType, HelperType>;

template <typename C, typename H>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(const ObjectTy &object,
                                                       const ClauseTy *node) {
  syms[object.id()].insert(node);
}

template <typename C, typename H>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(
    const tomp::ObjectListT<IdTy, ExprTy> &objects, const ClauseTy *node) {
  for (auto &object : objects)
    syms[object.id()].insert(node);
}

template <typename C, typename H>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(const TypeTy &item,
                                                       const ClauseTy *node) {
  // Nothing to do for types.
}

template <typename C, typename H>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(const ExprTy &item,
                                                       const ClauseTy *node) {
  // Nothing to do for expressions.
}

template <typename C, typename H>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(
    const tomp::clause::MapT<TypeTy, IdTy, ExprTy> &item,
    const ClauseTy *node) {
  auto &objects = std::get<tomp::ObjectListT<IdTy, ExprTy>>(item.t);
  addClauseSymsToMap(objects, node);
  for (auto &object : objects) {
    if (auto base = helper.getBaseObject(object))
      mapBases.insert(base->id());
  }
}

template <typename C, typename H>
template <typename U>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(
    const std::optional<U> &item, const ClauseTy *node) {
  if (item)
    addClauseSymsToMap(*item, node);
}

template <typename C, typename H>
template <typename U>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(
    const tomp::ListT<U> &item, const ClauseTy *node) {
  for (auto &s : item)
    addClauseSymsToMap(s, node);
}

template <typename C, typename H>
template <typename... U, size_t... Is>
void ConstructDecompositionT<C, H>::addClauseSymsToMap(
    const std::tuple<U...> &item, const ClauseTy *node,
    std::index_sequence<Is...>) {
  (void)node; // Silence strange warning from GCC.
  (addClauseSymsToMap(std::get<Is>(item), node), ...);
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<std::is_enum_v<llvm::remove_cvref_t<U>>, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  // Nothing to do for enums.
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<llvm::remove_cvref_t<U>::EmptyTrait::value, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  // Nothing to do for an empty class.
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<llvm::remove_cvref_t<U>::IncompleteTrait::value, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  // Nothing to do for an incomplete class (they're empty).
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<llvm::remove_cvref_t<U>::WrapperTrait::value, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  addClauseSymsToMap(item.v, node);
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<llvm::remove_cvref_t<U>::TupleTrait::value, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  constexpr size_t tuple_size =
      std::tuple_size_v<llvm::remove_cvref_t<decltype(item.t)>>;
  addClauseSymsToMap(item.t, node, std::make_index_sequence<tuple_size>{});
}

template <typename C, typename H>
template <typename U>
std::enable_if_t<llvm::remove_cvref_t<U>::UnionTrait::value, void>
ConstructDecompositionT<C, H>::addClauseSymsToMap(U &&item,
                                                  const ClauseTy *node) {
  std::visit([&](auto &&s) { addClauseSymsToMap(s, node); }, item.u);
}

// Apply a clause to the only directive that allows it. If there are no
// directives that allow it, or if there is more that one, do not apply
// anything and return false, otherwise return true.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyToUnique(const ClauseTy *node) {
  auto unique = detail::find_unique(leafs, [=](const auto &leaf) {
    return llvm::omp::isAllowedClauseForDirective(leaf.id, node->id, version);
  });

  if (unique != leafs.end()) {
    unique->clauses.push_back(node);
    return true;
  }
  return false;
}

// Apply a clause to the first directive in given range that allows it.
// If such a directive does not exist, return false, otherwise return true.
template <typename C, typename H>
template <typename Iterator>
bool ConstructDecompositionT<C, H>::applyToFirst(
    const ClauseTy *node, llvm::iterator_range<Iterator> range) {
  if (range.empty())
    return false;

  for (auto &leaf : range) {
    if (!llvm::omp::isAllowedClauseForDirective(leaf.id, node->id, version))
      continue;
    leaf.clauses.push_back(node);
    return true;
  }
  return false;
}

// Apply a clause to the innermost directive that allows it. If such a
// directive does not exist, return false, otherwise return true.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyToInnermost(const ClauseTy *node) {
  return applyToFirst(node, llvm::reverse(leafs));
}

// Apply a clause to the outermost directive that allows it. If such a
// directive does not exist, return false, otherwise return true.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyToOutermost(const ClauseTy *node) {
  return applyToFirst(node, llvm::iterator_range(leafs));
}

template <typename C, typename H>
template <typename Predicate>
bool ConstructDecompositionT<C, H>::applyIf(const ClauseTy *node,
                                            Predicate shouldApply) {
  bool applied = false;
  for (auto &leaf : leafs) {
    if (!llvm::omp::isAllowedClauseForDirective(leaf.id, node->id, version))
      continue;
    if (!shouldApply(leaf))
      continue;
    leaf.clauses.push_back(node);
    applied = true;
  }

  return applied;
}

template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyToAll(const ClauseTy *node) {
  return applyIf(node, [](auto) { return true; });
}

template <typename C, typename H>
template <typename Specific>
bool ConstructDecompositionT<C, H>::applyClause(Specific &&specific,
                                                const ClauseTy *node) {
  // The default behavior is to find the unique directive to which the
  // given clause may be applied. If there are no such directives, or
  // if there are multiple ones, flag an error.
  // From "OpenMP Application Programming Interface", Version 5.2:
  // S Some clauses are permitted only on a single leaf construct of the
  // S combined or composite construct, in which case the effect is as if
  // S the clause is applied to that specific construct. (p339, 31-33)
  if (applyToUnique(node))
    return true;

  return false;
}

// COLLAPSE
// [5.2:93:20-21]
// Directives: distribute, do, for, loop, simd, taskloop
//
// [5.2:339:35]
// (35) The collapse clause is applied once to the combined or composite
// construct.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::CollapseT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // Apply "collapse" to the innermost directive. If it's not one that
  // allows it flag an error.
  if (!leafs.empty()) {
    auto &last = leafs.back();

    if (llvm::omp::isAllowedClauseForDirective(last.id, node->id, version)) {
      last.clauses.push_back(node);
      return true;
    }
  }

  return false;
}

// PRIVATE
// [5.2:111:5-7]
// Directives: distribute, do, for, loop, parallel, scope, sections, simd,
// single, target, task, taskloop, teams
//
// [5.2:340:1-2]
// (1) The effect of the 1 private clause is as if it is applied only to the
// innermost leaf construct that permits it.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::PrivateT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  return applyToInnermost(node);
}

// FIRSTPRIVATE
// [5.2:112:5-7]
// Directives: distribute, do, for, parallel, scope, sections, single, target,
// task, taskloop, teams
//
// [5.2:340:3-20]
// (3) The effect of the firstprivate clause is as if it is applied to one or
// more leaf constructs as follows:
//  (5) To the distribute construct if it is among the constituent constructs;
//  (6) To the teams construct if it is among the constituent constructs and the
//      distribute construct is not;
//  (8) To a worksharing construct that accepts the clause if one is among the
//      constituent constructs;
//  (9) To the taskloop construct if it is among the constituent constructs;
// (10) To the parallel construct if it is among the constituent constructs and
//      neither a taskloop construct nor a worksharing construct that accepts
//      the clause is among them;
// (12) To the target construct if it is among the constituent constructs and
//      the same list item neither appears in a lastprivate clause nor is the
//      base variable or base pointer of a list item that appears in a map
//      clause.
//
// (15) If the parallel construct is among the constituent constructs and the
// effect is not as if the firstprivate clause is applied to it by the above
// rules, then the effect is as if the shared clause with the same list item is
// applied to the parallel construct.
// (17) If the teams construct is among the constituent constructs and the
// effect is not as if the firstprivate clause is applied to it by the above
// rules, then the effect is as if the shared clause with the same list item is
// applied to the teams construct.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::FirstprivateT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  bool applied = false;

  // [5.2:340:3-6]
  auto dirDistribute = findDirective(llvm::omp::OMPD_distribute);
  auto dirTeams = findDirective(llvm::omp::OMPD_teams);
  if (dirDistribute != nullptr) {
    dirDistribute->clauses.push_back(node);
    applied = true;
    // [5.2:340:17]
    if (dirTeams != nullptr) {
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/clause.v});
      dirTeams->clauses.push_back(shared);
    }
  } else if (dirTeams != nullptr) {
    dirTeams->clauses.push_back(node);
    applied = true;
  }

  // [5.2:340:8]
  auto findWorksharing = [&]() {
    auto worksharing = getWorksharing();
    for (auto &leaf : leafs) {
      auto found = llvm::find(worksharing, leaf.id);
      if (found != std::end(worksharing))
        return &leaf;
    }
    return static_cast<typename decltype(leafs)::value_type *>(nullptr);
  };

  auto dirWorksharing = findWorksharing();
  if (dirWorksharing != nullptr) {
    dirWorksharing->clauses.push_back(node);
    applied = true;
  }

  // [5.2:340:9]
  auto dirTaskloop = findDirective(llvm::omp::OMPD_taskloop);
  if (dirTaskloop != nullptr) {
    dirTaskloop->clauses.push_back(node);
    applied = true;
  }

  // [5.2:340:10]
  auto dirParallel = findDirective(llvm::omp::OMPD_parallel);
  if (dirParallel != nullptr) {
    if (dirTaskloop == nullptr && dirWorksharing == nullptr) {
      dirParallel->clauses.push_back(node);
      applied = true;
    } else {
      // [5.2:340:15]
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/clause.v});
      dirParallel->clauses.push_back(shared);
    }
  }

  // [5.2:340:12]
  auto inLastprivate = [&](const ObjectTy &object) {
    if (ClauseSet *set = findClausesWith(object)) {
      return llvm::find_if(*set, [](const ClauseTy *c) {
               return c->id == llvm::omp::Clause::OMPC_lastprivate;
             }) != set->end();
    }
    return false;
  };

  auto dirTarget = findDirective(llvm::omp::OMPD_target);
  if (dirTarget != nullptr) {
    tomp::ObjectListT<IdTy, ExprTy> objects;
    llvm::copy_if(
        clause.v, std::back_inserter(objects), [&](const ObjectTy &object) {
          return !inLastprivate(object) && !mapBases.count(object.id());
        });
    if (!objects.empty()) {
      auto *firstp = makeClause(
          llvm::omp::Clause::OMPC_firstprivate,
          tomp::clause::FirstprivateT<TypeTy, IdTy, ExprTy>{/*List=*/objects});
      dirTarget->clauses.push_back(firstp);
      applied = true;
    }
  }

  // "task" is not handled by any of the cases above.
  if (auto dirTask = findDirective(llvm::omp::OMPD_task)) {
    dirTask->clauses.push_back(node);
    applied = true;
  }

  return applied;
}

// LASTPRIVATE
// [5.2:115:7-8]
// Directives: distribute, do, for, loop, sections, simd, taskloop
//
// [5.2:340:21-30]
// (21) The effect of the lastprivate clause is as if it is applied to all leaf
// constructs that permit the clause.
// (22) If the parallel construct is among the constituent constructs and the
// list item is not also specified in the firstprivate clause, then the effect
// of the lastprivate clause is as if the shared clause with the same list item
// is applied to the parallel construct.
// (24) If the teams construct is among the constituent constructs and the list
// item is not also specified in the firstprivate clause, then the effect of the
// lastprivate clause is as if the shared clause with the same list item is
// applied to the teams construct.
// (27) If the target construct is among the constituent constructs and the list
// item is not the base variable or base pointer of a list item that appears in
// a map clause, the effect of the lastprivate clause is as if the same list
// item appears in a map clause with a map-type of tofrom.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::LastprivateT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  bool applied = false;

  // [5.2:340:21]
  applied = applyToAll(node);
  if (!applied)
    return false;

  auto inFirstprivate = [&](const ObjectTy &object) {
    if (ClauseSet *set = findClausesWith(object)) {
      return llvm::find_if(*set, [](const ClauseTy *c) {
               return c->id == llvm::omp::Clause::OMPC_firstprivate;
             }) != set->end();
    }
    return false;
  };

  auto &objects = std::get<tomp::ObjectListT<IdTy, ExprTy>>(clause.t);

  // Prepare list of objects that could end up in a "shared" clause.
  tomp::ObjectListT<IdTy, ExprTy> sharedObjects;
  llvm::copy_if(
      objects, std::back_inserter(sharedObjects),
      [&](const ObjectTy &object) { return !inFirstprivate(object); });

  if (!sharedObjects.empty()) {
    // [5.2:340:22]
    if (auto dirParallel = findDirective(llvm::omp::OMPD_parallel)) {
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/sharedObjects});
      dirParallel->clauses.push_back(shared);
      applied = true;
    }

    // [5.2:340:24]
    if (auto dirTeams = findDirective(llvm::omp::OMPD_teams)) {
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/sharedObjects});
      dirTeams->clauses.push_back(shared);
      applied = true;
    }
  }

  // [5.2:340:27]
  if (auto dirTarget = findDirective(llvm::omp::OMPD_target)) {
    tomp::ObjectListT<IdTy, ExprTy> tofrom;
    llvm::copy_if(
        objects, std::back_inserter(tofrom),
        [&](const ObjectTy &object) { return !mapBases.count(object.id()); });

    if (!tofrom.empty()) {
      using MapType =
          typename tomp::clause::MapT<TypeTy, IdTy, ExprTy>::MapType;
      auto *map =
          makeClause(llvm::omp::Clause::OMPC_map,
                     tomp::clause::MapT<TypeTy, IdTy, ExprTy>{
                         {/*MapType=*/MapType::Tofrom,
                          /*MapTypeModifier=*/std::nullopt,
                          /*Mapper=*/std::nullopt, /*Iterator=*/std::nullopt,
                          /*LocatorList=*/std::move(tofrom)}});
      dirTarget->clauses.push_back(map);
      applied = true;
    }
  }

  return applied;
}

// SHARED
// [5.2:110:5-6]
// Directives: parallel, task, taskloop, teams
//
// [5.2:340:31-32]
// (31) The effect of the shared, default, thread_limit, or order clause is as
// if it is applied to all leaf constructs that permit the clause.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::SharedT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // [5.2:340:31]
  return applyToAll(node);
}

// DEFAULT
// [5.2:109:5-6]
// Directives: parallel, task, taskloop, teams
//
// [5.2:340:31-32]
// (31) The effect of the shared, default, thread_limit, or order clause is as
// if it is applied to all leaf constructs that permit the clause.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::DefaultT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // [5.2:340:31]
  return applyToAll(node);
}

// THREAD_LIMIT
// [5.2:277:14-15]
// Directives: target, teams
//
// [5.2:340:31-32]
// (31) The effect of the shared, default, thread_limit, or order clause is as
// if it is applied to all leaf constructs that permit the clause.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::ThreadLimitT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // [5.2:340:31]
  return applyToAll(node);
}

// ORDER
// [5.2:234:3-4]
// Directives: distribute, do, for, loop, simd
//
// [5.2:340:31-32]
// (31) The effect of the shared, default, thread_limit, or order clause is as
// if it is applied to all leaf constructs that permit the clause.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::OrderT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // [5.2:340:31]
  return applyToAll(node);
}

// ALLOCATE
// [5.2:178:7-9]
// Directives: allocators, distribute, do, for, parallel, scope, sections,
// single, target, task, taskgroup, taskloop, teams
//
// [5.2:340:33-35]
// (33) The effect of the allocate clause is as if it is applied to all leaf
// constructs that permit the clause and to which a data-sharing attribute
// clause that may create a private copy of the same list item is applied.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::AllocateT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // This one needs to be applied at the end, once we know which clauses are
  // assigned to which leaf constructs.

  // [5.2:340:33]
  auto canMakePrivateCopy = [](llvm::omp::Clause id) {
    switch (id) {
    // Clauses with "privatization" property:
    case llvm::omp::Clause::OMPC_firstprivate:
    case llvm::omp::Clause::OMPC_in_reduction:
    case llvm::omp::Clause::OMPC_lastprivate:
    case llvm::omp::Clause::OMPC_linear:
    case llvm::omp::Clause::OMPC_private:
    case llvm::omp::Clause::OMPC_reduction:
    case llvm::omp::Clause::OMPC_task_reduction:
      return true;
    default:
      return false;
    }
  };

  bool applied = applyIf(node, [&](const auto &leaf) {
    return llvm::any_of(leaf.clauses, [&](const ClauseTy *n) {
      return canMakePrivateCopy(n->id);
    });
  });

  return applied;
}

// REDUCTION
// [5.2:134:17-18]
// Directives: do, for, loop, parallel, scope, sections, simd, taskloop, teams
//
// [5.2:340:36-37], [5.2:341:1-13]
// (36) The effect of the reduction clause is as if it is applied to all leaf
// constructs that permit the clause, except for the following constructs:
//  (1) The parallel construct, when combined with the sections,
//      worksharing-loop, loop, or taskloop construct; and
//  (3) The teams construct, when combined with the loop construct.
// (4) For the parallel and teams constructs above, the effect of the reduction
// clause instead is as if each list item or, for any list item that is an array
// item, its corresponding base array or base pointer appears in a shared clause
// for the construct.
// (6) If the task reduction-modifier is specified, the effect is as if it only
// modifies the behavior of the reduction clause on the innermost leaf construct
// that accepts the modifier (see Section 5.5.8).
// (8) If the inscan reduction-modifier is specified, the effect is as if it
// modifies the behavior of the reduction clause on all constructs of the
// combined construct to which the clause is applied and that accept the
// modifier.
// (10) If a list item in a reduction clause on a combined target construct does
// not have the same base variable or base pointer as a list item in a map
// clause on the construct, then the effect is as if the list item in the
// reduction clause appears as a list item in a map clause with a map-type of
// tofrom.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::ReductionT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  using ReductionTy = tomp::clause::ReductionT<TypeTy, IdTy, ExprTy>;

  // [5.2:340:36], [5.2:341:1], [5.2:341:3]
  bool applyToParallel = true, applyToTeams = true;

  auto dirParallel = findDirective(llvm::omp::Directive::OMPD_parallel);
  if (dirParallel) {
    auto exclusions = llvm::concat<const llvm::omp::Directive>(
        getWorksharingLoop(), tomp::ListT<llvm::omp::Directive>{
                                  llvm::omp::Directive::OMPD_loop,
                                  llvm::omp::Directive::OMPD_sections,
                                  llvm::omp::Directive::OMPD_taskloop,
                              });
    auto present = [&](llvm::omp::Directive id) {
      return findDirective(id) != nullptr;
    };

    if (llvm::any_of(exclusions, present))
      applyToParallel = false;
  }

  auto dirTeams = findDirective(llvm::omp::Directive::OMPD_teams);
  if (dirTeams) {
    // The only exclusion is OMPD_loop.
    if (findDirective(llvm::omp::Directive::OMPD_loop))
      applyToTeams = false;
  }

  using ReductionModifier = typename ReductionTy::ReductionModifier;
  using ReductionIdentifiers = typename ReductionTy::ReductionIdentifiers;

  auto &objects = std::get<tomp::ObjectListT<IdTy, ExprTy>>(clause.t);
  auto &modifier = std::get<std::optional<ReductionModifier>>(clause.t);

  // Apply the reduction clause first to all directives according to the spec.
  // If the reduction was applied at least once, proceed with the data sharing
  // side-effects.
  bool applied = false;

  // [5.2:341:6], [5.2:341:8]
  auto isValidModifier = [](llvm::omp::Directive dir, ReductionModifier mod,
                            bool alreadyApplied) {
    switch (mod) {
    case ReductionModifier::Inscan:
      // According to [5.2:135:11-13], "inscan" only applies to
      // worksharing-loop, worksharing-loop-simd, or "simd" constructs.
      return dir == llvm::omp::Directive::OMPD_simd ||
             llvm::is_contained(getWorksharingLoop(), dir);
    case ReductionModifier::Task:
      if (alreadyApplied)
        return false;
      // According to [5.2:135:16-18], "task" only applies to "parallel" and
      // worksharing constructs.
      return dir == llvm::omp::Directive::OMPD_parallel ||
             llvm::is_contained(getWorksharing(), dir);
    case ReductionModifier::Default:
      return true;
    }
    llvm_unreachable("Unexpected modifier");
  };

  auto *unmodified = makeClause(
      llvm::omp::Clause::OMPC_reduction,
      ReductionTy{
          {/*ReductionModifier=*/std::nullopt,
           /*ReductionIdentifiers=*/std::get<ReductionIdentifiers>(clause.t),
           /*List=*/objects}});

  ReductionModifier effective =
      modifier.has_value() ? *modifier : ReductionModifier::Default;
  bool effectiveApplied = false;
  // Walk over the leaf constructs starting from the innermost, and apply
  // the clause as required by the spec.
  for (auto &leaf : llvm::reverse(leafs)) {
    if (!llvm::omp::isAllowedClauseForDirective(leaf.id, node->id, version))
      continue;
    if (!applyToParallel && &leaf == dirParallel)
      continue;
    if (!applyToTeams && &leaf == dirTeams)
      continue;
    // Some form of the clause will be applied past this point.
    if (isValidModifier(leaf.id, effective, effectiveApplied)) {
      // Apply clause with modifier.
      leaf.clauses.push_back(node);
      effectiveApplied = true;
    } else {
      // Apply clause without modifier.
      leaf.clauses.push_back(unmodified);
    }
    // The modifier must be applied to some construct.
    applied = effectiveApplied;
  }

  if (!applied)
    return false;

  tomp::ObjectListT<IdTy, ExprTy> sharedObjects;
  llvm::transform(objects, std::back_inserter(sharedObjects),
                  [&](const ObjectTy &object) {
                    auto maybeBase = helper.getBaseObject(object);
                    return maybeBase ? *maybeBase : object;
                  });

  // [5.2:341:4]
  if (!sharedObjects.empty()) {
    if (dirParallel && !applyToParallel) {
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/sharedObjects});
      dirParallel->clauses.push_back(shared);
    }
    if (dirTeams && !applyToTeams) {
      auto *shared = makeClause(
          llvm::omp::Clause::OMPC_shared,
          tomp::clause::SharedT<TypeTy, IdTy, ExprTy>{/*List=*/sharedObjects});
      dirTeams->clauses.push_back(shared);
    }
  }

  // [5.2:341:10]
  auto dirTarget = findDirective(llvm::omp::Directive::OMPD_target);
  if (dirTarget && leafs.size() > 1) {
    tomp::ObjectListT<IdTy, ExprTy> tofrom;
    llvm::copy_if(objects, std::back_inserter(tofrom),
                  [&](const ObjectTy &object) {
                    if (auto maybeBase = helper.getBaseObject(object))
                      return !mapBases.count(maybeBase->id());
                    return !mapBases.count(object.id()); // XXX is this ok?
                  });
    if (!tofrom.empty()) {
      using MapType =
          typename tomp::clause::MapT<TypeTy, IdTy, ExprTy>::MapType;
      auto *map = makeClause(
          llvm::omp::Clause::OMPC_map,
          tomp::clause::MapT<TypeTy, IdTy, ExprTy>{
              {/*MapType=*/MapType::Tofrom, /*MapTypeModifier=*/std::nullopt,
               /*Mapper=*/std::nullopt, /*Iterator=*/std::nullopt,
               /*LocatorList=*/std::move(tofrom)}});

      dirTarget->clauses.push_back(map);
      applied = true;
    }
  }

  return applied;
}

// IF
// [5.2:72:7-9]
// Directives: cancel, parallel, simd, target, target data, target enter data,
// target exit data, target update, task, taskloop
//
// [5.2:72:15-18]
// (15) For combined or composite constructs, the if clause only applies to the
// semantics of the construct named in the directive-name-modifier.
// (16) For a combined or composite construct, if no directive-name-modifier is
// specified then the if clause applies to all constituent constructs to which
// an if clause can apply.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::IfT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  using DirectiveNameModifier =
      typename clause::IfT<TypeTy, IdTy, ExprTy>::DirectiveNameModifier;
  using IfExpression = typename clause::IfT<TypeTy, IdTy, ExprTy>::IfExpression;
  auto &modifier = std::get<std::optional<DirectiveNameModifier>>(clause.t);

  if (modifier) {
    llvm::omp::Directive dirId = *modifier;
    auto *unmodified =
        makeClause(llvm::omp::Clause::OMPC_if,
                   tomp::clause::IfT<TypeTy, IdTy, ExprTy>{
                       {/*DirectiveNameModifier=*/std::nullopt,
                        /*IfExpression=*/std::get<IfExpression>(clause.t)}});

    if (auto *hasDir = findDirective(dirId)) {
      hasDir->clauses.push_back(unmodified);
      return true;
    }
    return false;
  }

  return applyToAll(node);
}

// LINEAR
// [5.2:118:1-2]
// Directives: declare simd, do, for, simd
//
// [5.2:341:15-22]
// (15.1) The effect of the linear clause is as if it is applied to the
// innermost leaf construct.
// (15.2) Additionally, if the list item is not the iteration variable of a simd
// or worksharing-loop SIMD construct, the effect on the outer leaf constructs
// is as if the list item was specified in firstprivate and lastprivate clauses
// on the combined or composite construct, with the rules specified above
// applied.
// (19) If a list item of the linear clause is the iteration variable of a simd
// or worksharing-loop SIMD construct and it is not declared in the construct,
// the effect on the outer leaf constructs is as if the list item was specified
// in a lastprivate clause on the combined or composite construct with the rules
// specified above applied.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::LinearT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  // [5.2:341:15.1]
  if (!applyToInnermost(node))
    return false;

  // [5.2:341:15.2], [5.2:341:19]
  auto dirSimd = findDirective(llvm::omp::Directive::OMPD_simd);
  std::optional<ObjectTy> iterVar = helper.getLoopIterVar();
  const auto &objects = std::get<tomp::ObjectListT<IdTy, ExprTy>>(clause.t);

  // Lists of objects that will be used to construct "firstprivate" and
  // "lastprivate" clauses.
  tomp::ObjectListT<IdTy, ExprTy> first, last;

  for (const ObjectTy &object : objects) {
    last.push_back(object);
    if (!dirSimd || !iterVar || object.id() != iterVar->id())
      first.push_back(object);
  }

  if (!first.empty()) {
    auto *firstp = makeClause(
        llvm::omp::Clause::OMPC_firstprivate,
        tomp::clause::FirstprivateT<TypeTy, IdTy, ExprTy>{/*List=*/first});
    nodes.push_back(firstp); // Appending to the main clause list.
  }
  if (!last.empty()) {
    auto *lastp =
        makeClause(llvm::omp::Clause::OMPC_lastprivate,
                   tomp::clause::LastprivateT<TypeTy, IdTy, ExprTy>{
                       {/*LastprivateModifier=*/std::nullopt, /*List=*/last}});
    nodes.push_back(lastp); // Appending to the main clause list.
  }
  return true;
}

// NOWAIT
// [5.2:308:11-13]
// Directives: dispatch, do, for, interop, scope, sections, single, target,
// target enter data, target exit data, target update, taskwait, workshare
//
// [5.2:341:23]
// (23) The effect of the nowait clause is as if it is applied to the outermost
// leaf construct that permits it.
template <typename C, typename H>
bool ConstructDecompositionT<C, H>::applyClause(
    const tomp::clause::NowaitT<TypeTy, IdTy, ExprTy> &clause,
    const ClauseTy *node) {
  return applyToOutermost(node);
}

template <typename C, typename H> bool ConstructDecompositionT<C, H>::split() {
  bool success = true;

  for (llvm::omp::Directive leaf :
       llvm::omp::getLeafConstructsOrSelf(construct))
    leafs.push_back(LeafReprInternal{leaf, /*clauses=*/{}});

  for (const ClauseTy *node : nodes)
    addClauseSymsToMap(*node, node);

  // First we need to apply LINEAR, because it can generate additional
  // "firstprivate" and "lastprivate" clauses that apply to the combined/
  // composite construct.
  // Collect them separately, because they may modify the clause list.
  llvm::SmallVector<const ClauseTy *> linears;
  for (const ClauseTy *node : nodes) {
    if (node->id == llvm::omp::Clause::OMPC_linear)
      linears.push_back(node);
  }
  for (const auto *node : linears) {
    success = success &&
              applyClause(std::get<tomp::clause::LinearT<TypeTy, IdTy, ExprTy>>(
                              node->u),
                          node);
  }

  // "allocate" clauses need to be applied last since they need to see
  // which directives have data-privatizing clauses.
  auto skip = [](const ClauseTy *node) {
    switch (node->id) {
    case llvm::omp::Clause::OMPC_allocate:
    case llvm::omp::Clause::OMPC_linear:
      return true;
    default:
      return false;
    }
  };

  // Apply (almost) all clauses.
  for (const ClauseTy *node : nodes) {
    if (skip(node))
      continue;
    success =
        success &&
        std::visit([&](auto &&s) { return applyClause(s, node); }, node->u);
  }

  // Apply "allocate".
  for (const ClauseTy *node : nodes) {
    if (node->id != llvm::omp::Clause::OMPC_allocate)
      continue;
    success =
        success &&
        std::visit([&](auto &&s) { return applyClause(s, node); }, node->u);
  }

  return success;
}

} // namespace tomp

#endif // LLVM_FRONTEND_OPENMP_CONSTRUCTDECOMPOSITIONT_H
