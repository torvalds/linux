//===- llvm/ADT/SetVector.h - Set with insert order iteration ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a set that has insertion order iteration
/// characteristics. This is useful for keeping a set of things that need to be
/// visited later but in a deterministic order (insertion order). The interface
/// is purposefully minimal.
///
/// This file defines SetVector and SmallSetVector, which performs no
/// allocations if the SetVector has less than a certain number of elements.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SETVECTOR_H
#define LLVM_ADT_SETVECTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <iterator>

namespace llvm {

/// A vector that has set insertion semantics.
///
/// This adapter class provides a way to keep a set of things that also has the
/// property of a deterministic iteration order. The order of iteration is the
/// order of insertion.
///
/// The key and value types are derived from the Set and Vector types
/// respectively. This allows the vector-type operations and set-type operations
/// to have different types. In particular, this is useful when storing pointers
/// as "Foo *" values but looking them up as "const Foo *" keys.
///
/// No constraint is placed on the key and value types, although it is assumed
/// that value_type can be converted into key_type for insertion. Users must be
/// aware of any loss of information in this conversion. For example, setting
/// value_type to float and key_type to int can produce very surprising results,
/// but it is not explicitly disallowed.
///
/// The parameter N specifies the "small" size of the container, which is the
/// number of elements upto which a linear scan over the Vector will be used
/// when searching for elements instead of checking Set, due to it being better
/// for performance. A value of 0 means that this mode of operation is not used,
/// and is the default value.
template <typename T, typename Vector = SmallVector<T, 0>,
          typename Set = DenseSet<T>, unsigned N = 0>
class SetVector {
  // Much like in SmallPtrSet, this value should not be too high to prevent
  // excessively long linear scans from occuring.
  static_assert(N <= 32, "Small size should be less than or equal to 32!");

public:
  using value_type = typename Vector::value_type;
  using key_type = typename Set::key_type;
  using reference = value_type &;
  using const_reference = const value_type &;
  using set_type = Set;
  using vector_type = Vector;
  using iterator = typename vector_type::const_iterator;
  using const_iterator = typename vector_type::const_iterator;
  using reverse_iterator = typename vector_type::const_reverse_iterator;
  using const_reverse_iterator = typename vector_type::const_reverse_iterator;
  using size_type = typename vector_type::size_type;

  /// Construct an empty SetVector
  SetVector() = default;

  /// Initialize a SetVector with a range of elements
  template<typename It>
  SetVector(It Start, It End) {
    insert(Start, End);
  }

  ArrayRef<value_type> getArrayRef() const { return vector_; }

  /// Clear the SetVector and return the underlying vector.
  Vector takeVector() {
    set_.clear();
    return std::move(vector_);
  }

  /// Determine if the SetVector is empty or not.
  bool empty() const {
    return vector_.empty();
  }

  /// Determine the number of elements in the SetVector.
  size_type size() const {
    return vector_.size();
  }

  /// Get an iterator to the beginning of the SetVector.
  iterator begin() {
    return vector_.begin();
  }

  /// Get a const_iterator to the beginning of the SetVector.
  const_iterator begin() const {
    return vector_.begin();
  }

  /// Get an iterator to the end of the SetVector.
  iterator end() {
    return vector_.end();
  }

  /// Get a const_iterator to the end of the SetVector.
  const_iterator end() const {
    return vector_.end();
  }

  /// Get an reverse_iterator to the end of the SetVector.
  reverse_iterator rbegin() {
    return vector_.rbegin();
  }

  /// Get a const_reverse_iterator to the end of the SetVector.
  const_reverse_iterator rbegin() const {
    return vector_.rbegin();
  }

  /// Get a reverse_iterator to the beginning of the SetVector.
  reverse_iterator rend() {
    return vector_.rend();
  }

  /// Get a const_reverse_iterator to the beginning of the SetVector.
  const_reverse_iterator rend() const {
    return vector_.rend();
  }

  /// Return the first element of the SetVector.
  const value_type &front() const {
    assert(!empty() && "Cannot call front() on empty SetVector!");
    return vector_.front();
  }

  /// Return the last element of the SetVector.
  const value_type &back() const {
    assert(!empty() && "Cannot call back() on empty SetVector!");
    return vector_.back();
  }

  /// Index into the SetVector.
  const_reference operator[](size_type n) const {
    assert(n < vector_.size() && "SetVector access out of range!");
    return vector_[n];
  }

  /// Insert a new element into the SetVector.
  /// \returns true if the element was inserted into the SetVector.
  bool insert(const value_type &X) {
    if constexpr (canBeSmall())
      if (isSmall()) {
        if (!llvm::is_contained(vector_, X)) {
          vector_.push_back(X);
          if (vector_.size() > N)
            makeBig();
          return true;
        }
        return false;
      }

    bool result = set_.insert(X).second;
    if (result)
      vector_.push_back(X);
    return result;
  }

  /// Insert a range of elements into the SetVector.
  template<typename It>
  void insert(It Start, It End) {
    for (; Start != End; ++Start)
      insert(*Start);
  }

  /// Remove an item from the set vector.
  bool remove(const value_type& X) {
    if constexpr (canBeSmall())
      if (isSmall()) {
        typename vector_type::iterator I = find(vector_, X);
        if (I != vector_.end()) {
          vector_.erase(I);
          return true;
        }
        return false;
      }

    if (set_.erase(X)) {
      typename vector_type::iterator I = find(vector_, X);
      assert(I != vector_.end() && "Corrupted SetVector instances!");
      vector_.erase(I);
      return true;
    }
    return false;
  }

