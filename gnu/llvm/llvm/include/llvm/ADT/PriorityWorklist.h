//===- PriorityWorklist.h - Worklist with insertion priority ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// This file provides a priority worklist. See the class comments for details.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_PRIORITYWORKLIST_H
#define LLVM_ADT_PRIORITYWORKLIST_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

namespace llvm {

/// A FILO worklist that prioritizes on re-insertion without duplication.
///
/// This is very similar to a \c SetVector with the primary difference that
/// while re-insertion does not create a duplicate, it does adjust the
/// visitation order to respect the last insertion point. This can be useful
/// when the visit order needs to be prioritized based on insertion point
/// without actually having duplicate visits.
///
/// Note that this doesn't prevent re-insertion of elements which have been
/// visited -- if you need to break cycles, a set will still be necessary.
///
/// The type \c T must be default constructable to a null value that will be
/// ignored. It is an error to insert such a value, and popping elements will
/// never produce such a value. It is expected to be used with common nullable
/// types like pointers or optionals.
///
/// Internally this uses a vector to store the worklist and a map to identify
/// existing elements in the worklist. Both of these may be customized, but the
/// map must support the basic DenseMap API for mapping from a T to an integer
/// index into the vector.
///
/// A partial specialization is provided to automatically select a SmallVector
/// and a SmallDenseMap if custom data structures are not provided.
template <typename T, typename VectorT = std::vector<T>,
          typename MapT = DenseMap<T, ptrdiff_t>>
class PriorityWorklist {
public:
  using value_type = T;
  using key_type = T;
  using reference = T&;
  using const_reference = const T&;
  using size_type = typename MapT::size_type;

  /// Construct an empty PriorityWorklist
  PriorityWorklist() = default;

  /// Determine if the PriorityWorklist is empty or not.
  bool empty() const {
    return V.empty();
  }

  /// Returns the number of elements in the worklist.
  size_type size() const {
    return M.size();
  }

  /// Count the number of elements of a given key in the PriorityWorklist.
  /// \returns 0 if the element is not in the PriorityWorklist, 1 if it is.
  size_type count(const key_type &key) const {
    return M.count(key);
  }

  /// Return the last element of the PriorityWorklist.
  const T &back() const {
    assert(!empty() && "Cannot call back() on empty PriorityWorklist!");
    return V.back();
  }

  /// Insert a new element into the PriorityWorklist.
  /// \returns true if the element was inserted into the PriorityWorklist.
  bool insert(const T &X) {
    assert(X != T() && "Cannot insert a null (default constructed) value!");
    auto InsertResult = M.insert({X, V.size()});
    if (InsertResult.second) {
      // Fresh value, just append it to the vector.
      V.push_back(X);
      return true;
    }

    auto &Index = InsertResult.first->second;
    assert(V[Index] == X && "Value not actually at index in map!");
    if (Index != (ptrdiff_t)(V.size() - 1)) {
      // If the element isn't at the back, null it out and append a fresh one.
      V[Index] = T();
      Index = (ptrdiff_t)V.size();
      V.push_back(X);
    }
    return false;
  }

  /// Insert a sequence of new elements into the PriorityWorklist.
  template <typename SequenceT>
  std::enable_if_t<!std::is_convertible<SequenceT, T>::value>
  insert(SequenceT &&Input) {
    if (std::begin(Input) == std::end(Input))
      // Nothing to do for an empty input sequence.
      return;

    // First pull the input sequence into the vector as a bulk append
    // operation.
    ptrdiff_t StartIndex = V.size();
    V.insert(V.end(), std::begin(Input), std::end(Input));
    // Now walk backwards fixing up the index map and deleting any duplicates.
    for (ptrdiff_t i = V.size() - 1; i >= StartIndex; --i) {
      auto InsertResult = M.insert({V[i], i});
      if (InsertResult.second)
        continue;

      // If the existing index is before this insert's start, nuke that one and
      // move it up.
      ptrdiff_t &Index = InsertResult.first->second;
      if (Index < StartIndex) {
        V[Index] = T();
        Index = i;
        continue;
      }

      // Otherwise the existing one comes first so just clear out the value in
      // this slot.
      V[i] = T();
    }
  }

  /// Remove the last element of the PriorityWorklist.
  void pop_back() {
    assert(!empty() && "Cannot remove an element when empty!");
    assert(back() != T() && "Cannot have a null element at the back!");
    M.erase(back());
    do {
      V.pop_back();
    } while (!V.empty() && V.back() == T());
  }

  [[nodiscard]] T pop_back_val() {
    T Ret = back();
    pop_back();
    return Ret;
  }

  /// Erase an item from the worklist.
  ///
  /// Note that this is constant time due to the nature of the worklist implementation.
  bool erase(const T& X) {
    auto I = M.find(X);
    if (I == M.end())
      return false;

    assert(V[I->second] == X && "Value not actually at index in map!");
    if (I->second == (ptrdiff_t)(V.size() - 1)) {
      do {
        V.pop_back();
      } while (!V.empty() && V.back() == T());
    } else {
      V[I->second] = T();
    }
    M.erase(I);
    return true;
  }

  /// Erase items from the set vector based on a predicate function.
  ///
  /// This is intended to be equivalent to the following code, if we could
  /// write it:
  ///
  /// \code
  ///   V.erase(remove_if(V, P), V.end());
  /// \endcode
  ///
  /// However, PriorityWorklist doesn't expose non-const iterators, making any
  /// algorithm like remove_if impossible to use.
  ///
  /// \returns true if any element is removed.
  template <typename UnaryPredicate>
  bool erase_if(UnaryPredicate P) {
    typename VectorT::iterator E =
        remove_if(V, TestAndEraseFromMap<UnaryPredicate>(P, M));
    if (E == V.end())
      return false;
    for (auto I = V.begin(); I != E; ++I)
      if (*I != T())
        M[*I] = I - V.begin();
    V.erase(E, V.end());
    return true;
  }

  /// Reverse the items in the PriorityWorklist.
  ///
  /// This does an in-place reversal. Other kinds of reverse aren't easy to
  /// support in the face of the worklist semantics.

  /// Completely clear the PriorityWorklist
  void clear() {
    M.clear();
    V.clear();
  }

private:
  /// A wrapper predicate designed for use with std::remove_if.
  ///
  /// This predicate wraps a predicate suitable for use with std::remove_if to
  /// call M.erase(x) on each element which is slated for removal. This just
  /// allows the predicate to be move only which we can't do with lambdas
  /// today.
  template <typename UnaryPredicateT>
  class TestAndEraseFromMap {
    UnaryPredicateT P;
    MapT &M;

  public:
    TestAndEraseFromMap(UnaryPredicateT P, MapT &M)
        : P(std::move(P)), M(M) {}

    bool operator()(const T &Arg) {
      if (Arg == T())
        // Skip null values in the PriorityWorklist.
        return false;

      if (P(Arg)) {
        M.erase(Arg);
        return true;
      }
      return false;
    }
  };

  /// The map from value to index in the vector.
  MapT M;

  /// The vector of elements in insertion order.
  VectorT V;
};

/// A version of \c PriorityWorklist that selects small size optimized data
/// structures for the vector and map.
template <typename T, unsigned N>
class SmallPriorityWorklist
    : public PriorityWorklist<T, SmallVector<T, N>,
                              SmallDenseMap<T, ptrdiff_t>> {
public:
  SmallPriorityWorklist() = default;
};

} // end namespace llvm

#endif // LLVM_ADT_PRIORITYWORKLIST_H
