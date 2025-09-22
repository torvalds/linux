//===- llvm/ADT/DenseSet.h - Dense probed hash table ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the DenseSet and SmallDenseSet classes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DENSESET_H
#define LLVM_ADT_DENSESET_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/type_traits.h"
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace llvm {

namespace detail {

struct DenseSetEmpty {};

// Use the empty base class trick so we can create a DenseMap where the buckets
// contain only a single item.
template <typename KeyT> class DenseSetPair : public DenseSetEmpty {
  KeyT key;

public:
  KeyT &getFirst() { return key; }
  const KeyT &getFirst() const { return key; }
  DenseSetEmpty &getSecond() { return *this; }
  const DenseSetEmpty &getSecond() const { return *this; }
};

/// Base class for DenseSet and DenseSmallSet.
///
/// MapTy should be either
///
///   DenseMap<ValueT, detail::DenseSetEmpty, ValueInfoT,
///            detail::DenseSetPair<ValueT>>
///
/// or the equivalent SmallDenseMap type.  ValueInfoT must implement the
/// DenseMapInfo "concept".
template <typename ValueT, typename MapTy, typename ValueInfoT>
class DenseSetImpl {
  static_assert(sizeof(typename MapTy::value_type) == sizeof(ValueT),
                "DenseMap buckets unexpectedly large!");
  MapTy TheMap;

  template <typename T>
  using const_arg_type_t = typename const_pointer_or_const_ref<T>::type;

public:
  using key_type = ValueT;
  using value_type = ValueT;
  using size_type = unsigned;

  explicit DenseSetImpl(unsigned InitialReserve = 0) : TheMap(InitialReserve) {}

  template <typename InputIt>
  DenseSetImpl(const InputIt &I, const InputIt &E)
      : DenseSetImpl(PowerOf2Ceil(std::distance(I, E))) {
    insert(I, E);
  }

  DenseSetImpl(std::initializer_list<ValueT> Elems)
      : DenseSetImpl(PowerOf2Ceil(Elems.size())) {
    insert(Elems.begin(), Elems.end());
  }

  bool empty() const { return TheMap.empty(); }
  size_type size() const { return TheMap.size(); }
  size_t getMemorySize() const { return TheMap.getMemorySize(); }

  /// Grow the DenseSet so that it has at least Size buckets. Will not shrink
  /// the Size of the set.
  void resize(size_t Size) { TheMap.resize(Size); }

  /// Grow the DenseSet so that it can contain at least \p NumEntries items
  /// before resizing again.
  void reserve(size_t Size) { TheMap.reserve(Size); }

  void clear() {
    TheMap.clear();
  }

  /// Return 1 if the specified key is in the set, 0 otherwise.
  size_type count(const_arg_type_t<ValueT> V) const {
    return TheMap.count(V);
  }

  bool erase(const ValueT &V) {
    return TheMap.erase(V);
  }

  void swap(DenseSetImpl &RHS) { TheMap.swap(RHS.TheMap); }

  // Iterators.

  class ConstIterator;

  class Iterator {
    typename MapTy::iterator I;
    friend class DenseSetImpl;
    friend class ConstIterator;

  public:
    using difference_type = typename MapTy::iterator::difference_type;
    using value_type = ValueT;
    using pointer = value_type *;
    using reference = value_type &;
    using iterator_category = std::forward_iterator_tag;

    Iterator() = default;
    Iterator(const typename MapTy::iterator &i) : I(i) {}

    ValueT &operator*() { return I->getFirst(); }
    const ValueT &operator*() const { return I->getFirst(); }
    ValueT *operator->() { return &I->getFirst(); }
    const ValueT *operator->() const { return &I->getFirst(); }

    Iterator& operator++() { ++I; return *this; }
    Iterator operator++(int) { auto T = *this; ++I; return T; }
    friend bool operator==(const Iterator &X, const Iterator &Y) {
      return X.I == Y.I;
    }
    friend bool operator!=(const Iterator &X, const Iterator &Y) {
      return X.I != Y.I;
    }
  };

  class ConstIterator {
    typename MapTy::const_iterator I;
    friend class DenseSetImpl;
    friend class Iterator;

  public:
    using difference_type = typename MapTy::const_iterator::difference_type;
    using value_type = ValueT;
    using pointer = const value_type *;
    using reference = const value_type &;
    using iterator_category = std::forward_iterator_tag;

    ConstIterator() = default;
    ConstIterator(const Iterator &B) : I(B.I) {}
    ConstIterator(const typename MapTy::const_iterator &i) : I(i) {}

    const ValueT &operator*() const { return I->getFirst(); }
    const ValueT *operator->() const { return &I->getFirst(); }

    ConstIterator& operator++() { ++I; return *this; }
    ConstIterator operator++(int) { auto T = *this; ++I; return T; }
    friend bool operator==(const ConstIterator &X, const ConstIterator &Y) {
      return X.I == Y.I;
    }
    friend bool operator!=(const ConstIterator &X, const ConstIterator &Y) {
      return X.I != Y.I;
    }
  };

  using iterator = Iterator;
  using const_iterator = ConstIterator;

  iterator begin() { return Iterator(TheMap.begin()); }
  iterator end() { return Iterator(TheMap.end()); }

  const_iterator begin() const { return ConstIterator(TheMap.begin()); }
  const_iterator end() const { return ConstIterator(TheMap.end()); }

  iterator find(const_arg_type_t<ValueT> V) { return Iterator(TheMap.find(V)); }
  const_iterator find(const_arg_type_t<ValueT> V) const {
    return ConstIterator(TheMap.find(V));
  }

  /// Check if the set contains the given element.
  bool contains(const_arg_type_t<ValueT> V) const {
    return TheMap.find(V) != TheMap.end();
  }

  /// Alternative version of find() which allows a different, and possibly less
  /// expensive, key type.
  /// The DenseMapInfo is responsible for supplying methods
  /// getHashValue(LookupKeyT) and isEqual(LookupKeyT, KeyT) for each key type
  /// used.
  template <class LookupKeyT>
  iterator find_as(const LookupKeyT &Val) {
    return Iterator(TheMap.find_as(Val));
  }
  template <class LookupKeyT>
  const_iterator find_as(const LookupKeyT &Val) const {
    return ConstIterator(TheMap.find_as(Val));
  }

  void erase(Iterator I) { return TheMap.erase(I.I); }
  void erase(ConstIterator CI) { return TheMap.erase(CI.I); }

  std::pair<iterator, bool> insert(const ValueT &V) {
    detail::DenseSetEmpty Empty;
    return TheMap.try_emplace(V, Empty);
  }

  std::pair<iterator, bool> insert(ValueT &&V) {
    detail::DenseSetEmpty Empty;
    return TheMap.try_emplace(std::move(V), Empty);
  }

  /// Alternative version of insert that uses a different (and possibly less
  /// expensive) key type.
  template <typename LookupKeyT>
  std::pair<iterator, bool> insert_as(const ValueT &V,
                                      const LookupKeyT &LookupKey) {
    return TheMap.insert_as({V, detail::DenseSetEmpty()}, LookupKey);
  }
  template <typename LookupKeyT>
  std::pair<iterator, bool> insert_as(ValueT &&V, const LookupKeyT &LookupKey) {
    return TheMap.insert_as({std::move(V), detail::DenseSetEmpty()}, LookupKey);
  }

  // Range insertion of values.
  template<typename InputIt>
  void insert(InputIt I, InputIt E) {
    for (; I != E; ++I)
      insert(*I);
  }
};

/// Equality comparison for DenseSet.
///
/// Iterates over elements of LHS confirming that each element is also a member
/// of RHS, and that RHS contains no additional values.
/// Equivalent to N calls to RHS.count. Amortized complexity is linear, worst
/// case is O(N^2) (if every hash collides).
template <typename ValueT, typename MapTy, typename ValueInfoT>
bool operator==(const DenseSetImpl<ValueT, MapTy, ValueInfoT> &LHS,
                const DenseSetImpl<ValueT, MapTy, ValueInfoT> &RHS) {
  if (LHS.size() != RHS.size())
    return false;

  for (auto &E : LHS)
    if (!RHS.count(E))
      return false;

  return true;
}

/// Inequality comparison for DenseSet.
///
/// Equivalent to !(LHS == RHS). See operator== for performance notes.
template <typename ValueT, typename MapTy, typename ValueInfoT>
bool operator!=(const DenseSetImpl<ValueT, MapTy, ValueInfoT> &LHS,
                const DenseSetImpl<ValueT, MapTy, ValueInfoT> &RHS) {
  return !(LHS == RHS);
}

} // end namespace detail

/// Implements a dense probed hash-table based set.
template <typename ValueT, typename ValueInfoT = DenseMapInfo<ValueT>>
class DenseSet : public detail::DenseSetImpl<
                     ValueT, DenseMap<ValueT, detail::DenseSetEmpty, ValueInfoT,
                                      detail::DenseSetPair<ValueT>>,
                     ValueInfoT> {
  using BaseT =
      detail::DenseSetImpl<ValueT,
                           DenseMap<ValueT, detail::DenseSetEmpty, ValueInfoT,
                                    detail::DenseSetPair<ValueT>>,
                           ValueInfoT>;

public:
  using BaseT::BaseT;
};

/// Implements a dense probed hash-table based set with some number of buckets
/// stored inline.
template <typename ValueT, unsigned InlineBuckets = 4,
          typename ValueInfoT = DenseMapInfo<ValueT>>
class SmallDenseSet
    : public detail::DenseSetImpl<
          ValueT, SmallDenseMap<ValueT, detail::DenseSetEmpty, InlineBuckets,
                                ValueInfoT, detail::DenseSetPair<ValueT>>,
          ValueInfoT> {
  using BaseT = detail::DenseSetImpl<
      ValueT, SmallDenseMap<ValueT, detail::DenseSetEmpty, InlineBuckets,
                            ValueInfoT, detail::DenseSetPair<ValueT>>,
      ValueInfoT>;

public:
  using BaseT::BaseT;
};

} // end namespace llvm

#endif // LLVM_ADT_DENSESET_H
