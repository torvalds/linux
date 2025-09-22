//===- ASTVector.h - Vector that uses ASTContext for allocation ---*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file provides ASTVector, a vector  ADT whose contents are
//  allocated using the allocator associated with an ASTContext..
//
//===----------------------------------------------------------------------===//

// FIXME: Most of this is copy-and-paste from BumpVector.h and SmallVector.h.
// We can refactor this core logic into something common.

#ifndef LLVM_CLANG_AST_ASTVECTOR_H
#define LLVM_CLANG_AST_ASTVECTOR_H

#include "clang/AST/ASTContextAllocate.h"
#include "llvm/ADT/PointerIntPair.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

namespace clang {

class ASTContext;

template<typename T>
class ASTVector {
private:
  T *Begin = nullptr;
  T *End = nullptr;
  llvm::PointerIntPair<T *, 1, bool> Capacity;

  void setEnd(T *P) { this->End = P; }

protected:
  // Make a tag bit available to users of this class.
  // FIXME: This is a horrible hack.
  bool getTag() const { return Capacity.getInt(); }
  void setTag(bool B) { Capacity.setInt(B); }

public:
  // Default ctor - Initialize to empty.
  ASTVector() : Capacity(nullptr, false) {}

  ASTVector(ASTVector &&O) : Begin(O.Begin), End(O.End), Capacity(O.Capacity) {
    O.Begin = O.End = nullptr;
    O.Capacity.setPointer(nullptr);
    O.Capacity.setInt(false);
  }

  ASTVector(const ASTContext &C, unsigned N) : Capacity(nullptr, false) {
    reserve(C, N);
  }

  ASTVector &operator=(ASTVector &&RHS) {
    ASTVector O(std::move(RHS));

    using std::swap;

    swap(Begin, O.Begin);
    swap(End, O.End);
    swap(Capacity, O.Capacity);
    return *this;
  }

  ~ASTVector() {
    if (std::is_class<T>::value) {
      // Destroy the constructed elements in the vector.
      destroy_range(Begin, End);
    }
  }

  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using value_type = T;
  using iterator = T *;
  using const_iterator = const T *;

  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = std::reverse_iterator<iterator>;

  using reference = T &;
  using const_reference = const T &;
  using pointer = T *;
  using const_pointer = const T *;

  // forward iterator creation methods.
  iterator begin() { return Begin; }
  const_iterator begin() const { return Begin; }
  iterator end() { return End; }
  const_iterator end() const { return End; }

  // reverse iterator creation methods.
  reverse_iterator rbegin()            { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const{ return const_reverse_iterator(end()); }
  reverse_iterator rend()              { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin());}

  bool empty() const { return Begin == End; }
  size_type size() const { return End-Begin; }

  reference operator[](unsigned idx) {
    assert(Begin + idx < End);
    return Begin[idx];
  }
  const_reference operator[](unsigned idx) const {
    assert(Begin + idx < End);
    return Begin[idx];
  }

  reference front() {
    return begin()[0];
  }
  const_reference front() const {
    return begin()[0];
  }

  reference back() {
    return end()[-1];
  }
  const_reference back() const {
    return end()[-1];
  }

  void pop_back() {
    --End;
    End->~T();
  }

  T pop_back_val() {
    T Result = back();
    pop_back();
    return Result;
  }

  void clear() {
    if (std::is_class<T>::value) {
      destroy_range(Begin, End);
    }
    End = Begin;
  }

  /// data - Return a pointer to the vector's buffer, even if empty().
  pointer data() {
    return pointer(Begin);
  }

  /// data - Return a pointer to the vector's buffer, even if empty().
  const_pointer data() const {
    return const_pointer(Begin);
  }

  void push_back(const_reference Elt, const ASTContext &C) {
    if (End < this->capacity_ptr()) {
    Retry:
      new (End) T(Elt);
      ++End;
      return;
    }
    grow(C);
    goto Retry;
  }

  void reserve(const ASTContext &C, unsigned N) {
    if (unsigned(this->capacity_ptr()-Begin) < N)
      grow(C, N);
  }

  /// capacity - Return the total number of elements in the currently allocated
  /// buffer.
  size_t capacity() const { return this->capacity_ptr() - Begin; }

  /// append - Add the specified range to the end of the SmallVector.
  template<typename in_iter>
  void append(const ASTContext &C, in_iter in_start, in_iter in_end) {
    size_type NumInputs = std::distance(in_start, in_end);

    if (NumInputs == 0)
      return;

    // Grow allocated space if needed.
    if (NumInputs > size_type(this->capacity_ptr()-this->end()))
      this->grow(C, this->size()+NumInputs);

    // Copy the new elements over.
    // TODO: NEED To compile time dispatch on whether in_iter is a random access
    // iterator to use the fast uninitialized_copy.
    std::uninitialized_copy(in_start, in_end, this->end());
    this->setEnd(this->end() + NumInputs);
  }

