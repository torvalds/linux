//==- llvm/Support/ArrayRecycler.h - Recycling of Arrays ---------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ArrayRecycler class template which can recycle small
// arrays allocated from one of the allocators in Allocator.h
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ARRAYRECYCLER_H
#define LLVM_SUPPORT_ARRAYRECYCLER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MathExtras.h"

namespace llvm {

/// Recycle small arrays allocated from a BumpPtrAllocator.
///
/// Arrays are allocated in a small number of fixed sizes. For each supported
/// array size, the ArrayRecycler keeps a free list of available arrays.
///
template <class T, size_t Align = alignof(T)> class ArrayRecycler {
  // The free list for a given array size is a simple singly linked list.
  // We can't use iplist or Recycler here since those classes can't be copied.
  struct FreeList {
    FreeList *Next;
  };

  static_assert(Align >= alignof(FreeList), "Object underaligned");
  static_assert(sizeof(T) >= sizeof(FreeList), "Objects are too small");

  // Keep a free list for each array size.
  SmallVector<FreeList*, 8> Bucket;

  // Remove an entry from the free list in Bucket[Idx] and return it.
  // Return NULL if no entries are available.
  T *pop(unsigned Idx) {
    if (Idx >= Bucket.size())
      return nullptr;
    FreeList *Entry = Bucket[Idx];
    if (!Entry)
      return nullptr;
    __asan_unpoison_memory_region(Entry, Capacity::get(Idx).getSize());
    Bucket[Idx] = Entry->Next;
    __msan_allocated_memory(Entry, Capacity::get(Idx).getSize());
    return reinterpret_cast<T*>(Entry);
  }

  // Add an entry to the free list at Bucket[Idx].
  void push(unsigned Idx, T *Ptr) {
    assert(Ptr && "Cannot recycle NULL pointer");
    FreeList *Entry = reinterpret_cast<FreeList*>(Ptr);
    if (Idx >= Bucket.size())
      Bucket.resize(size_t(Idx) + 1);
    Entry->Next = Bucket[Idx];
    Bucket[Idx] = Entry;
    __asan_poison_memory_region(Ptr, Capacity::get(Idx).getSize());
  }

public:
  /// The size of an allocated array is represented by a Capacity instance.
  ///
  /// This class is much smaller than a size_t, and it provides methods to work
  /// with the set of legal array capacities.
  class Capacity {
    uint8_t Index;
    explicit Capacity(uint8_t idx) : Index(idx) {}

  public:
    Capacity() : Index(0) {}

    /// Get the capacity of an array that can hold at least N elements.
    static Capacity get(size_t N) {
      return Capacity(N ? Log2_64_Ceil(N) : 0);
    }

    /// Get the number of elements in an array with this capacity.
    size_t getSize() const { return size_t(1u) << Index; }

    /// Get the bucket number for this capacity.
    unsigned getBucket() const { return Index; }

    /// Get the next larger capacity. Large capacities grow exponentially, so
    /// this function can be used to reallocate incrementally growing vectors
    /// in amortized linear time.
    Capacity getNext() const { return Capacity(Index + 1); }
  };

  ~ArrayRecycler() {
    // The client should always call clear() so recycled arrays can be returned
    // to the allocator.
    assert(Bucket.empty() && "Non-empty ArrayRecycler deleted!");
  }

  /// Release all the tracked allocations to the allocator. The recycler must
  /// be free of any tracked allocations before being deleted.
  template<class AllocatorType>
  void clear(AllocatorType &Allocator) {
    for (; !Bucket.empty(); Bucket.pop_back())
      while (T *Ptr = pop(Bucket.size() - 1))
        Allocator.Deallocate(Ptr);
  }

  /// Special case for BumpPtrAllocator which has an empty Deallocate()
  /// function.
  ///
  /// There is no need to traverse the free lists, pulling all the objects into
  /// cache.
  void clear(BumpPtrAllocator&) {
    Bucket.clear();
  }

  /// Allocate an array of at least the requested capacity.
  ///
  /// Return an existing recycled array, or allocate one from Allocator if
  /// none are available for recycling.
  ///
  template<class AllocatorType>
  T *allocate(Capacity Cap, AllocatorType &Allocator) {
    // Try to recycle an existing array.
    if (T *Ptr = pop(Cap.getBucket()))
      return Ptr;
    // Nope, get more memory.
    return static_cast<T*>(Allocator.Allocate(sizeof(T)*Cap.getSize(), Align));
  }

  /// Deallocate an array with the specified Capacity.
  ///
  /// Cap must be the same capacity that was given to allocate().
  ///
  void deallocate(Capacity Cap, T *Ptr) {
    push(Cap.getBucket(), Ptr);
  }
};

} // end llvm namespace

#endif
