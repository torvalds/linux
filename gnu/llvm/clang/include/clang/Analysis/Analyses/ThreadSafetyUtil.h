//===- ThreadSafetyUtil.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines some basic utility classes for use by ThreadSafetyTIL.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYUTIL_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYUTIL_H

#include "clang/AST/Decl.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Allocator.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

namespace clang {

class Expr;

namespace threadSafety {
namespace til {

// Simple wrapper class to abstract away from the details of memory management.
// SExprs are allocated in pools, and deallocated all at once.
class MemRegionRef {
private:
  union AlignmentType {
    double d;
    void *p;
    long double dd;
    long long ii;
  };

public:
  MemRegionRef() = default;
  MemRegionRef(llvm::BumpPtrAllocator *A) : Allocator(A) {}

  void *allocate(size_t Sz) {
    return Allocator->Allocate(Sz, alignof(AlignmentType));
  }

  template <typename T> T *allocateT() { return Allocator->Allocate<T>(); }

  template <typename T> T *allocateT(size_t NumElems) {
    return Allocator->Allocate<T>(NumElems);
  }

private:
  llvm::BumpPtrAllocator *Allocator = nullptr;
};

} // namespace til
} // namespace threadSafety

} // namespace clang

inline void *operator new(size_t Sz,
                          clang::threadSafety::til::MemRegionRef &R) {
  return R.allocate(Sz);
}

namespace clang {
namespace threadSafety {

std::string getSourceLiteralString(const Expr *CE);

namespace til {

// A simple fixed size array class that does not manage its own memory,
// suitable for use with bump pointer allocation.
template <class T> class SimpleArray {
public:
  SimpleArray() = default;
  SimpleArray(T *Dat, size_t Cp, size_t Sz = 0)
      : Data(Dat), Size(Sz), Capacity(Cp) {}
  SimpleArray(MemRegionRef A, size_t Cp)
      : Data(Cp == 0 ? nullptr : A.allocateT<T>(Cp)), Capacity(Cp) {}
  SimpleArray(const SimpleArray<T> &A) = delete;

  SimpleArray(SimpleArray<T> &&A)
      : Data(A.Data), Size(A.Size), Capacity(A.Capacity) {
    A.Data = nullptr;
    A.Size = 0;
    A.Capacity = 0;
  }

  SimpleArray &operator=(SimpleArray &&RHS) {
    if (this != &RHS) {
      Data = RHS.Data;
      Size = RHS.Size;
      Capacity = RHS.Capacity;

      RHS.Data = nullptr;
      RHS.Size = RHS.Capacity = 0;
    }
    return *this;
  }

  // Reserve space for at least Ncp items, reallocating if necessary.
  void reserve(size_t Ncp, MemRegionRef A) {
    if (Ncp <= Capacity)
      return;
    T *Odata = Data;
    Data = A.allocateT<T>(Ncp);
    Capacity = Ncp;
    memcpy(Data, Odata, sizeof(T) * Size);
  }

  // Reserve space for at least N more items.
  void reserveCheck(size_t N, MemRegionRef A) {
    if (Capacity == 0)
      reserve(u_max(InitialCapacity, N), A);
    else if (Size + N < Capacity)
      reserve(u_max(Size + N, Capacity * 2), A);
  }

  using iterator = T *;
  using const_iterator = const T *;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  size_t size() const { return Size; }
  size_t capacity() const { return Capacity; }

  T &operator[](unsigned i) {
    assert(i < Size && "Array index out of bounds.");
    return Data[i];
  }

  const T &operator[](unsigned i) const {
    assert(i < Size && "Array index out of bounds.");
    return Data[i];
  }

  T &back() {
    assert(Size && "No elements in the array.");
    return Data[Size - 1];
  }

  const T &back() const {
    assert(Size && "No elements in the array.");
    return Data[Size - 1];
  }

  iterator begin() { return Data; }
  iterator end() { return Data + Size; }

  const_iterator begin() const { return Data; }
  const_iterator end() const { return Data + Size; }

  const_iterator cbegin() const { return Data; }
  const_iterator cend() const { return Data + Size; }

  reverse_iterator rbegin() { return reverse_iterator(end()); }
  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(begin());
  }

  void push_back(const T &Elem) {
    assert(Size < Capacity);
    Data[Size++] = Elem;
  }

  // drop last n elements from array
  void drop(unsigned n = 0) {
    assert(Size > n);
    Size -= n;
  }

  void setValues(unsigned Sz, const T& C) {
    assert(Sz <= Capacity);
    Size = Sz;
    for (unsigned i = 0; i < Sz; ++i) {
      Data[i] = C;
    }
  }

