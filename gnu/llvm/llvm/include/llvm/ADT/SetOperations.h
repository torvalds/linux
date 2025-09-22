//===-- llvm/ADT/SetOperations.h - Generic Set Operations -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines generic set operations that may be used on set's of
/// different types, and different element types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SETOPERATIONS_H
#define LLVM_ADT_SETOPERATIONS_H

#include "llvm/ADT/STLExtras.h"

namespace llvm {

namespace detail {
template <typename Set, typename Fn>
using check_has_member_remove_if_t =
    decltype(std::declval<Set>().remove_if(std::declval<Fn>()));

template <typename Set, typename Fn>
static constexpr bool HasMemberRemoveIf =
    is_detected<check_has_member_remove_if_t, Set, Fn>::value;

template <typename Set>
using check_has_member_erase_iter_t =
    decltype(std::declval<Set>().erase(std::declval<Set>().begin()));

template <typename Set>
static constexpr bool HasMemberEraseIter =
    is_detected<check_has_member_erase_iter_t, Set>::value;

} // namespace detail

/// set_union(A, B) - Compute A := A u B, return whether A changed.
///
template <class S1Ty, class S2Ty> bool set_union(S1Ty &S1, const S2Ty &S2) {
  bool Changed = false;

  for (const auto &E : S2)
    if (S1.insert(E).second)
      Changed = true;

  return Changed;
}

/// set_intersect(A, B) - Compute A := A ^ B
/// Identical to set_intersection, except that it works on set<>'s and
/// is nicer to use.  Functionally, this iterates through S1, removing
/// elements that are not contained in S2.
///
template <class S1Ty, class S2Ty> void set_intersect(S1Ty &S1, const S2Ty &S2) {
  auto Pred = [&S2](const auto &E) { return !S2.count(E); };
  if constexpr (detail::HasMemberRemoveIf<S1Ty, decltype(Pred)>) {
    S1.remove_if(Pred);
  } else {
    typename S1Ty::iterator Next;
    for (typename S1Ty::iterator I = S1.begin(); I != S1.end(); I = Next) {
      Next = std::next(I);
      if (!S2.count(*I))
        S1.erase(I); // Erase element if not in S2
    }
  }
}

template <class S1Ty, class S2Ty>
S1Ty set_intersection_impl(const S1Ty &S1, const S2Ty &S2) {
  S1Ty Result;
  for (const auto &E : S1)
    if (S2.count(E))
      Result.insert(E);
  return Result;
}

/// set_intersection(A, B) - Return A ^ B
template <class S1Ty, class S2Ty>
S1Ty set_intersection(const S1Ty &S1, const S2Ty &S2) {
  if (S1.size() < S2.size())
    return set_intersection_impl(S1, S2);
  else
    return set_intersection_impl(S2, S1);
}

/// set_difference(A, B) - Return A - B
///
template <class S1Ty, class S2Ty>
S1Ty set_difference(const S1Ty &S1, const S2Ty &S2) {
  S1Ty Result;
  for (const auto &E : S1)
    if (!S2.count(E)) // if the element is not in set2
      Result.insert(E);
  return Result;
}

/// set_subtract(A, B) - Compute A := A - B
///
/// Selects the set to iterate based on the relative sizes of A and B for better
/// efficiency.
///
template <class S1Ty, class S2Ty> void set_subtract(S1Ty &S1, const S2Ty &S2) {
  // If S1 is smaller than S2, iterate on S1 provided that S2 supports efficient
  // lookups via contains().  Note that a couple callers pass a vector for S2,
  // which doesn't support contains(), and wouldn't be efficient if it did.
  using ElemTy = decltype(*S1.begin());
  if constexpr (detail::HasMemberContains<S2Ty, ElemTy>) {
    auto Pred = [&S2](const auto &E) { return S2.contains(E); };
    if constexpr (detail::HasMemberRemoveIf<S1Ty, decltype(Pred)>) {
      if (S1.size() < S2.size()) {
        S1.remove_if(Pred);
        return;
      }
    } else if constexpr (detail::HasMemberEraseIter<S1Ty>) {
      if (S1.size() < S2.size()) {
        typename S1Ty::iterator Next;
        for (typename S1Ty::iterator SI = S1.begin(), SE = S1.end(); SI != SE;
             SI = Next) {
          Next = std::next(SI);
          if (S2.contains(*SI))
            S1.erase(SI);
        }
        return;
      }
    }
  }

  for (const auto &E : S2)
    S1.erase(E);
}

/// set_subtract(A, B, C, D) - Compute A := A - B, set C to the elements of B
/// removed from A (A ^ B), and D to the elements of B not found in and removed
/// from A (B - A).
template <class S1Ty, class S2Ty>
void set_subtract(S1Ty &S1, const S2Ty &S2, S1Ty &Removed, S1Ty &Remaining) {
  for (const auto &E : S2)
    if (S1.erase(E))
      Removed.insert(E);
    else
      Remaining.insert(E);
}

/// set_is_subset(A, B) - Return true iff A in B
///
template <class S1Ty, class S2Ty>
bool set_is_subset(const S1Ty &S1, const S2Ty &S2) {
  if (S1.size() > S2.size())
    return false;
  for (const auto It : S1)
    if (!S2.count(It))
      return false;
  return true;
}

} // namespace llvm

#endif
