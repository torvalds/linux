//===- llvm/ADT/SmallVector.h - 'Normally small' vectors --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the SmallVector class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_SMALLVECTOR_H
#define LLVM_ADT_SMALLVECTOR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace llvm {

template <typename T> class ArrayRef;

template <typename IteratorT> class iterator_range;

template <class Iterator>
using EnableIfConvertibleToInputIterator = std::enable_if_t<std::is_convertible<
    typename std::iterator_traits<Iterator>::iterator_category,
    std::input_iterator_tag>::value>;

/// This is all the stuff common to all SmallVectors.
///
/// The template parameter specifies the type which should be used to hold the
/// Size and Capacity of the SmallVector, so it can be adjusted.
/// Using 32 bit size is desirable to shrink the size of the SmallVector.
/// Using 64 bit size is desirable for cases like SmallVector<char>, where a
/// 32 bit size would limit the vector to ~4GB. SmallVectors are used for
/// buffering bitcode output - which can exceed 4GB.
template <class Size_T> class SmallVectorBase {
protected:
  void *BeginX;
  Size_T Size = 0, Capacity;

  /// The maximum value of the Size_T used.
  static constexpr size_t SizeTypeMax() {
    return std::numeric_limits<Size_T>::max();
  }

  SmallVectorBase() = delete;
  SmallVectorBase(void *FirstEl, size_t TotalCapacity)
      : BeginX(FirstEl), Capacity(static_cast<Size_T>(TotalCapacity)) {}

  /// This is a helper for \a grow() that's out of line to reduce code
  /// duplication.  This function will report a fatal error if it can't grow at
  /// least to \p MinSize.
  void *mallocForGrow(void *FirstEl, size_t MinSize, size_t TSize,
                      size_t &NewCapacity);

  /// This is an implementation of the grow() method which only works
  /// on POD-like data types and is out of line to reduce code duplication.
  /// This function will report a fatal error if it cannot increase capacity.
  void grow_pod(void *FirstEl, size_t MinSize, size_t TSize);

  /// If vector was first created with capacity 0, getFirstEl() points to the
  /// memory right after, an area unallocated. If a subsequent allocation,
  /// that grows the vector, happens to return the same pointer as getFirstEl(),
  /// get a new allocation, otherwise isSmall() will falsely return that no
  /// allocation was done (true) and the memory will not be freed in the
  /// destructor. If a VSize is given (vector size), also copy that many
  /// elements to the new allocation - used if realloca fails to increase
  /// space, and happens to allocate precisely at BeginX.
  /// This is unlikely to be called often, but resolves a memory leak when the
  /// situation does occur.
  void *replaceAllocation(void *NewElts, size_t TSize, size_t NewCapacity,
                          size_t VSize = 0);

public:
  size_t size() const { return Size; }
  size_t capacity() const { return Capacity; }

  [[nodiscard]] bool empty() const { return !Size; }

protected:
  /// Set the array size to \p N, which the current array must have enough
  /// capacity for.
  ///
  /// This does not construct or destroy any elements in the vector.
  void set_size(size_t N) {
    assert(N <= capacity()); // implies no overflow in assignment
    Size = static_cast<Size_T>(N);
  }

  /// Set the array data pointer to \p Begin and capacity to \p N.
  ///
  /// This does not construct or destroy any elements in the vector.
  //  This does not clean up any existing allocation.
  void set_allocation_range(void *Begin, size_t N) {
    assert(N <= SizeTypeMax());
    BeginX = Begin;
    Capacity = static_cast<Size_T>(N);
  }
};

template <class T>
using SmallVectorSizeType =
    std::conditional_t<sizeof(T) < 4 && sizeof(void *) >= 8, uint64_t,
                       uint32_t>;

/// Figure out the offset of the first element.
template <class T, typename = void> struct SmallVectorAlignmentAndSize {
  alignas(SmallVectorBase<SmallVectorSizeType<T>>) char Base[sizeof(
      SmallVectorBase<SmallVectorSizeType<T>>)];
  alignas(T) char FirstEl[sizeof(T)];
};

