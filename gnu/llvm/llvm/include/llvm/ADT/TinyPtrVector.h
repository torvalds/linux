//===- llvm/ADT/TinyPtrVector.h - 'Normally tiny' vectors -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_TINYPTRVECTOR_H
#define LLVM_ADT_TINYPTRVECTOR_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/SmallVector.h"
#include <cassert>
#include <cstddef>
#include <iterator>
#include <type_traits>

namespace llvm {

/// TinyPtrVector - This class is specialized for cases where there are
/// normally 0 or 1 element in a vector, but is general enough to go beyond that
/// when required.
///
/// NOTE: This container doesn't allow you to store a null pointer into it.
///
template <typename EltTy>
class TinyPtrVector {
public:
  using VecTy = SmallVector<EltTy, 4>;
  using value_type = typename VecTy::value_type;
  // EltTy must be the first pointer type so that is<EltTy> is true for the
  // default-constructed PtrUnion. This allows an empty TinyPtrVector to
  // naturally vend a begin/end iterator of type EltTy* without an additional
  // check for the empty state.
  using PtrUnion = PointerUnion<EltTy, VecTy *>;

private:
  PtrUnion Val;

public:
  TinyPtrVector() = default;

  ~TinyPtrVector() {
    if (VecTy *V = dyn_cast_if_present<VecTy *>(Val))
      delete V;
  }

  TinyPtrVector(const TinyPtrVector &RHS) : Val(RHS.Val) {
    if (VecTy *V = dyn_cast_if_present<VecTy *>(Val))
      Val = new VecTy(*V);
  }

  TinyPtrVector &operator=(const TinyPtrVector &RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // Try to squeeze into the single slot. If it won't fit, allocate a copied
    // vector.
    if (isa<EltTy>(Val)) {
      if (RHS.size() == 1)
        Val = RHS.front();
      else
        Val = new VecTy(*cast<VecTy *>(RHS.Val));
      return *this;
    }

    // If we have a full vector allocated, try to re-use it.
    if (isa<EltTy>(RHS.Val)) {
      cast<VecTy *>(Val)->clear();
      cast<VecTy *>(Val)->push_back(RHS.front());
    } else {
      *cast<VecTy *>(Val) = *cast<VecTy *>(RHS.Val);
    }
    return *this;
  }

  TinyPtrVector(TinyPtrVector &&RHS) : Val(RHS.Val) {
    RHS.Val = (EltTy)nullptr;
  }

  TinyPtrVector &operator=(TinyPtrVector &&RHS) {
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->clear();
      return *this;
    }

    // If this vector has been allocated on the heap, re-use it if cheap. If it
    // would require more copying, just delete it and we'll steal the other
    // side.
    if (VecTy *V = dyn_cast_if_present<VecTy *>(Val)) {
      if (isa<EltTy>(RHS.Val)) {
        V->clear();
        V->push_back(RHS.front());
        RHS.Val = EltTy();
        return *this;
      }
      delete V;
    }

    Val = RHS.Val;
    RHS.Val = EltTy();
    return *this;
  }

  TinyPtrVector(std::initializer_list<EltTy> IL)
      : Val(IL.size() == 0
                ? PtrUnion()
                : IL.size() == 1 ? PtrUnion(*IL.begin())
                                 : PtrUnion(new VecTy(IL.begin(), IL.end()))) {}

  /// Constructor from an ArrayRef.
  ///
  /// This also is a constructor for individual array elements due to the single
  /// element constructor for ArrayRef.
  explicit TinyPtrVector(ArrayRef<EltTy> Elts)
      : Val(Elts.empty()
                ? PtrUnion()
                : Elts.size() == 1
                      ? PtrUnion(Elts[0])
                      : PtrUnion(new VecTy(Elts.begin(), Elts.end()))) {}

  TinyPtrVector(size_t Count, EltTy Value)
      : Val(Count == 0 ? PtrUnion()
                       : Count == 1 ? PtrUnion(Value)
                                    : PtrUnion(new VecTy(Count, Value))) {}

  // implicit conversion operator to ArrayRef.
  operator ArrayRef<EltTy>() const {
    if (Val.isNull())
      return std::nullopt;
    if (isa<EltTy>(Val))
      return *Val.getAddrOfPtr1();
    return *cast<VecTy *>(Val);
  }

  // implicit conversion operator to MutableArrayRef.
  operator MutableArrayRef<EltTy>() {
    if (Val.isNull())
      return std::nullopt;
    if (isa<EltTy>(Val))
      return *Val.getAddrOfPtr1();
    return *cast<VecTy *>(Val);
  }

  // Implicit conversion to ArrayRef<U> if EltTy* implicitly converts to U*.
  template <
      typename U,
      std::enable_if_t<std::is_convertible<ArrayRef<EltTy>, ArrayRef<U>>::value,
                       bool> = false>
  operator ArrayRef<U>() const {
    return operator ArrayRef<EltTy>();
  }

  bool empty() const {
    // This vector can be empty if it contains no element, or if it
    // contains a pointer to an empty vector.
    if (Val.isNull()) return true;
    if (VecTy *Vec = dyn_cast_if_present<VecTy *>(Val))
      return Vec->empty();
    return false;
  }

