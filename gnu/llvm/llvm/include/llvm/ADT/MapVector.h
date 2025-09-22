//===- llvm/ADT/MapVector.h - Map w/ deterministic value order --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a map that provides insertion order iteration. The
/// interface is purposefully minimal. The key is assumed to be cheap to copy
/// and 2 copies are kept, one for indexing in a DenseMap, one for iteration in
/// a SmallVector.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_MAPVECTOR_H
#define LLVM_ADT_MAPVECTOR_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace llvm {

/// This class implements a map that also provides access to all stored values
/// in a deterministic order. The values are kept in a SmallVector<*, 0> and the
/// mapping is done with DenseMap from Keys to indexes in that vector.
template <typename KeyT, typename ValueT,
          typename MapType = DenseMap<KeyT, unsigned>,
          typename VectorType = SmallVector<std::pair<KeyT, ValueT>, 0>>
class MapVector {
  MapType Map;
  VectorType Vector;

  static_assert(
      std::is_integral_v<typename MapType::mapped_type>,
      "The mapped_type of the specified Map must be an integral type");

public:
  using key_type = KeyT;
  using value_type = typename VectorType::value_type;
  using size_type = typename VectorType::size_type;

  using iterator = typename VectorType::iterator;
  using const_iterator = typename VectorType::const_iterator;
  using reverse_iterator = typename VectorType::reverse_iterator;
  using const_reverse_iterator = typename VectorType::const_reverse_iterator;

  /// Clear the MapVector and return the underlying vector.
  VectorType takeVector() {
    Map.clear();
    return std::move(Vector);
  }

  size_type size() const { return Vector.size(); }

  /// Grow the MapVector so that it can contain at least \p NumEntries items
  /// before resizing again.
  void reserve(size_type NumEntries) {
    Map.reserve(NumEntries);
    Vector.reserve(NumEntries);
  }

  iterator begin() { return Vector.begin(); }
  const_iterator begin() const { return Vector.begin(); }
  iterator end() { return Vector.end(); }
  const_iterator end() const { return Vector.end(); }

  reverse_iterator rbegin() { return Vector.rbegin(); }
  const_reverse_iterator rbegin() const { return Vector.rbegin(); }
  reverse_iterator rend() { return Vector.rend(); }
  const_reverse_iterator rend() const { return Vector.rend(); }

  bool empty() const {
    return Vector.empty();
  }

  std::pair<KeyT, ValueT>       &front()       { return Vector.front(); }
  const std::pair<KeyT, ValueT> &front() const { return Vector.front(); }
  std::pair<KeyT, ValueT>       &back()        { return Vector.back(); }
  const std::pair<KeyT, ValueT> &back()  const { return Vector.back(); }

  void clear() {
    Map.clear();
    Vector.clear();
  }

  void swap(MapVector &RHS) {
    std::swap(Map, RHS.Map);
    std::swap(Vector, RHS.Vector);
  }

  ValueT &operator[](const KeyT &Key) {
    std::pair<KeyT, typename MapType::mapped_type> Pair = std::make_pair(Key, 0);
    std::pair<typename MapType::iterator, bool> Result = Map.insert(Pair);
    auto &I = Result.first->second;
    if (Result.second) {
      Vector.push_back(std::make_pair(Key, ValueT()));
      I = Vector.size() - 1;
    }
    return Vector[I].second;
  }

  // Returns a copy of the value.  Only allowed if ValueT is copyable.
  ValueT lookup(const KeyT &Key) const {
    static_assert(std::is_copy_constructible_v<ValueT>,
                  "Cannot call lookup() if ValueT is not copyable.");
    typename MapType::const_iterator Pos = Map.find(Key);
    return Pos == Map.end()? ValueT() : Vector[Pos->second].second;
  }

  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(const KeyT &Key, Ts &&...Args) {
    auto [It, Inserted] = Map.insert(std::make_pair(Key, 0));
    if (Inserted) {
      It->second = Vector.size();
      Vector.emplace_back(std::piecewise_construct, std::forward_as_tuple(Key),
                          std::forward_as_tuple(std::forward<Ts>(Args)...));
      return std::make_pair(std::prev(end()), true);
    }
    return std::make_pair(begin() + It->second, false);
  }
  template <typename... Ts>
  std::pair<iterator, bool> try_emplace(KeyT &&Key, Ts &&...Args) {
    auto [It, Inserted] = Map.insert(std::make_pair(Key, 0));
    if (Inserted) {
      It->second = Vector.size();
      Vector.emplace_back(std::piecewise_construct,
                          std::forward_as_tuple(std::move(Key)),
                          std::forward_as_tuple(std::forward<Ts>(Args)...));
      return std::make_pair(std::prev(end()), true);
    }
    return std::make_pair(begin() + It->second, false);
  }