/// This is the part of SmallVectorTemplateBase which does not depend on whether
/// the type T is a POD. The extra dummy template argument is used by ArrayRef
/// to avoid unnecessarily requiring T to be complete.
template <typename T, typename = void>
class SmallVectorTemplateCommon
    : public SmallVectorBase<SmallVectorSizeType<T>> {
  using Base = SmallVectorBase<SmallVectorSizeType<T>>;

protected:
  /// Find the address of the first element.  For this pointer math to be valid
  /// with small-size of 0 for T with lots of alignment, it's important that
  /// SmallVectorStorage is properly-aligned even for small-size of 0.
  void *getFirstEl() const {
    return const_cast<void *>(reinterpret_cast<const void *>(
        reinterpret_cast<const char *>(this) +
        offsetof(SmallVectorAlignmentAndSize<T>, FirstEl)));
  }
  // Space after 'FirstEl' is clobbered, do not add any instance vars after it.

  SmallVectorTemplateCommon(size_t Size) : Base(getFirstEl(), Size) {}

  void grow_pod(size_t MinSize, size_t TSize) {
    Base::grow_pod(getFirstEl(), MinSize, TSize);
  }

  /// Return true if this is a smallvector which has not had dynamic
  /// memory allocated for it.
  bool isSmall() const { return this->BeginX == getFirstEl(); }

  /// Put this vector in a state of being small.
  void resetToSmall() {
    this->BeginX = getFirstEl();
    this->Size = this->Capacity = 0; // FIXME: Setting Capacity to 0 is suspect.
  }

  /// Return true if V is an internal reference to the given range.
  bool isReferenceToRange(const void *V, const void *First, const void *Last) const {
    // Use std::less to avoid UB.
    std::less<> LessThan;
    return !LessThan(V, First) && LessThan(V, Last);
  }

  /// Return true if V is an internal reference to this vector.
  bool isReferenceToStorage(const void *V) const {
    return isReferenceToRange(V, this->begin(), this->end());
  }

  /// Return true if First and Last form a valid (possibly empty) range in this
  /// vector's storage.
  bool isRangeInStorage(const void *First, const void *Last) const {
    // Use std::less to avoid UB.
    std::less<> LessThan;
    return !LessThan(First, this->begin()) && !LessThan(Last, First) &&
           !LessThan(this->end(), Last);
  }

  /// Return true unless Elt will be invalidated by resizing the vector to
  /// NewSize.
  bool isSafeToReferenceAfterResize(const void *Elt, size_t NewSize) {
    // Past the end.
    if (LLVM_LIKELY(!isReferenceToStorage(Elt)))
      return true;

    // Return false if Elt will be destroyed by shrinking.
    if (NewSize <= this->size())
      return Elt < this->begin() + NewSize;

    // Return false if we need to grow.
    return NewSize <= this->capacity();
  }

  /// Check whether Elt will be invalidated by resizing the vector to NewSize.
  void assertSafeToReferenceAfterResize(const void *Elt, size_t NewSize) {
    assert(isSafeToReferenceAfterResize(Elt, NewSize) &&
           "Attempting to reference an element of the vector in an operation "
           "that invalidates it");
  }

  /// Check whether Elt will be invalidated by increasing the size of the
  /// vector by N.
  void assertSafeToAdd(const void *Elt, size_t N = 1) {
    this->assertSafeToReferenceAfterResize(Elt, this->size() + N);
  }

  /// Check whether any part of the range will be invalidated by clearing.
  void assertSafeToReferenceAfterClear(const T *From, const T *To) {
    if (From == To)
      return;
    this->assertSafeToReferenceAfterResize(From, 0);
    this->assertSafeToReferenceAfterResize(To - 1, 0);
  }
  template <
      class ItTy,
      std::enable_if_t<!std::is_same<std::remove_const_t<ItTy>, T *>::value,
                       bool> = false>
  void assertSafeToReferenceAfterClear(ItTy, ItTy) {}

  /// Check whether any part of the range will be invalidated by growing.
  void assertSafeToAddRange(const T *From, const T *To) {
    if (From == To)
      return;
    this->assertSafeToAdd(From, To - From);
    this->assertSafeToAdd(To - 1, To - From);
  }
  template <
      class ItTy,
      std::enable_if_t<!std::is_same<std::remove_const_t<ItTy>, T *>::value,
                       bool> = false>
  void assertSafeToAddRange(ItTy, ItTy) {}

  /// Reserve enough space to add one element, and return the updated element
  /// pointer in case it was a reference to the storage.
  template <class U>
  static const T *reserveForParamAndGetAddressImpl(U *This, const T &Elt,
                                                   size_t N) {
    size_t NewSize = This->size() + N;
    if (LLVM_LIKELY(NewSize <= This->capacity()))
      return &Elt;

    bool ReferencesStorage = false;
    int64_t Index = -1;
    if (!U::TakesParamByValue) {
      if (LLVM_UNLIKELY(This->isReferenceToStorage(&Elt))) {
        ReferencesStorage = true;
        Index = &Elt - This->begin();
      }
    }
    This->grow(NewSize);
    return ReferencesStorage ? This->begin() + Index : &Elt;
  }

public:
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

  using Base::capacity;
  using Base::empty;
  using Base::size;

  // forward iterator creation methods.
  iterator begin() { return (iterator)this->BeginX; }
  const_iterator begin() const { return (const_iterator)this->BeginX; }
  iterator end() { return begin() + size(); }
  const_iterator end() const { return begin() + size(); }

  // reverse iterator creation methods.
  reverse_iterator rbegin()            { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const{ return const_reverse_iterator(end()); }
  reverse_iterator rend()              { return reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return const_reverse_iterator(begin());}

  size_type size_in_bytes() const { return size() * sizeof(T); }
  size_type max_size() const {
    return std::min(this->SizeTypeMax(), size_type(-1) / sizeof(T));
  }

  size_t capacity_in_bytes() const { return capacity() * sizeof(T); }

  /// Return a pointer to the vector's buffer, even if empty().
  pointer data() { return pointer(begin()); }
  /// Return a pointer to the vector's buffer, even if empty().
  const_pointer data() const { return const_pointer(begin()); }

  reference operator[](size_type idx) {
    assert(idx < size());
    return begin()[idx];
  }
  const_reference operator[](size_type idx) const {
    assert(idx < size());
    return begin()[idx];
  }

  reference front() {
    assert(!empty());
    return begin()[0];
  }
  const_reference front() const {
    assert(!empty());
    return begin()[0];
  }

  reference back() {
    assert(!empty());
    return end()[-1];
  }
  const_reference back() const {
    assert(!empty());
    return end()[-1];
  }
};

/// SmallVectorTemplateBase<TriviallyCopyable = false> - This is where we put
/// method implementations that are designed to work with non-trivial T's.
///
/// We approximate is_trivially_copyable with trivial move/copy construction and
/// trivial destruction. While the standard doesn't specify that you're allowed
/// copy these types with memcpy, there is no way for the type to observe this.
/// This catches the important case of std::pair<POD, POD>, which is not
/// trivially assignable.
template <typename T, bool = (std::is_trivially_copy_constructible<T>::value) &&
                             (std::is_trivially_move_constructible<T>::value) &&
                             std::is_trivially_destructible<T>::value>
class SmallVectorTemplateBase : public SmallVectorTemplateCommon<T> {
  friend class SmallVectorTemplateCommon<T>;

protected:
  static constexpr bool TakesParamByValue = false;
  using ValueParamT = const T &;

  SmallVectorTemplateBase(size_t Size) : SmallVectorTemplateCommon<T>(Size) {}

  static void destroy_range(T *S, T *E) {
    while (S != E) {
      --E;
      E->~T();
    }
  }

  /// Move the range [I, E) into the uninitialized memory starting with "Dest",
  /// constructing elements as needed.
  template<typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
    std::uninitialized_move(I, E, Dest);
  }

  /// Copy the range [I, E) onto the uninitialized memory starting with "Dest",
  /// constructing elements as needed.
  template<typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    std::uninitialized_copy(I, E, Dest);
  }

  /// Grow the allocated memory (without initializing new elements), doubling
  /// the size of the allocated memory. Guarantees space for at least one more
  /// element, or MinSize more elements if specified.
  void grow(size_t MinSize = 0);

  /// Create a new allocation big enough for \p MinSize and pass back its size
  /// in \p NewCapacity. This is the first section of \a grow().
  T *mallocForGrow(size_t MinSize, size_t &NewCapacity);

  /// Move existing elements over to the new allocation \p NewElts, the middle
  /// section of \a grow().
  void moveElementsForGrow(T *NewElts);

  /// Transfer ownership of the allocation, finishing up \a grow().
  void takeAllocationForGrow(T *NewElts, size_t NewCapacity);

  /// Reserve enough space to add one element, and return the updated element
  /// pointer in case it was a reference to the storage.
  const T *reserveForParamAndGetAddress(const T &Elt, size_t N = 1) {
    return this->reserveForParamAndGetAddressImpl(this, Elt, N);
  }

  /// Reserve enough space to add one element, and return the updated element
  /// pointer in case it was a reference to the storage.
  T *reserveForParamAndGetAddress(T &Elt, size_t N = 1) {
    return const_cast<T *>(
        this->reserveForParamAndGetAddressImpl(this, Elt, N));
  }

  static T &&forward_value_param(T &&V) { return std::move(V); }
  static const T &forward_value_param(const T &V) { return V; }

  void growAndAssign(size_t NumElts, const T &Elt) {
    // Grow manually in case Elt is an internal reference.
    size_t NewCapacity;
    T *NewElts = mallocForGrow(NumElts, NewCapacity);
    std::uninitialized_fill_n(NewElts, NumElts, Elt);
    this->destroy_range(this->begin(), this->end());
    takeAllocationForGrow(NewElts, NewCapacity);
    this->set_size(NumElts);
  }

  template <typename... ArgTypes> T &growAndEmplaceBack(ArgTypes &&... Args) {
    // Grow manually in case one of Args is an internal reference.
    size_t NewCapacity;
    T *NewElts = mallocForGrow(0, NewCapacity);
    ::new ((void *)(NewElts + this->size())) T(std::forward<ArgTypes>(Args)...);
    moveElementsForGrow(NewElts);
    takeAllocationForGrow(NewElts, NewCapacity);
    this->set_size(this->size() + 1);
    return this->back();
  }