  /// Erase a single element from the set vector.
  /// \returns an iterator pointing to the next element that followed the
  /// element erased. This is the end of the SetVector if the last element is
  /// erased.
  iterator erase(const_iterator I) {
    if constexpr (canBeSmall())
      if (isSmall())
        return vector_.erase(I);

    const key_type &V = *I;
    assert(set_.count(V) && "Corrupted SetVector instances!");
    set_.erase(V);
    return vector_.erase(I);
  }

  /// Remove items from the set vector based on a predicate function.
  ///
  /// This is intended to be equivalent to the following code, if we could
  /// write it:
  ///
  /// \code
  ///   V.erase(remove_if(V, P), V.end());
  /// \endcode
  ///
  /// However, SetVector doesn't expose non-const iterators, making any
  /// algorithm like remove_if impossible to use.
  ///
  /// \returns true if any element is removed.
  template <typename UnaryPredicate>
  bool remove_if(UnaryPredicate P) {
    typename vector_type::iterator I = [this, P] {
      if constexpr (canBeSmall())
        if (isSmall())
          return llvm::remove_if(vector_, P);

      return llvm::remove_if(vector_,
                             TestAndEraseFromSet<UnaryPredicate>(P, set_));
    }();

    if (I == vector_.end())
      return false;
    vector_.erase(I, vector_.end());
    return true;
  }

  /// Check if the SetVector contains the given key.
  bool contains(const key_type &key) const {
    if constexpr (canBeSmall())
      if (isSmall())
        return is_contained(vector_, key);

    return set_.find(key) != set_.end();
  }

  /// Count the number of elements of a given key in the SetVector.
  /// \returns 0 if the element is not in the SetVector, 1 if it is.
  size_type count(const key_type &key) const {
    if constexpr (canBeSmall())
      if (isSmall())
        return is_contained(vector_, key);

    return set_.count(key);
  }

  /// Completely clear the SetVector
  void clear() {
    set_.clear();
    vector_.clear();
  }

  /// Remove the last element of the SetVector.
  void pop_back() {
    assert(!empty() && "Cannot remove an element from an empty SetVector!");
    set_.erase(back());
    vector_.pop_back();
  }

  [[nodiscard]] value_type pop_back_val() {
    value_type Ret = back();
    pop_back();
    return Ret;
  }

  bool operator==(const SetVector &that) const {
    return vector_ == that.vector_;
  }

  bool operator!=(const SetVector &that) const {
    return vector_ != that.vector_;
  }

  /// Compute This := This u S, return whether 'This' changed.
  /// TODO: We should be able to use set_union from SetOperations.h, but
  ///       SetVector interface is inconsistent with DenseSet.
  template <class STy>
  bool set_union(const STy &S) {
    bool Changed = false;

    for (typename STy::const_iterator SI = S.begin(), SE = S.end(); SI != SE;
         ++SI)
      if (insert(*SI))
        Changed = true;

    return Changed;
  }

  /// Compute This := This - B
  /// TODO: We should be able to use set_subtract from SetOperations.h, but
  ///       SetVector interface is inconsistent with DenseSet.
  template <class STy>
  void set_subtract(const STy &S) {
    for (typename STy::const_iterator SI = S.begin(), SE = S.end(); SI != SE;
         ++SI)
      remove(*SI);
  }

  void swap(SetVector<T, Vector, Set, N> &RHS) {
    set_.swap(RHS.set_);
    vector_.swap(RHS.vector_);
  }

private:
  /// A wrapper predicate designed for use with std::remove_if.
  ///
  /// This predicate wraps a predicate suitable for use with std::remove_if to
  /// call set_.erase(x) on each element which is slated for removal.
  template <typename UnaryPredicate>
  class TestAndEraseFromSet {
    UnaryPredicate P;
    set_type &set_;

  public:
    TestAndEraseFromSet(UnaryPredicate P, set_type &set_)
        : P(std::move(P)), set_(set_) {}

    template <typename ArgumentT>
    bool operator()(const ArgumentT &Arg) {
      if (P(Arg)) {
        set_.erase(Arg);
        return true;
      }
      return false;
    }
  };

  [[nodiscard]] static constexpr bool canBeSmall() { return N != 0; }

  [[nodiscard]] bool isSmall() const { return set_.empty(); }

  void makeBig() {
    if constexpr (canBeSmall())
      for (const auto &entry : vector_)
        set_.insert(entry);
  }

  set_type set_;         ///< The set.
  vector_type vector_;   ///< The vector.
};

/// A SetVector that performs no allocations if smaller than
/// a certain size.
template <typename T, unsigned N>
class SmallSetVector : public SetVector<T, SmallVector<T, N>, DenseSet<T>, N> {
public:
  SmallSetVector() = default;

  /// Initialize a SmallSetVector with a range of elements
  template<typename It>
  SmallSetVector(It Start, It End) {
    this->insert(Start, End);
  }
};

} // end namespace llvm

namespace std {

/// Implement std::swap in terms of SetVector swap.
template <typename T, typename V, typename S, unsigned N>
inline void swap(llvm::SetVector<T, V, S, N> &LHS,
                 llvm::SetVector<T, V, S, N> &RHS) {
  LHS.swap(RHS);
}

/// Implement std::swap in terms of SmallSetVector swap.
template<typename T, unsigned N>
inline void
swap(llvm::SmallSetVector<T, N> &LHS, llvm::SmallSetVector<T, N> &RHS) {
  LHS.swap(RHS);
}

} // end namespace std

#endif // LLVM_ADT_SETVECTOR_H
