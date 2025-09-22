//===--------- interval_map.h - A sorted interval map -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a coalescing interval map.
//
//===----------------------------------------------------------------------===//

#ifndef ORC_RT_INTERVAL_MAP_H
#define ORC_RT_INTERVAL_MAP_H

#include "adt.h"
#include <cassert>
#include <map>

namespace __orc_rt {

enum class IntervalCoalescing { Enabled, Disabled };

/// Maps intervals to keys with optional coalescing.
///
/// NOTE: The interface is kept mostly compatible with LLVM's IntervalMap
///       collection to make it easy to swap over in the future if we choose
///       to.
template <typename KeyT, typename ValT> class IntervalMapBase {
private:
  using KeyPairT = std::pair<KeyT, KeyT>;

  struct Compare {
    using is_transparent = std::true_type;
    bool operator()(const KeyPairT &LHS, const KeyPairT &RHS) const {
      return LHS < RHS;
    }
    bool operator()(const KeyPairT &LHS, const KeyT &RHS) const {
      return LHS.first < RHS;
    }
    bool operator()(const KeyT &LHS, const KeyPairT &RHS) const {
      return LHS < RHS.first;
    }
  };

  using ImplMap = std::map<KeyPairT, ValT, Compare>;

public:
  using iterator = typename ImplMap::iterator;
  using const_iterator = typename ImplMap::const_iterator;
  using size_type = typename ImplMap::size_type;

  bool empty() const { return Impl.empty(); }

  void clear() { Impl.clear(); }

  iterator begin() { return Impl.begin(); }
  iterator end() { return Impl.end(); }

  const_iterator begin() const { return Impl.begin(); }
  const_iterator end() const { return Impl.end(); }

  iterator find(KeyT K) {
    // Early out if the key is clearly outside the range.
    if (empty() || K < begin()->first.first ||
        K >= std::prev(end())->first.second)
      return end();

    auto I = Impl.upper_bound(K);
    assert(I != begin() && "Should have hit early out above");
    I = std::prev(I);
    if (K < I->first.second)
      return I;
    return end();
  }

  const_iterator find(KeyT K) const {
    return const_cast<IntervalMapBase<KeyT, ValT> *>(this)->find(K);
  }

  ValT lookup(KeyT K, ValT NotFound = ValT()) const {
    auto I = find(K);
    if (I == end())
      return NotFound;
    return I->second;
  }

  // Erase [KS, KE), which must be entirely containing within one existing
  // range in the map. Removal is allowed to split the range.
  void erase(KeyT KS, KeyT KE) {
    if (empty())
      return;

    auto J = Impl.upper_bound(KS);

    // Check previous range. Bail out if range to remove is entirely after
    // it.
    auto I = std::prev(J);
    if (KS >= I->first.second)
      return;

    // Assert that range is wholly contained.
    assert(KE <= I->first.second);

    auto Tmp = std::move(*I);
    Impl.erase(I);

    // Split-right -- introduce right-split range.
    if (KE < Tmp.first.second) {
      Impl.insert(
          J, std::make_pair(std::make_pair(KE, Tmp.first.second), Tmp.second));
      J = std::prev(J);
    }

    // Split-left -- introduce left-split range.
    if (KS > Tmp.first.first)
      Impl.insert(
          J, std::make_pair(std::make_pair(Tmp.first.first, KS), Tmp.second));
  }

protected:
  ImplMap Impl;
};

template <typename KeyT, typename ValT, IntervalCoalescing Coalescing>
class IntervalMap;

template <typename KeyT, typename ValT>
class IntervalMap<KeyT, ValT, IntervalCoalescing::Enabled>
    : public IntervalMapBase<KeyT, ValT> {
public:
  // Coalescing insert. Requires that ValTs be equality-comparable.
  void insert(KeyT KS, KeyT KE, ValT V) {
    auto J = this->Impl.upper_bound(KS);

    // Coalesce-right if possible. Either way, J points at our insertion
    // point.
    if (J != this->end() && KE == J->first.first && J->second == V) {
      KE = J->first.second;
      auto Tmp = J++;
      this->Impl.erase(Tmp);
    }

    // Coalesce-left if possible.
    if (J != this->begin()) {
      auto I = std::prev(J);
      if (I->first.second == KS && I->second == V) {
        KS = I->first.first;
        this->Impl.erase(I);
      }
    }
    this->Impl.insert(J, std::make_pair(std::make_pair(KS, KE), std::move(V)));
  }
};

template <typename KeyT, typename ValT>
class IntervalMap<KeyT, ValT, IntervalCoalescing::Disabled>
    : public IntervalMapBase<KeyT, ValT> {
public:
  // Non-coalescing insert. Does not require ValT to be equality-comparable.
  void insert(KeyT KS, KeyT KE, ValT V) {
    this->Impl.insert(std::make_pair(std::make_pair(KS, KE), std::move(V)));
  }
};

} // End namespace __orc_rt

#endif // ORC_RT_INTERVAL_MAP_H