public:
  void push_back(const T &Elt) {
    const T *EltPtr = reserveForParamAndGetAddress(Elt);
    ::new ((void *)this->end()) T(*EltPtr);
    this->set_size(this->size() + 1);
  }

  void push_back(T &&Elt) {
    T *EltPtr = reserveForParamAndGetAddress(Elt);
    ::new ((void *)this->end()) T(::std::move(*EltPtr));
    this->set_size(this->size() + 1);
  }

  void pop_back() {
    this->set_size(this->size() - 1);
    this->end()->~T();
  }
};

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::grow(size_t MinSize) {
  size_t NewCapacity;
  T *NewElts = mallocForGrow(MinSize, NewCapacity);
  moveElementsForGrow(NewElts);
  takeAllocationForGrow(NewElts, NewCapacity);
}

template <typename T, bool TriviallyCopyable>
T *SmallVectorTemplateBase<T, TriviallyCopyable>::mallocForGrow(
    size_t MinSize, size_t &NewCapacity) {
  return static_cast<T *>(
      SmallVectorBase<SmallVectorSizeType<T>>::mallocForGrow(
          this->getFirstEl(), MinSize, sizeof(T), NewCapacity));
}

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::moveElementsForGrow(
    T *NewElts) {
  // Move the elements over.
  this->uninitialized_move(this->begin(), this->end(), NewElts);

  // Destroy the original elements.
  destroy_range(this->begin(), this->end());
}

// Define this out-of-line to dissuade the C++ compiler from inlining it.
template <typename T, bool TriviallyCopyable>
void SmallVectorTemplateBase<T, TriviallyCopyable>::takeAllocationForGrow(
    T *NewElts, size_t NewCapacity) {
  // If this wasn't grown from the inline copy, deallocate the old space.
  if (!this->isSmall())
    free(this->begin());

  this->set_allocation_range(NewElts, NewCapacity);
}

/// SmallVectorTemplateBase<TriviallyCopyable = true> - This is where we put
/// method implementations that are designed to work with trivially copyable
/// T's. This allows using memcpy in place of copy/move construction and
/// skipping destruction.
template <typename T>
class SmallVectorTemplateBase<T, true> : public SmallVectorTemplateCommon<T> {
  friend class SmallVectorTemplateCommon<T>;

protected:
  /// True if it's cheap enough to take parameters by value. Doing so avoids
  /// overhead related to mitigations for reference invalidation.
  static constexpr bool TakesParamByValue = sizeof(T) <= 2 * sizeof(void *);

  /// Either const T& or T, depending on whether it's cheap enough to take
  /// parameters by value.
  using ValueParamT = std::conditional_t<TakesParamByValue, T, const T &>;

  SmallVectorTemplateBase(size_t Size) : SmallVectorTemplateCommon<T>(Size) {}

  // No need to do a destroy loop for POD's.
  static void destroy_range(T *, T *) {}

  /// Move the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template<typename It1, typename It2>
  static void uninitialized_move(It1 I, It1 E, It2 Dest) {
    // Just do a copy.
    uninitialized_copy(I, E, Dest);
  }