  /// append - Add the specified range to the end of the SmallVector.
  void append(const ASTContext &C, size_type NumInputs, const T &Elt) {
    // Grow allocated space if needed.
    if (NumInputs > size_type(this->capacity_ptr()-this->end()))
      this->grow(C, this->size()+NumInputs);

    // Copy the new elements over.
    std::uninitialized_fill_n(this->end(), NumInputs, Elt);
    this->setEnd(this->end() + NumInputs);
  }

  /// uninitialized_copy - Copy the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template<typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    std::uninitialized_copy(I, E, Dest);
  }

  iterator insert(const ASTContext &C, iterator I, const T &Elt) {
    if (I == this->end()) {  // Important special case for empty vector.
      push_back(Elt, C);
      return this->end()-1;
    }

    if (this->End < this->capacity_ptr()) {
    Retry:
      new (this->end()) T(this->back());
      this->setEnd(this->end()+1);
      // Push everything else over.
      std::copy_backward(I, this->end()-1, this->end());
      *I = Elt;
      return I;
    }
    size_t EltNo = I-this->begin();
    this->grow(C);
    I = this->begin()+EltNo;
    goto Retry;
  }

  iterator insert(const ASTContext &C, iterator I, size_type NumToInsert,
                  const T &Elt) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) { // Important special case for empty vector.
      append(C, NumToInsert, Elt);
      return this->begin() + InsertElt;
    }

    // Ensure there is enough space.
    reserve(C, static_cast<unsigned>(this->size() + NumToInsert));

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(C, this->end()-NumToInsert, this->end());

      // Copy the existing elements that get replaced.
      std::copy_backward(I, OldEnd-NumToInsert, OldEnd);

      std::fill_n(I, NumToInsert, Elt);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Copy over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->setEnd(this->end() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_copy(I, OldEnd, this->end()-NumOverwritten);

    // Replace the overwritten part.
    std::fill_n(I, NumOverwritten, Elt);

    // Insert the non-overwritten middle part.
    std::uninitialized_fill_n(OldEnd, NumToInsert-NumOverwritten, Elt);
    return I;
  }

  template<typename ItTy>
  iterator insert(const ASTContext &C, iterator I, ItTy From, ItTy To) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) { // Important special case for empty vector.
      append(C, From, To);
      return this->begin() + InsertElt;
    }

    size_t NumToInsert = std::distance(From, To);

    // Ensure there is enough space.
    reserve(C, static_cast<unsigned>(this->size() + NumToInsert));

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(C, this->end()-NumToInsert, this->end());

      // Copy the existing elements that get replaced.
      std::copy_backward(I, OldEnd-NumToInsert, OldEnd);

      std::copy(From, To, I);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Copy over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->setEnd(this->end() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_copy(I, OldEnd, this->end()-NumOverwritten);

    // Replace the overwritten part.
    for (; NumOverwritten > 0; --NumOverwritten) {
      *I = *From;
      ++I; ++From;
    }

    // Insert the non-overwritten middle part.
    this->uninitialized_copy(From, To, OldEnd);
    return I;
  }

  void resize(const ASTContext &C, unsigned N, const T &NV) {
    if (N < this->size()) {
      this->destroy_range(this->begin()+N, this->end());
      this->setEnd(this->begin()+N);
    } else if (N > this->size()) {
      if (this->capacity() < N)
        this->grow(C, N);
      construct_range(this->end(), this->begin()+N, NV);
      this->setEnd(this->begin()+N);
    }
  }

private:
  /// grow - double the size of the allocated memory, guaranteeing space for at
  /// least one more element or MinSize if specified.
  void grow(const ASTContext &C, size_type MinSize = 1);

  void construct_range(T *S, T *E, const T &Elt) {
    for (; S != E; ++S)
      new (S) T(Elt);
  }

  void destroy_range(T *S, T *E) {
    while (S != E) {
      --E;
      E->~T();
    }
  }

protected:
  const_iterator capacity_ptr() const {
    return (iterator) Capacity.getPointer();
  }

  iterator capacity_ptr() { return (iterator)Capacity.getPointer(); }
};

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T>
void ASTVector<T>::grow(const ASTContext &C, size_t MinSize) {
  size_t CurCapacity = this->capacity();
  size_t CurSize = size();
  size_t NewCapacity = 2*CurCapacity;
  if (NewCapacity < MinSize)
    NewCapacity = MinSize;

  // Allocate the memory from the ASTContext.
  T *NewElts = new (C, alignof(T)) T[NewCapacity];

  // Copy the elements over.
  if (Begin != End) {
    if (std::is_class<T>::value) {
      std::uninitialized_copy(Begin, End, NewElts);
      // Destroy the original elements.
      destroy_range(Begin, End);
    } else {
      // Use memcpy for PODs (std::uninitialized_copy optimizes to memmove).
      memcpy(NewElts, Begin, CurSize * sizeof(T));
    }
  }

  // ASTContext never frees any memory.
  Begin = NewElts;
  End = NewElts+CurSize;
  Capacity.setPointer(Begin+NewCapacity);
}

} // namespace clang

#endif // LLVM_CLANG_AST_ASTVECTOR_H
