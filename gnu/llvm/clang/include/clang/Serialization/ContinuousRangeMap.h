//===- ContinuousRangeMap.h - Map with int range as key ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ContinuousRangeMap class, which is a highly
//  specialized container used by serialization.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_CONTINUOUSRANGEMAP_H
#define LLVM_CLANG_SERIALIZATION_CONTINUOUSRANGEMAP_H

#include "clang/Basic/LLVM.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <algorithm>
#include <cassert>
#include <utility>

namespace clang {

/// A map from continuous integer ranges to some value, with a very
/// specialized interface.
///
/// CRM maps from integer ranges to values. The ranges are continuous, i.e.
/// where one ends, the next one begins. So if the map contains the stops I0-3,
/// the first range is from I0 to I1, the second from I1 to I2, the third from
/// I2 to I3 and the last from I3 to infinity.
///
/// Ranges must be inserted in order. Inserting a new stop I4 into the map will
/// shrink the fourth range to I3 to I4 and add the new range I4 to inf.
template <typename Int, typename V, unsigned InitialCapacity>
class ContinuousRangeMap {
public:
  using value_type = std::pair<Int, V>;
  using reference = value_type &;
  using const_reference = const value_type &;
  using pointer = value_type *;
  using const_pointer = const value_type *;

private:
  using Representation = SmallVector<value_type, InitialCapacity>;

  Representation Rep;

  struct Compare {
    bool operator ()(const_reference L, Int R) const {
      return L.first < R;
    }
    bool operator ()(Int L, const_reference R) const {
      return L < R.first;
    }
    bool operator ()(Int L, Int R) const {
      return L < R;
    }
    bool operator ()(const_reference L, const_reference R) const {
      return L.first < R.first;
    }
  };

public:
  void insert(const value_type &Val) {
    if (!Rep.empty() && Rep.back() == Val)
      return;

    assert((Rep.empty() || Rep.back().first < Val.first) &&
           "Must insert keys in order.");
    Rep.push_back(Val);
  }

  void insertOrReplace(const value_type &Val) {
    iterator I = llvm::lower_bound(Rep, Val, Compare());
    if (I != Rep.end() && I->first == Val.first) {
      I->second = Val.second;
      return;
    }

    Rep.insert(I, Val);
  }

  using iterator = typename Representation::iterator;
  using const_iterator = typename Representation::const_iterator;

  iterator begin() { return Rep.begin(); }
  iterator end() { return Rep.end(); }
  const_iterator begin() const { return Rep.begin(); }
  const_iterator end() const { return Rep.end(); }

  iterator find(Int K) {
    iterator I = llvm::upper_bound(Rep, K, Compare());
    // I points to the first entry with a key > K, which is the range that
    // follows the one containing K.
    if (I == Rep.begin())
      return Rep.end();
    --I;
    return I;
  }
  const_iterator find(Int K) const {
    return const_cast<ContinuousRangeMap*>(this)->find(K);
  }

  reference back() { return Rep.back(); }
  const_reference back() const { return Rep.back(); }

  /// An object that helps properly build a continuous range map
  /// from a set of values.
  class Builder {
    ContinuousRangeMap &Self;

  public:
    explicit Builder(ContinuousRangeMap &Self) : Self(Self) {}
    Builder(const Builder&) = delete;
    Builder &operator=(const Builder&) = delete;

    ~Builder() {
      llvm::sort(Self.Rep, Compare());
      Self.Rep.erase(
          std::unique(
              Self.Rep.begin(), Self.Rep.end(),
              [](const_reference A, const_reference B) {
                // FIXME: we should not allow any duplicate keys, but there are
                // a lot of duplicate 0 -> 0 mappings to remove first.
                assert((A == B || A.first != B.first) &&
                       "ContinuousRangeMap::Builder given non-unique keys");
                return A == B;
              }),
          Self.Rep.end());
    }

    void insert(const value_type &Val) {
      Self.Rep.push_back(Val);
    }
  };

  friend class Builder;
};

} // namespace clang

#endif // LLVM_CLANG_SERIALIZATION_CONTINUOUSRANGEMAP_H