  /// Copy the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template<typename It1, typename It2>
  static void uninitialized_copy(It1 I, It1 E, It2 Dest) {
    // Arbitrary iterator types; just use the basic implementation.
    std::uninitialized_copy(I, E, Dest);
  }

  /// Copy the range [I, E) onto the uninitialized memory
  /// starting with "Dest", constructing elements into it as needed.
  template <typename T1, typename T2>
  static void uninitialized_copy(
      T1 *I, T1 *E, T2 *Dest,
      std::enable_if_t<std::is_same<std::remove_const_t<T1>, T2>::value> * =
          nullptr) {
    // Use memcpy for PODs iterated by pointers (which includes SmallVector
    // iterators): std::uninitialized_copy optimizes to memmove, but we can
    // use memcpy here. Note that I and E are iterators and thus might be
    // invalid for memcpy if they are equal.
    if (I != E)
      memcpy(reinterpret_cast<void *>(Dest), I, (E - I) * sizeof(T));
  }

  /// Double the size of the allocated memory, guaranteeing space for at
  /// least one more element or MinSize if specified.
  void grow(size_t MinSize = 0) { this->grow_pod(MinSize, sizeof(T)); }

  /// Reserve enough space to add one element, and return the updated element
  /// pointer in case it was a reference to the storage.
  const T *reserveForParamAndGetAddress(const T &Elt, size_t N = 1) {
    return this->reserveForParamAndGetAddressImpl(this, Elt, N);
  }

  /// Reserve enough space to add one element, and return the updated element
  /// pointer in case it was a reference to the storage.
  T *reserveForParamAndGetAddress(T &Elt, size_t N = 1) {
    return const_cast<T *>(
        this->reserveForParamAndGetAddressImpl(this, Elt, N));
  }

  /// Copy \p V or return a reference, depending on \a ValueParamT.
  static ValueParamT forward_value_param(ValueParamT V) { return V; }

  void growAndAssign(size_t NumElts, T Elt) {
    // Elt has been copied in case it's an internal reference, side-stepping
    // reference invalidation problems without losing the realloc optimization.
    this->set_size(0);
    this->grow(NumElts);
    std::uninitialized_fill_n(this->begin(), NumElts, Elt);
    this->set_size(NumElts);
  }

  template <typename... ArgTypes> T &growAndEmplaceBack(ArgTypes &&... Args) {
    // Use push_back with a copy in case Args has an internal reference,
    // side-stepping reference invalidation problems without losing the realloc
    // optimization.
    push_back(T(std::forward<ArgTypes>(Args)...));
    return this->back();
  }

public:
  void push_back(ValueParamT Elt) {
    const T *EltPtr = reserveForParamAndGetAddress(Elt);
    memcpy(reinterpret_cast<void *>(this->end()), EltPtr, sizeof(T));
    this->set_size(this->size() + 1);
  }

  void pop_back() { this->set_size(this->size() - 1); }
};

/// This class consists of common code factored out of the SmallVector class to
/// reduce code duplication based on the SmallVector 'N' template parameter.
template <typename T>
class SmallVectorImpl : public SmallVectorTemplateBase<T> {
  using SuperClass = SmallVectorTemplateBase<T>;

public:
  using iterator = typename SuperClass::iterator;
  using const_iterator = typename SuperClass::const_iterator;
  using reference = typename SuperClass::reference;
  using size_type = typename SuperClass::size_type;

protected:
  using SmallVectorTemplateBase<T>::TakesParamByValue;
  using ValueParamT = typename SuperClass::ValueParamT;

  // Default ctor - Initialize to empty.
  explicit SmallVectorImpl(unsigned N)
      : SmallVectorTemplateBase<T>(N) {}

  void assignRemote(SmallVectorImpl &&RHS) {
    this->destroy_range(this->begin(), this->end());
    if (!this->isSmall())
      free(this->begin());
    this->BeginX = RHS.BeginX;
    this->Size = RHS.Size;
    this->Capacity = RHS.Capacity;
    RHS.resetToSmall();
  }

  ~SmallVectorImpl() {
    // Subclass has already destructed this vector's elements.
    // If this wasn't grown from the inline copy, deallocate the old space.
    if (!this->isSmall())
      free(this->begin());
  }

public:
  SmallVectorImpl(const SmallVectorImpl &) = delete;

  void clear() {
    this->destroy_range(this->begin(), this->end());
    this->Size = 0;
  }

private:
  // Make set_size() private to avoid misuse in subclasses.
  using SuperClass::set_size;

  template <bool ForOverwrite> void resizeImpl(size_type N) {
    if (N == this->size())
      return;

    if (N < this->size()) {
      this->truncate(N);
      return;
    }

    this->reserve(N);
    for (auto I = this->end(), E = this->begin() + N; I != E; ++I)
      if (ForOverwrite)
        new (&*I) T;
      else
        new (&*I) T();
    this->set_size(N);
  }

public:
  void resize(size_type N) { resizeImpl<false>(N); }

  /// Like resize, but \ref T is POD, the new values won't be initialized.
  void resize_for_overwrite(size_type N) { resizeImpl<true>(N); }

  /// Like resize, but requires that \p N is less than \a size().
  void truncate(size_type N) {
    assert(this->size() >= N && "Cannot increase size with truncate");
    this->destroy_range(this->begin() + N, this->end());
    this->set_size(N);
  }

  void resize(size_type N, ValueParamT NV) {
    if (N == this->size())
      return;

    if (N < this->size()) {
      this->truncate(N);
      return;
    }

    // N > this->size(). Defer to append.
    this->append(N - this->size(), NV);
  }

  void reserve(size_type N) {
    if (this->capacity() < N)
      this->grow(N);
  }

  void pop_back_n(size_type NumItems) {
    assert(this->size() >= NumItems);
    truncate(this->size() - NumItems);
  }