  template <class Iter> unsigned append(Iter I, Iter E) {
    size_t Osz = Size;
    size_t J = Osz;
    for (; J < Capacity && I != E; ++J, ++I)
      Data[J] = *I;
    Size = J;
    return J - Osz;
  }

  llvm::iterator_range<reverse_iterator> reverse() {
    return llvm::reverse(*this);
  }

  llvm::iterator_range<const_reverse_iterator> reverse() const {
    return llvm::reverse(*this);
  }

private:
  // std::max is annoying here, because it requires a reference,
  // thus forcing InitialCapacity to be initialized outside the .h file.
  size_t u_max(size_t i, size_t j) { return (i < j) ? j : i; }

  static const size_t InitialCapacity = 4;

  T *Data = nullptr;
  size_t Size = 0;
  size_t Capacity = 0;
};

}  // namespace til

// A copy on write vector.
// The vector can be in one of three states:
// * invalid -- no operations are permitted.
// * read-only -- read operations are permitted.
// * writable -- read and write operations are permitted.
// The init(), destroy(), and makeWritable() methods will change state.
template<typename T>
class CopyOnWriteVector {
  class VectorData {
  public:
    unsigned NumRefs = 1;
    std::vector<T> Vect;

    VectorData() = default;
    VectorData(const VectorData &VD) : Vect(VD.Vect) {}

    // The copy assignment operator is defined as deleted pending further
    // motivation.
    VectorData &operator=(const VectorData &) = delete;
  };

public:
  CopyOnWriteVector() = default;
  CopyOnWriteVector(CopyOnWriteVector &&V) : Data(V.Data) { V.Data = nullptr; }

  CopyOnWriteVector &operator=(CopyOnWriteVector &&V) {
    destroy();
    Data = V.Data;
    V.Data = nullptr;
    return *this;
  }

  // No copy constructor or copy assignment.  Use clone() with move assignment.
  CopyOnWriteVector(const CopyOnWriteVector &) = delete;
  CopyOnWriteVector &operator=(const CopyOnWriteVector &) = delete;

  ~CopyOnWriteVector() { destroy(); }

  // Returns true if this holds a valid vector.
  bool valid() const  { return Data; }

  // Returns true if this vector is writable.
  bool writable() const { return Data && Data->NumRefs == 1; }

  // If this vector is not valid, initialize it to a valid vector.
  void init() {
    if (!Data) {
      Data = new VectorData();
    }
  }

  // Destroy this vector; thus making it invalid.
  void destroy() {
    if (!Data)
      return;
    if (Data->NumRefs <= 1)
      delete Data;
    else
      --Data->NumRefs;
    Data = nullptr;
  }

  // Make this vector writable, creating a copy if needed.
  void makeWritable() {
    if (!Data) {
      Data = new VectorData();
      return;
    }
    if (Data->NumRefs == 1)
      return;   // already writeable.
    --Data->NumRefs;
    Data = new VectorData(*Data);
  }

  // Create a lazy copy of this vector.
  CopyOnWriteVector clone() { return CopyOnWriteVector(Data); }

  using const_iterator = typename std::vector<T>::const_iterator;

  const std::vector<T> &elements() const { return Data->Vect; }

  const_iterator begin() const { return elements().cbegin(); }
  const_iterator end() const { return elements().cend(); }

  const T& operator[](unsigned i) const { return elements()[i]; }

  unsigned size() const { return Data ? elements().size() : 0; }

  // Return true if V and this vector refer to the same data.
  bool sameAs(const CopyOnWriteVector &V) const { return Data == V.Data; }

  // Clear vector.  The vector must be writable.
  void clear() {
    assert(writable() && "Vector is not writable!");
    Data->Vect.clear();
  }

  // Push a new element onto the end.  The vector must be writable.
  void push_back(const T &Elem) {
    assert(writable() && "Vector is not writable!");
    Data->Vect.push_back(Elem);
  }

  // Gets a mutable reference to the element at index(i).
  // The vector must be writable.
  T& elem(unsigned i) {
    assert(writable() && "Vector is not writable!");
    return Data->Vect[i];
  }

  // Drops elements from the back until the vector has size i.
  void downsize(unsigned i) {
    assert(writable() && "Vector is not writable!");
    Data->Vect.erase(Data->Vect.begin() + i, Data->Vect.end());
  }

private:
  CopyOnWriteVector(VectorData *D) : Data(D) {
    if (!Data)
      return;
    ++Data->NumRefs;
  }

  VectorData *Data = nullptr;
};

inline std::ostream& operator<<(std::ostream& ss, const StringRef str) {
  return ss.write(str.data(), str.size());
}

} // namespace threadSafety
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_THREADSAFETYUTIL_H
