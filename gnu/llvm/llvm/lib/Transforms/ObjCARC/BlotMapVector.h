//===- BlotMapVector.h - A MapVector with the blot operation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TRANSFORMS_OBJCARC_BLOTMAPVECTOR_H
#define LLVM_LIB_TRANSFORMS_OBJCARC_BLOTMAPVECTOR_H

#include "llvm/ADT/DenseMap.h"
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>

namespace llvm {

/// An associative container with fast insertion-order (deterministic)
/// iteration over its elements. Plus the special blot operation.
template <class KeyT, class ValueT> class BlotMapVector {
  /// Map keys to indices in Vector.
  using MapTy = DenseMap<KeyT, size_t>;
  MapTy Map;

  /// Keys and values.
  using VectorTy = std::vector<std::pair<KeyT, ValueT>>;
  VectorTy Vector;

public:
#ifdef EXPENSIVE_CHECKS
  ~BlotMapVector() {
    assert(Vector.size() >= Map.size()); // May differ due to blotting.
    for (typename MapTy::const_iterator I = Map.begin(), E = Map.end(); I != E;
         ++I) {
      assert(I->second < Vector.size());
      assert(Vector[I->second].first == I->first);
    }
    for (typename VectorTy::const_iterator I = Vector.begin(), E = Vector.end();
         I != E; ++I)
      assert(!I->first || (Map.count(I->first) &&
                           Map[I->first] == size_t(I - Vector.begin())));
  }
#endif

  using iterator = typename VectorTy::iterator;
  using const_iterator = typename VectorTy::const_iterator;

  iterator begin() { return Vector.begin(); }
  iterator end() { return Vector.end(); }
  const_iterator begin() const { return Vector.begin(); }
  const_iterator end() const { return Vector.end(); }

  ValueT &operator[](const KeyT &Arg) {
    std::pair<typename MapTy::iterator, bool> Pair =
        Map.insert(std::make_pair(Arg, size_t(0)));
    if (Pair.second) {
      size_t Num = Vector.size();
      Pair.first->second = Num;
      Vector.push_back(std::make_pair(Arg, ValueT()));
      return Vector[Num].second;
    }
    return Vector[Pair.first->second].second;
  }

  std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &InsertPair) {
    std::pair<typename MapTy::iterator, bool> Pair =
        Map.insert(std::make_pair(InsertPair.first, size_t(0)));
    if (Pair.second) {
      size_t Num = Vector.size();
      Pair.first->second = Num;
      Vector.push_back(InsertPair);
      return std::make_pair(Vector.begin() + Num, true);
    }
    return std::make_pair(Vector.begin() + Pair.first->second, false);
  }

  iterator find(const KeyT &Key) {
    typename MapTy::iterator It = Map.find(Key);
    if (It == Map.end())
      return Vector.end();
    return Vector.begin() + It->second;
  }

  const_iterator find(const KeyT &Key) const {
    typename MapTy::const_iterator It = Map.find(Key);
    if (It == Map.end())
      return Vector.end();
    return Vector.begin() + It->second;
  }

  /// This is similar to erase, but instead of removing the element from the
  /// vector, it just zeros out the key in the vector. This leaves iterators
  /// intact, but clients must be prepared for zeroed-out keys when iterating.
  void blot(const KeyT &Key) {
    typename MapTy::iterator It = Map.find(Key);
    if (It == Map.end())
      return;
    Vector[It->second].first = KeyT();
    Map.erase(It);
  }

  void clear() {
    Map.clear();
    Vector.clear();
  }

  bool empty() const {
    assert(Map.empty() == Vector.empty());
    return Map.empty();
  }
};

} // end namespace llvm

#endif // LLVM_LIB_TRANSFORMS_OBJCARC_BLOTMAPVECTOR_H