  [[nodiscard]] T pop_back_val() {
    T Result = ::std::move(this->back());
    this->pop_back();
    return Result;
  }

  void swap(SmallVectorImpl &RHS);

  /// Add the specified range to the end of the SmallVector.
  template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
  void append(ItTy in_start, ItTy in_end) {
    this->assertSafeToAddRange(in_start, in_end);
    size_type NumInputs = std::distance(in_start, in_end);
    this->reserve(this->size() + NumInputs);
    this->uninitialized_copy(in_start, in_end, this->end());
    this->set_size(this->size() + NumInputs);
  }

  /// Append \p NumInputs copies of \p Elt to the end.
  void append(size_type NumInputs, ValueParamT Elt) {
    const T *EltPtr = this->reserveForParamAndGetAddress(Elt, NumInputs);
    std::uninitialized_fill_n(this->end(), NumInputs, *EltPtr);
    this->set_size(this->size() + NumInputs);
  }

  void append(std::initializer_list<T> IL) {
    append(IL.begin(), IL.end());
  }

  void append(const SmallVectorImpl &RHS) { append(RHS.begin(), RHS.end()); }

  void assign(size_type NumElts, ValueParamT Elt) {
    // Note that Elt could be an internal reference.
    if (NumElts > this->capacity()) {
      this->growAndAssign(NumElts, Elt);
      return;
    }

    // Assign over existing elements.
    std::fill_n(this->begin(), std::min(NumElts, this->size()), Elt);
    if (NumElts > this->size())
      std::uninitialized_fill_n(this->end(), NumElts - this->size(), Elt);
    else if (NumElts < this->size())
      this->destroy_range(this->begin() + NumElts, this->end());
    this->set_size(NumElts);
  }

  // FIXME: Consider assigning over existing elements, rather than clearing &
  // re-initializing them - for all assign(...) variants.

  template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
  void assign(ItTy in_start, ItTy in_end) {
    this->assertSafeToReferenceAfterClear(in_start, in_end);
    clear();
    append(in_start, in_end);
  }

  void assign(std::initializer_list<T> IL) {
    clear();
    append(IL);
  }

  void assign(const SmallVectorImpl &RHS) { assign(RHS.begin(), RHS.end()); }

  iterator erase(const_iterator CI) {
    // Just cast away constness because this is a non-const member function.
    iterator I = const_cast<iterator>(CI);

    assert(this->isReferenceToStorage(CI) && "Iterator to erase is out of bounds.");

    iterator N = I;
    // Shift all elts down one.
    std::move(I+1, this->end(), I);
    // Drop the last elt.
    this->pop_back();
    return(N);
  }

  iterator erase(const_iterator CS, const_iterator CE) {
    // Just cast away constness because this is a non-const member function.
    iterator S = const_cast<iterator>(CS);
    iterator E = const_cast<iterator>(CE);

    assert(this->isRangeInStorage(S, E) && "Range to erase is out of bounds.");

    iterator N = S;
    // Shift all elts down.
    iterator I = std::move(E, this->end(), S);
    // Drop the last elts.
    this->destroy_range(I, this->end());
    this->set_size(I - this->begin());
    return(N);
  }

private:
  template <class ArgType> iterator insert_one_impl(iterator I, ArgType &&Elt) {
    // Callers ensure that ArgType is derived from T.
    static_assert(
        std::is_same<std::remove_const_t<std::remove_reference_t<ArgType>>,
                     T>::value,
        "ArgType must be derived from T!");

    if (I == this->end()) {  // Important special case for empty vector.
      this->push_back(::std::forward<ArgType>(Elt));
      return this->end()-1;
    }

    assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

    // Grow if necessary.
    size_t Index = I - this->begin();
    std::remove_reference_t<ArgType> *EltPtr =
        this->reserveForParamAndGetAddress(Elt);
    I = this->begin() + Index;

    ::new ((void*) this->end()) T(::std::move(this->back()));
    // Push everything else over.
    std::move_backward(I, this->end()-1, this->end());
    this->set_size(this->size() + 1);

    // If we just moved the element we're inserting, be sure to update
    // the reference (never happens if TakesParamByValue).
    static_assert(!TakesParamByValue || std::is_same<ArgType, T>::value,
                  "ArgType must be 'T' when taking by value!");
    if (!TakesParamByValue && this->isReferenceToRange(EltPtr, I, this->end()))
      ++EltPtr;

    *I = ::std::forward<ArgType>(*EltPtr);
    return I;
  }

public:
  iterator insert(iterator I, T &&Elt) {
    return insert_one_impl(I, this->forward_value_param(std::move(Elt)));
  }

  iterator insert(iterator I, const T &Elt) {
    return insert_one_impl(I, this->forward_value_param(Elt));
  }

