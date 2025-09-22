//===--------- interval_set.h - A sorted interval set -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a coalescing interval set.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_INTERVAL_SET_H
#define ORC_RT_INTERVAL_SET_H

#include "interval_map.h"

namespace __orc_rt {

/// Implements a coalescing interval set.
///
/// Adjacent intervals are coalesced.
///
/// NOTE: The interface is kept mostly compatible with LLVM's IntervalMap
///       collection to make it easy to swap over in the future if we choose
///       to.
template <typename KeyT, IntervalCoalescing Coalescing>
class IntervalSet {
private:
  using ImplMap = IntervalMap<KeyT, std::monostate, Coalescing>;
public:

  using value_type = std::pair<KeyT, KeyT>;

  class const_iterator {
    friend class IntervalSet;
  public:
    using difference_type = typename ImplMap::iterator::difference_type;
    using value_type = IntervalSet::value_type;
    using pointer = const value_type *;
    using reference = const value_type &;
    using iterator_category = std::input_iterator_tag;

    const_iterator() = default;
    const value_type &operator*() const { return I->first; }
    const value_type *operator->() const { return &I->first; }
    const_iterator &operator++() { ++I; return *this; }
    const_iterator operator++(int) { auto Tmp = I; ++I; return Tmp; }
    friend bool operator==(const const_iterator &LHS,
                           const const_iterator &RHS) {
      return LHS.I == RHS.I;
    }
    friend bool operator!=(const const_iterator &LHS,
                           const const_iterator &RHS) {
      return LHS.I != RHS.I;
    }
  private:
    const_iterator(typename ImplMap::const_iterator I) : I(std::move(I)) {}
    typename ImplMap::const_iterator I;
  };

  bool empty() const { return Map.empty(); }

  void clear() { Map.clear(); }

  const_iterator begin() const { return const_iterator(Map.begin()); }
  const_iterator end() const { return const_iterator(Map.end()); }

  const_iterator find(KeyT K) const {
    return const_iterator(Map.find(K));
  }

  void insert(KeyT KS, KeyT KE) {
    Map.insert(std::move(KS), std::move(KE), std::monostate());
  }

  void erase(KeyT KS, KeyT KE) {
    Map.erase(KS, KE);
  }

private:
  ImplMap Map;
};

} // End namespace __orc_rt

#endif // ORC_RT_INTERVAL_SET_H