  std::pair<iterator, bool> insert(const std::pair<KeyT, ValueT> &KV) {
    return try_emplace(KV.first, KV.second);
  }
  std::pair<iterator, bool> insert(std::pair<KeyT, ValueT> &&KV) {
    return try_emplace(std::move(KV.first), std::move(KV.second));
  }

  template <typename V>
  std::pair<iterator, bool> insert_or_assign(const KeyT &Key, V &&Val) {
    auto Ret = try_emplace(Key, std::forward<V>(Val));
    if (!Ret.second)
      Ret.first->second = std::forward<V>(Val);
    return Ret;
  }
  template <typename V>
  std::pair<iterator, bool> insert_or_assign(KeyT &&Key, V &&Val) {
    auto Ret = try_emplace(std::move(Key), std::forward<V>(Val));
    if (!Ret.second)
      Ret.first->second = std::forward<V>(Val);
    return Ret;
  }

  bool contains(const KeyT &Key) const { return Map.find(Key) != Map.end(); }

  size_type count(const KeyT &Key) const { return contains(Key) ? 1 : 0; }

  iterator find(const KeyT &Key) {
    typename MapType::const_iterator Pos = Map.find(Key);
    return Pos == Map.end()? Vector.end() :
                            (Vector.begin() + Pos->second);
  }

  const_iterator find(const KeyT &Key) const {
    typename MapType::const_iterator Pos = Map.find(Key);
    return Pos == Map.end()? Vector.end() :
                            (Vector.begin() + Pos->second);
  }

  /// Remove the last element from the vector.
  void pop_back() {
    typename MapType::iterator Pos = Map.find(Vector.back().first);
    Map.erase(Pos);
    Vector.pop_back();
  }

  /// Remove the element given by Iterator.
  ///
  /// Returns an iterator to the element following the one which was removed,
  /// which may be end().
  ///
  /// \note This is a deceivingly expensive operation (linear time).  It's
  /// usually better to use \a remove_if() if possible.
  typename VectorType::iterator erase(typename VectorType::iterator Iterator) {
    Map.erase(Iterator->first);
    auto Next = Vector.erase(Iterator);
    if (Next == Vector.end())
      return Next;

    // Update indices in the map.
    size_t Index = Next - Vector.begin();
    for (auto &I : Map) {
      assert(I.second != Index && "Index was already erased!");
      if (I.second > Index)
        --I.second;
    }
    return Next;
  }

  /// Remove all elements with the key value Key.
  ///
  /// Returns the number of elements removed.
  size_type erase(const KeyT &Key) {
    auto Iterator = find(Key);
    if (Iterator == end())
      return 0;
    erase(Iterator);
    return 1;
  }

  /// Remove the elements that match the predicate.
  ///
  /// Erase all elements that match \c Pred in a single pass.  Takes linear
  /// time.
  template <class Predicate> void remove_if(Predicate Pred);
};

template <typename KeyT, typename ValueT, typename MapType, typename VectorType>
template <class Function>
void MapVector<KeyT, ValueT, MapType, VectorType>::remove_if(Function Pred) {
  auto O = Vector.begin();
  for (auto I = O, E = Vector.end(); I != E; ++I) {
    if (Pred(*I)) {
      // Erase from the map.
      Map.erase(I->first);
      continue;
    }

    if (I != O) {
      // Move the value and update the index in the map.
      *O = std::move(*I);
      Map[O->first] = O - Vector.begin();
    }
    ++O;
  }
  // Erase trailing entries in the vector.
  Vector.erase(O, Vector.end());
}

/// A MapVector that performs no allocations if smaller than a certain
/// size.
template <typename KeyT, typename ValueT, unsigned N>
struct SmallMapVector
    : MapVector<KeyT, ValueT, SmallDenseMap<KeyT, unsigned, N>,
                SmallVector<std::pair<KeyT, ValueT>, N>> {
};

} // end namespace llvm

#endif // LLVM_ADT_MAPVECTOR_H