  iterator insert(iterator I, size_type NumToInsert, ValueParamT Elt) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) {  // Important special case for empty vector.
      append(NumToInsert, Elt);
      return this->begin()+InsertElt;
    }

    assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

    // Ensure there is enough space, and get the (maybe updated) address of
    // Elt.
    const T *EltPtr = this->reserveForParamAndGetAddress(Elt, NumToInsert);

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(std::move_iterator<iterator>(this->end() - NumToInsert),
             std::move_iterator<iterator>(this->end()));

      // Copy the existing elements that get replaced.
      std::move_backward(I, OldEnd-NumToInsert, OldEnd);

      // If we just moved the element we're inserting, be sure to update
      // the reference (never happens if TakesParamByValue).
      if (!TakesParamByValue && I <= EltPtr && EltPtr < this->end())
        EltPtr += NumToInsert;

      std::fill_n(I, NumToInsert, *EltPtr);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Move over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->set_size(this->size() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_move(I, OldEnd, this->end()-NumOverwritten);

    // If we just moved the element we're inserting, be sure to update
    // the reference (never happens if TakesParamByValue).
    if (!TakesParamByValue && I <= EltPtr && EltPtr < this->end())
      EltPtr += NumToInsert;

    // Replace the overwritten part.
    std::fill_n(I, NumOverwritten, *EltPtr);

    // Insert the non-overwritten middle part.
    std::uninitialized_fill_n(OldEnd, NumToInsert - NumOverwritten, *EltPtr);
    return I;
  }

  template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
  iterator insert(iterator I, ItTy From, ItTy To) {
    // Convert iterator to elt# to avoid invalidating iterator when we reserve()
    size_t InsertElt = I - this->begin();

    if (I == this->end()) {  // Important special case for empty vector.
      append(From, To);
      return this->begin()+InsertElt;
    }

    assert(this->isReferenceToStorage(I) && "Insertion iterator is out of bounds.");

    // Check that the reserve that follows doesn't invalidate the iterators.
    this->assertSafeToAddRange(From, To);

    size_t NumToInsert = std::distance(From, To);

    // Ensure there is enough space.
    reserve(this->size() + NumToInsert);

    // Uninvalidate the iterator.
    I = this->begin()+InsertElt;

    // If there are more elements between the insertion point and the end of the
    // range than there are being inserted, we can use a simple approach to
    // insertion.  Since we already reserved space, we know that this won't
    // reallocate the vector.
    if (size_t(this->end()-I) >= NumToInsert) {
      T *OldEnd = this->end();
      append(std::move_iterator<iterator>(this->end() - NumToInsert),
             std::move_iterator<iterator>(this->end()));

      // Copy the existing elements that get replaced.
      std::move_backward(I, OldEnd-NumToInsert, OldEnd);

      std::copy(From, To, I);
      return I;
    }

    // Otherwise, we're inserting more elements than exist already, and we're
    // not inserting at the end.

    // Move over the elements that we're about to overwrite.
    T *OldEnd = this->end();
    this->set_size(this->size() + NumToInsert);
    size_t NumOverwritten = OldEnd-I;
    this->uninitialized_move(I, OldEnd, this->end()-NumOverwritten);

    // Replace the overwritten part.
    for (T *J = I; NumOverwritten > 0; --NumOverwritten) {
      *J = *From;
      ++J; ++From;
    }

    // Insert the non-overwritten middle part.
    this->uninitialized_copy(From, To, OldEnd);
    return I;
  }

  void insert(iterator I, std::initializer_list<T> IL) {
    insert(I, IL.begin(), IL.end());
  }

  template <typename... ArgTypes> reference emplace_back(ArgTypes &&... Args) {
    if (LLVM_UNLIKELY(this->size() >= this->capacity()))
      return this->growAndEmplaceBack(std::forward<ArgTypes>(Args)...);

    ::new ((void *)this->end()) T(std::forward<ArgTypes>(Args)...);
    this->set_size(this->size() + 1);
    return this->back();
  }

  SmallVectorImpl &operator=(const SmallVectorImpl &RHS);

  SmallVectorImpl &operator=(SmallVectorImpl &&RHS);

  bool operator==(const SmallVectorImpl &RHS) const {
    if (this->size() != RHS.size()) return false;
    return std::equal(this->begin(), this->end(), RHS.begin());
  }
  bool operator!=(const SmallVectorImpl &RHS) const {
    return !(*this == RHS);
  }

  bool operator<(const SmallVectorImpl &RHS) const {
    return std::lexicographical_compare(this->begin(), this->end(),
                                        RHS.begin(), RHS.end());
  }
  bool operator>(const SmallVectorImpl &RHS) const { return RHS < *this; }
  bool operator<=(const SmallVectorImpl &RHS) const { return !(*this > RHS); }
  bool operator>=(const SmallVectorImpl &RHS) const { return !(*this < RHS); }
};

template <typename T>
void SmallVectorImpl<T>::swap(SmallVectorImpl<T> &RHS) {
  if (this == &RHS) return;

  // We can only avoid copying elements if neither vector is small.
  if (!this->isSmall() && !RHS.isSmall()) {
    std::swap(this->BeginX, RHS.BeginX);
    std::swap(this->Size, RHS.Size);
    std::swap(this->Capacity, RHS.Capacity);
    return;
  }
  this->reserve(RHS.size());
  RHS.reserve(this->size());

  // Swap the shared elements.
  size_t NumShared = this->size();
  if (NumShared > RHS.size()) NumShared = RHS.size();
  for (size_type i = 0; i != NumShared; ++i)
    std::swap((*this)[i], RHS[i]);

  // Copy over the extra elts.
  if (this->size() > RHS.size()) {
    size_t EltDiff = this->size() - RHS.size();
    this->uninitialized_copy(this->begin()+NumShared, this->end(), RHS.end());
    RHS.set_size(RHS.size() + EltDiff);
    this->destroy_range(this->begin()+NumShared, this->end());
    this->set_size(NumShared);
  } else if (RHS.size() > this->size()) {
    size_t EltDiff = RHS.size() - this->size();
    this->uninitialized_copy(RHS.begin()+NumShared, RHS.end(), this->end());
    this->set_size(this->size() + EltDiff);
    this->destroy_range(RHS.begin()+NumShared, RHS.end());
    RHS.set_size(NumShared);
  }
}