  unsigned size() const {
    if (empty())
      return 0;
    if (isa<EltTy>(Val))
      return 1;
    return cast<VecTy *>(Val)->size();
  }

  using iterator = EltTy *;
  using const_iterator = const EltTy *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() {
    if (isa<EltTy>(Val))
      return Val.getAddrOfPtr1();

    return cast<VecTy *>(Val)->begin();
  }

  iterator end() {
    if (isa<EltTy>(Val))
      return begin() + (Val.isNull() ? 0 : 1);

    return cast<VecTy *>(Val)->end();
  }

  const_iterator begin() const {
    return (const_iterator)const_cast<TinyPtrVector*>(this)->begin();
  }

  const_iterator end() const {
    return (const_iterator)const_cast<TinyPtrVector*>(this)->end();
  }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  EltTy operator[](unsigned i) const {
    assert(!Val.isNull() && "can't index into an empty vector");
    if (isa<EltTy>(Val)) {
      assert(i == 0 && "tinyvector index out of range");
      return cast<EltTy>(Val);
    }

    assert(i < cast<VecTy *>(Val)->size() && "tinyvector index out of range");
    return (*cast<VecTy *>(Val))[i];
  }

  EltTy front() const {
    assert(!empty() && "vector empty");
    if (isa<EltTy>(Val))
      return cast<EltTy>(Val);
    return cast<VecTy *>(Val)->front();
  }

  EltTy back() const {
    assert(!empty() && "vector empty");
    if (isa<EltTy>(Val))
      return cast<EltTy>(Val);
    return cast<VecTy *>(Val)->back();
  }

  void push_back(EltTy NewVal) {
    // If we have nothing, add something.
    if (Val.isNull()) {
      Val = NewVal;
      assert(!Val.isNull() && "Can't add a null value");
      return;
    }

    // If we have a single value, convert to a vector.
    if (isa<EltTy>(Val)) {
      EltTy V = cast<EltTy>(Val);
      Val = new VecTy();
      cast<VecTy *>(Val)->push_back(V);
    }

    // Add the new value, we know we have a vector.
    cast<VecTy *>(Val)->push_back(NewVal);
  }

  void pop_back() {
    // If we have a single value, convert to empty.
    if (isa<EltTy>(Val))
      Val = (EltTy)nullptr;
    else if (VecTy *Vec = cast<VecTy *>(Val))
      Vec->pop_back();
  }

  void clear() {
    // If we have a single value, convert to empty.
    if (isa<EltTy>(Val)) {
      Val = EltTy();
    } else if (VecTy *Vec = dyn_cast_if_present<VecTy *>(Val)) {
      // If we have a vector form, just clear it.
      Vec->clear();
    }
    // Otherwise, we're already empty.
  }

  iterator erase(iterator I) {
    assert(I >= begin() && "Iterator to erase is out of bounds.");
    assert(I < end() && "Erasing at past-the-end iterator.");

    // If we have a single value, convert to empty.
    if (isa<EltTy>(Val)) {
      if (I == begin())
        Val = EltTy();
    } else if (VecTy *Vec = dyn_cast_if_present<VecTy *>(Val)) {
      // multiple items in a vector; just do the erase, there is no
      // benefit to collapsing back to a pointer
      return Vec->erase(I);
    }
    return end();
  }

  iterator erase(iterator S, iterator E) {
    assert(S >= begin() && "Range to erase is out of bounds.");
    assert(S <= E && "Trying to erase invalid range.");
    assert(E <= end() && "Trying to erase past the end.");

    if (isa<EltTy>(Val)) {
      if (S == begin() && S != E)
        Val = EltTy();
    } else if (VecTy *Vec = dyn_cast_if_present<VecTy *>(Val)) {
      return Vec->erase(S, E);
    }
    return end();
  }

  iterator insert(iterator I, const EltTy &Elt) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (I == end()) {
      push_back(Elt);
      return std::prev(end());
    }
    assert(!Val.isNull() && "Null value with non-end insert iterator.");
    if (isa<EltTy>(Val)) {
      EltTy V = cast<EltTy>(Val);
      assert(I == begin());
      Val = Elt;
      push_back(V);
      return begin();
    }

    return cast<VecTy *>(Val)->insert(I, Elt);
  }

  template<typename ItTy>
  iterator insert(iterator I, ItTy From, ItTy To) {
    assert(I >= this->begin() && "Insertion iterator is out of bounds.");
    assert(I <= this->end() && "Inserting past the end of the vector.");
    if (From == To)
      return I;

    // If we have a single value, convert to a vector.
    ptrdiff_t Offset = I - begin();
    if (Val.isNull()) {
      if (std::next(From) == To) {
        Val = *From;
        return begin();
      }

      Val = new VecTy();
    } else if (isa<EltTy>(Val)) {
      EltTy V = cast<EltTy>(Val);
      Val = new VecTy();
      cast<VecTy *>(Val)->push_back(V);
    }
    return cast<VecTy *>(Val)->insert(begin() + Offset, From, To);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_TINYPTRVECTOR_H