template <typename T>
SmallVectorImpl<T> &SmallVectorImpl<T>::
  operator=(const SmallVectorImpl<T> &RHS) {
  // Avoid self-assignment.
  if (this == &RHS) return *this;

  // If we already have sufficient space, assign the common elements, then
  // destroy any excess.
  size_t RHSSize = RHS.size();
  size_t CurSize = this->size();
  if (CurSize >= RHSSize) {
    // Assign common elements.
    iterator NewEnd;
    if (RHSSize)
      NewEnd = std::copy(RHS.begin(), RHS.begin()+RHSSize, this->begin());
    else
      NewEnd = this->begin();

    // Destroy excess elements.
    this->destroy_range(NewEnd, this->end());

    // Trim.
    this->set_size(RHSSize);
    return *this;
  }

  // If we have to grow to have enough elements, destroy the current elements.
  // This allows us to avoid copying them during the grow.
  // FIXME: don't do this if they're efficiently moveable.
  if (this->capacity() < RHSSize) {
    // Destroy current elements.
    this->clear();
    CurSize = 0;
    this->grow(RHSSize);
  } else if (CurSize) {
    // Otherwise, use assignment for the already-constructed elements.
    std::copy(RHS.begin(), RHS.begin()+CurSize, this->begin());
  }

  // Copy construct the new elements in place.
  this->uninitialized_copy(RHS.begin()+CurSize, RHS.end(),
                           this->begin()+CurSize);

  // Set end.
  this->set_size(RHSSize);
  return *this;
}

template <typename T>
SmallVectorImpl<T> &SmallVectorImpl<T>::operator=(SmallVectorImpl<T> &&RHS) {
  // Avoid self-assignment.
  if (this == &RHS) return *this;

  // If the RHS isn't small, clear this vector and then steal its buffer.
  if (!RHS.isSmall()) {
    this->assignRemote(std::move(RHS));
    return *this;
  }

  // If we already have sufficient space, assign the common elements, then
  // destroy any excess.
  size_t RHSSize = RHS.size();
  size_t CurSize = this->size();
  if (CurSize >= RHSSize) {
    // Assign common elements.
    iterator NewEnd = this->begin();
    if (RHSSize)
      NewEnd = std::move(RHS.begin(), RHS.end(), NewEnd);

    // Destroy excess elements and trim the bounds.
    this->destroy_range(NewEnd, this->end());
    this->set_size(RHSSize);

    // Clear the RHS.
    RHS.clear();

    return *this;
  }

  // If we have to grow to have enough elements, destroy the current elements.
  // This allows us to avoid copying them during the grow.
  // FIXME: this may not actually make any sense if we can efficiently move
  // elements.
  if (this->capacity() < RHSSize) {
    // Destroy current elements.
    this->clear();
    CurSize = 0;
    this->grow(RHSSize);
  } else if (CurSize) {
    // Otherwise, use assignment for the already-constructed elements.
    std::move(RHS.begin(), RHS.begin()+CurSize, this->begin());
  }

  // Move-construct the new elements in place.
  this->uninitialized_move(RHS.begin()+CurSize, RHS.end(),
                           this->begin()+CurSize);

  // Set end.
  this->set_size(RHSSize);

  RHS.clear();
  return *this;
}

/// Storage for the SmallVector elements.  This is specialized for the N=0 case
/// to avoid allocating unnecessary storage.
template <typename T, unsigned N>
struct SmallVectorStorage {
  alignas(T) char InlineElts[N * sizeof(T)];
};

/// We need the storage to be properly aligned even for small-size of 0 so that
/// the pointer math in \a SmallVectorTemplateCommon::getFirstEl() is
/// well-defined.
template <typename T> struct alignas(T) SmallVectorStorage<T, 0> {};

/// Forward declaration of SmallVector so that
/// calculateSmallVectorDefaultInlinedElements can reference
/// `sizeof(SmallVector<T, 0>)`.
template <typename T, unsigned N> class LLVM_GSL_OWNER SmallVector;

/// Helper class for calculating the default number of inline elements for
/// `SmallVector<T>`.
///
/// This should be migrated to a constexpr function when our minimum
/// compiler support is enough for multi-statement constexpr functions.
template <typename T> struct CalculateSmallVectorDefaultInlinedElements {
  // Parameter controlling the default number of inlined elements
  // for `SmallVector<T>`.
  //
  // The default number of inlined elements ensures that
  // 1. There is at least one inlined element.
  // 2. `sizeof(SmallVector<T>) <= kPreferredSmallVectorSizeof` unless
  // it contradicts 1.
  static constexpr size_t kPreferredSmallVectorSizeof = 64;

  // static_assert that sizeof(T) is not "too big".
  //
  // Because our policy guarantees at least one inlined element, it is possible
  // for an arbitrarily large inlined element to allocate an arbitrarily large
  // amount of inline storage. We generally consider it an antipattern for a
  // SmallVector to allocate an excessive amount of inline storage, so we want
  // to call attention to these cases and make sure that users are making an
  // intentional decision if they request a lot of inline storage.
  //
  // We want this assertion to trigger in pathological cases, but otherwise
  // not be too easy to hit. To accomplish that, the cutoff is actually somewhat
  // larger than kPreferredSmallVectorSizeof (otherwise,
  // `SmallVector<SmallVector<T>>` would be one easy way to trip it, and that
  // pattern seems useful in practice).
  //
  // One wrinkle is that this assertion is in theory non-portable, since
  // sizeof(T) is in general platform-dependent. However, we don't expect this
  // to be much of an issue, because most LLVM development happens on 64-bit
  // hosts, and therefore sizeof(T) is expected to *decrease* when compiled for
  // 32-bit hosts, dodging the issue. The reverse situation, where development
  // happens on a 32-bit host and then fails due to sizeof(T) *increasing* on a
  // 64-bit host, is expected to be very rare.
  static_assert(
      sizeof(T) <= 256,
      "You are trying to use a default number of inlined elements for "
      "`SmallVector<T>` but `sizeof(T)` is really big! Please use an "
      "explicit number of inlined elements with `SmallVector<T, N>` to make "
      "sure you really want that much inline storage.");

  // Discount the size of the header itself when calculating the maximum inline
  // bytes.
  static constexpr size_t PreferredInlineBytes =
      kPreferredSmallVectorSizeof - sizeof(SmallVector<T, 0>);
  static constexpr size_t NumElementsThatFit = PreferredInlineBytes / sizeof(T);
  static constexpr size_t value =
      NumElementsThatFit == 0 ? 1 : NumElementsThatFit;
};

/// This is a 'vector' (really, a variable-sized array), optimized
/// for the case when the array is small.  It contains some number of elements
/// in-place, which allows it to avoid heap allocation when the actual number of
/// elements is below that threshold.  This allows normal "small" cases to be
/// fast without losing generality for large inputs.
///
/// \note
/// In the absence of a well-motivated choice for the number of inlined
/// elements \p N, it is recommended to use \c SmallVector<T> (that is,
/// omitting the \p N). This will choose a default number of inlined elements
/// reasonable for allocation on the stack (for example, trying to keep \c
/// sizeof(SmallVector<T>) around 64 bytes).
///
/// \warning This does not attempt to be exception safe.
///
/// \see https://llvm.org/docs/ProgrammersManual.html#llvm-adt-smallvector-h
template <typename T,
          unsigned N = CalculateSmallVectorDefaultInlinedElements<T>::value>
class LLVM_GSL_OWNER SmallVector : public SmallVectorImpl<T>,
                                   SmallVectorStorage<T, N> {
public:
  SmallVector() : SmallVectorImpl<T>(N) {}

  ~SmallVector() {
    // Destroy the constructed elements in the vector.
    this->destroy_range(this->begin(), this->end());
  }

  explicit SmallVector(size_t Size)
    : SmallVectorImpl<T>(N) {
    this->resize(Size);
  }

  SmallVector(size_t Size, const T &Value)
    : SmallVectorImpl<T>(N) {
    this->assign(Size, Value);
  }

  template <typename ItTy, typename = EnableIfConvertibleToInputIterator<ItTy>>
  SmallVector(ItTy S, ItTy E) : SmallVectorImpl<T>(N) {
    this->append(S, E);
  }

  template <typename RangeTy>
  explicit SmallVector(const iterator_range<RangeTy> &R)
      : SmallVectorImpl<T>(N) {
    this->append(R.begin(), R.end());
  }

  SmallVector(std::initializer_list<T> IL) : SmallVectorImpl<T>(N) {
    this->append(IL);
  }

  template <typename U,
            typename = std::enable_if_t<std::is_convertible<U, T>::value>>
  explicit SmallVector(ArrayRef<U> A) : SmallVectorImpl<T>(N) {
    this->append(A.begin(), A.end());
  }

  SmallVector(const SmallVector &RHS) : SmallVectorImpl<T>(N) {
    if (!RHS.empty())
      SmallVectorImpl<T>::operator=(RHS);
  }

  SmallVector &operator=(const SmallVector &RHS) {
    SmallVectorImpl<T>::operator=(RHS);
    return *this;
  }

  SmallVector(SmallVector &&RHS) : SmallVectorImpl<T>(N) {
    if (!RHS.empty())
      SmallVectorImpl<T>::operator=(::std::move(RHS));
  }

  SmallVector(SmallVectorImpl<T> &&RHS) : SmallVectorImpl<T>(N) {
    if (!RHS.empty())
      SmallVectorImpl<T>::operator=(::std::move(RHS));
  }

  SmallVector &operator=(SmallVector &&RHS) {
    if (N) {
      SmallVectorImpl<T>::operator=(::std::move(RHS));
      return *this;
    }
    // SmallVectorImpl<T>::operator= does not leverage N==0. Optimize the
    // case.
    if (this == &RHS)
      return *this;
    if (RHS.empty()) {
      this->destroy_range(this->begin(), this->end());
      this->Size = 0;
    } else {
      this->assignRemote(std::move(RHS));
    }
    return *this;
  }

  SmallVector &operator=(SmallVectorImpl<T> &&RHS) {
    SmallVectorImpl<T>::operator=(::std::move(RHS));
    return *this;
  }

  SmallVector &operator=(std::initializer_list<T> IL) {
    this->assign(IL);
    return *this;
  }
};

template <typename T, unsigned N>
inline size_t capacity_in_bytes(const SmallVector<T, N> &X) {
  return X.capacity_in_bytes();
}

template <typename RangeType>
using ValueTypeFromRangeType =
    std::remove_const_t<std::remove_reference_t<decltype(*std::begin(
        std::declval<RangeType &>()))>>;

/// Given a range of type R, iterate the entire range and return a
/// SmallVector with elements of the vector.  This is useful, for example,
/// when you want to iterate a range and then sort the results.
template <unsigned Size, typename R>
SmallVector<ValueTypeFromRangeType<R>, Size> to_vector(R &&Range) {
  return {std::begin(Range), std::end(Range)};
}
template <typename R>
SmallVector<ValueTypeFromRangeType<R>> to_vector(R &&Range) {
  return {std::begin(Range), std::end(Range)};
}

template <typename Out, unsigned Size, typename R>
SmallVector<Out, Size> to_vector_of(R &&Range) {
  return {std::begin(Range), std::end(Range)};
}

template <typename Out, typename R> SmallVector<Out> to_vector_of(R &&Range) {
  return {std::begin(Range), std::end(Range)};
}

// Explicit instantiations
extern template class llvm::SmallVectorBase<uint32_t>;
#if SIZE_MAX > UINT32_MAX
extern template class llvm::SmallVectorBase<uint64_t>;
#endif

} // end namespace llvm

namespace std {

  /// Implement std::swap in terms of SmallVector swap.
  template<typename T>
  inline void
  swap(llvm::SmallVectorImpl<T> &LHS, llvm::SmallVectorImpl<T> &RHS) {
    LHS.swap(RHS);
  }

  /// Implement std::swap in terms of SmallVector swap.
  template<typename T, unsigned N>
  inline void
  swap(llvm::SmallVector<T, N> &LHS, llvm::SmallVector<T, N> &RHS) {
    LHS.swap(RHS);
  }

} // end namespace std

#endif // LLVM_ADT_SMALLVECTOR_H
