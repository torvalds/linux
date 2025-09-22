//===- Allocator.h - Simple memory allocation abstraction -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the BumpPtrAllocator interface. BumpPtrAllocator conforms
/// to the LLVM "Allocator" concept and is similar to MallocAllocator, but
/// objects cannot be deallocated. Their lifetime is tied to the lifetime of the
/// allocator.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ALLOCATOR_H
#define LLVM_SUPPORT_ALLOCATOR_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/AllocatorBase.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

namespace llvm {

namespace detail {

// We call out to an external function to actually print the message as the
// printing code uses Allocator.h in its implementation.
void printBumpPtrAllocatorStats(unsigned NumSlabs, size_t BytesAllocated,
                                size_t TotalMemory);

} // end namespace detail

/// Allocate memory in an ever growing pool, as if by bump-pointer.
///
/// This isn't strictly a bump-pointer allocator as it uses backing slabs of
/// memory rather than relying on a boundless contiguous heap. However, it has
/// bump-pointer semantics in that it is a monotonically growing pool of memory
/// where every allocation is found by merely allocating the next N bytes in
/// the slab, or the next N bytes in the next slab.
///
/// Note that this also has a threshold for forcing allocations above a certain
/// size into their own slab.
///
/// The BumpPtrAllocatorImpl template defaults to using a MallocAllocator
/// object, which wraps malloc, to allocate memory, but it can be changed to
/// use a custom allocator.
///
/// The GrowthDelay specifies after how many allocated slabs the allocator
/// increases the size of the slabs.
template <typename AllocatorT = MallocAllocator, size_t SlabSize = 4096,
          size_t SizeThreshold = SlabSize, size_t GrowthDelay = 128>
class BumpPtrAllocatorImpl
    : public AllocatorBase<BumpPtrAllocatorImpl<AllocatorT, SlabSize,
                                                SizeThreshold, GrowthDelay>>,
      private detail::AllocatorHolder<AllocatorT> {
  using AllocTy = detail::AllocatorHolder<AllocatorT>;

public:
  static_assert(SizeThreshold <= SlabSize,
                "The SizeThreshold must be at most the SlabSize to ensure "
                "that objects larger than a slab go into their own memory "
                "allocation.");
  static_assert(GrowthDelay > 0,
                "GrowthDelay must be at least 1 which already increases the"
                "slab size after each allocated slab.");

  BumpPtrAllocatorImpl() = default;

  template <typename T>
  BumpPtrAllocatorImpl(T &&Allocator)
      : AllocTy(std::forward<T &&>(Allocator)) {}

  // Manually implement a move constructor as we must clear the old allocator's
  // slabs as a matter of correctness.
  BumpPtrAllocatorImpl(BumpPtrAllocatorImpl &&Old)
      : AllocTy(std::move(Old.getAllocator())), CurPtr(Old.CurPtr),
        End(Old.End), Slabs(std::move(Old.Slabs)),
        CustomSizedSlabs(std::move(Old.CustomSizedSlabs)),
        BytesAllocated(Old.BytesAllocated), RedZoneSize(Old.RedZoneSize) {
    Old.CurPtr = Old.End = nullptr;
    Old.BytesAllocated = 0;
    Old.Slabs.clear();
    Old.CustomSizedSlabs.clear();
  }

  ~BumpPtrAllocatorImpl() {
    DeallocateSlabs(Slabs.begin(), Slabs.end());
    DeallocateCustomSizedSlabs();
  }

  BumpPtrAllocatorImpl &operator=(BumpPtrAllocatorImpl &&RHS) {
    DeallocateSlabs(Slabs.begin(), Slabs.end());
    DeallocateCustomSizedSlabs();

    CurPtr = RHS.CurPtr;
    End = RHS.End;
    BytesAllocated = RHS.BytesAllocated;
    RedZoneSize = RHS.RedZoneSize;
    Slabs = std::move(RHS.Slabs);
    CustomSizedSlabs = std::move(RHS.CustomSizedSlabs);
    AllocTy::operator=(std::move(RHS.getAllocator()));

    RHS.CurPtr = RHS.End = nullptr;
    RHS.BytesAllocated = 0;
    RHS.Slabs.clear();
    RHS.CustomSizedSlabs.clear();
    return *this;
  }

  /// Deallocate all but the current slab and reset the current pointer
  /// to the beginning of it, freeing all memory allocated so far.
  void Reset() {
    // Deallocate all but the first slab, and deallocate all custom-sized slabs.
    DeallocateCustomSizedSlabs();
    CustomSizedSlabs.clear();

    if (Slabs.empty())
      return;

    // Reset the state.
    BytesAllocated = 0;
    CurPtr = (char *)Slabs.front();
    End = CurPtr + SlabSize;

    __asan_poison_memory_region(*Slabs.begin(), computeSlabSize(0));
    DeallocateSlabs(std::next(Slabs.begin()), Slabs.end());
    Slabs.erase(std::next(Slabs.begin()), Slabs.end());
  }

  /// Allocate space at the specified alignment.
  // This method is *not* marked noalias, because
  // SpecificBumpPtrAllocator::DestroyAll() loops over all allocations, and
  // that loop is not based on the Allocate() return value.
  //
  // Allocate(0, N) is valid, it returns a non-null pointer (which should not
  // be dereferenced).
  LLVM_ATTRIBUTE_RETURNS_NONNULL void *Allocate(size_t Size, Align Alignment) {
    // Keep track of how many bytes we've allocated.
    BytesAllocated += Size;

    size_t Adjustment = offsetToAlignedAddr(CurPtr, Alignment);
    assert(Adjustment + Size >= Size && "Adjustment + Size must not overflow");

    size_t SizeToAllocate = Size;
#if LLVM_ADDRESS_SANITIZER_BUILD
    // Add trailing bytes as a "red zone" under ASan.
    SizeToAllocate += RedZoneSize;
#endif

    // Check if we have enough space.
    if (LLVM_LIKELY(Adjustment + SizeToAllocate <= size_t(End - CurPtr)
                    // We can't return nullptr even for a zero-sized allocation!
                    && CurPtr != nullptr)) {
      char *AlignedPtr = CurPtr + Adjustment;
      CurPtr = AlignedPtr + SizeToAllocate;
      // Update the allocation point of this memory block in MemorySanitizer.
      // Without this, MemorySanitizer messages for values originated from here
      // will point to the allocation of the entire slab.
      __msan_allocated_memory(AlignedPtr, Size);
      // Similarly, tell ASan about this space.
      __asan_unpoison_memory_region(AlignedPtr, Size);
      return AlignedPtr;
    }

    return AllocateSlow(Size, SizeToAllocate, Alignment);
  }

  LLVM_ATTRIBUTE_RETURNS_NONNULL LLVM_ATTRIBUTE_NOINLINE void *
  AllocateSlow(size_t Size, size_t SizeToAllocate, Align Alignment) {
    // If Size is really big, allocate a separate slab for it.
    size_t PaddedSize = SizeToAllocate + Alignment.value() - 1;
    if (PaddedSize > SizeThreshold) {
      void *NewSlab =
          this->getAllocator().Allocate(PaddedSize, alignof(std::max_align_t));
      // We own the new slab and don't want anyone reading anyting other than
      // pieces returned from this method.  So poison the whole slab.
      __asan_poison_memory_region(NewSlab, PaddedSize);
      CustomSizedSlabs.push_back(std::make_pair(NewSlab, PaddedSize));

      uintptr_t AlignedAddr = alignAddr(NewSlab, Alignment);
      assert(AlignedAddr + Size <= (uintptr_t)NewSlab + PaddedSize);
      char *AlignedPtr = (char*)AlignedAddr;
      __msan_allocated_memory(AlignedPtr, Size);
      __asan_unpoison_memory_region(AlignedPtr, Size);
      return AlignedPtr;
    }

    // Otherwise, start a new slab and try again.
    StartNewSlab();
    uintptr_t AlignedAddr = alignAddr(CurPtr, Alignment);
    assert(AlignedAddr + SizeToAllocate <= (uintptr_t)End &&
           "Unable to allocate memory!");
    char *AlignedPtr = (char*)AlignedAddr;
    CurPtr = AlignedPtr + SizeToAllocate;
    __msan_allocated_memory(AlignedPtr, Size);
    __asan_unpoison_memory_region(AlignedPtr, Size);
    return AlignedPtr;
  }

  inline LLVM_ATTRIBUTE_RETURNS_NONNULL void *
  Allocate(size_t Size, size_t Alignment) {
    assert(Alignment > 0 && "0-byte alignment is not allowed. Use 1 instead.");
    return Allocate(Size, Align(Alignment));
  }

  // Pull in base class overloads.
  using AllocatorBase<BumpPtrAllocatorImpl>::Allocate;

  // Bump pointer allocators are expected to never free their storage; and
  // clients expect pointers to remain valid for non-dereferencing uses even
  // after deallocation.
  void Deallocate(const void *Ptr, size_t Size, size_t /*Alignment*/) {
    __asan_poison_memory_region(Ptr, Size);
  }

  // Pull in base class overloads.
  using AllocatorBase<BumpPtrAllocatorImpl>::Deallocate;

  size_t GetNumSlabs() const { return Slabs.size() + CustomSizedSlabs.size(); }

  /// \return An index uniquely and reproducibly identifying
  /// an input pointer \p Ptr in the given allocator.
  /// The returned value is negative iff the object is inside a custom-size
  /// slab.
  /// Returns an empty optional if the pointer is not found in the allocator.
  std::optional<int64_t> identifyObject(const void *Ptr) {
    const char *P = static_cast<const char *>(Ptr);
    int64_t InSlabIdx = 0;
    for (size_t Idx = 0, E = Slabs.size(); Idx < E; Idx++) {
      const char *S = static_cast<const char *>(Slabs[Idx]);
      if (P >= S && P < S + computeSlabSize(Idx))
        return InSlabIdx + static_cast<int64_t>(P - S);
      InSlabIdx += static_cast<int64_t>(computeSlabSize(Idx));
    }

    // Use negative index to denote custom sized slabs.
    int64_t InCustomSizedSlabIdx = -1;
    for (size_t Idx = 0, E = CustomSizedSlabs.size(); Idx < E; Idx++) {
      const char *S = static_cast<const char *>(CustomSizedSlabs[Idx].first);
      size_t Size = CustomSizedSlabs[Idx].second;
      if (P >= S && P < S + Size)
        return InCustomSizedSlabIdx - static_cast<int64_t>(P - S);
      InCustomSizedSlabIdx -= static_cast<int64_t>(Size);
    }
    return std::nullopt;
  }

  /// A wrapper around identifyObject that additionally asserts that
  /// the object is indeed within the allocator.
  /// \return An index uniquely and reproducibly identifying
  /// an input pointer \p Ptr in the given allocator.
  int64_t identifyKnownObject(const void *Ptr) {
    std::optional<int64_t> Out = identifyObject(Ptr);
    assert(Out && "Wrong allocator used");
    return *Out;
  }

  /// A wrapper around identifyKnownObject. Accepts type information
  /// about the object and produces a smaller identifier by relying on
  /// the alignment information. Note that sub-classes may have different
  /// alignment, so the most base class should be passed as template parameter
  /// in order to obtain correct results. For that reason automatic template
  /// parameter deduction is disabled.
  /// \return An index uniquely and reproducibly identifying
  /// an input pointer \p Ptr in the given allocator. This identifier is
  /// different from the ones produced by identifyObject and
  /// identifyAlignedObject.
  template <typename T>
  int64_t identifyKnownAlignedObject(const void *Ptr) {
    int64_t Out = identifyKnownObject(Ptr);
    assert(Out % alignof(T) == 0 && "Wrong alignment information");
    return Out / alignof(T);
  }

  size_t getTotalMemory() const {
    size_t TotalMemory = 0;
    for (auto I = Slabs.begin(), E = Slabs.end(); I != E; ++I)
      TotalMemory += computeSlabSize(std::distance(Slabs.begin(), I));
    for (const auto &PtrAndSize : CustomSizedSlabs)
      TotalMemory += PtrAndSize.second;
    return TotalMemory;
  }

  size_t getBytesAllocated() const { return BytesAllocated; }

  void setRedZoneSize(size_t NewSize) {
    RedZoneSize = NewSize;
  }

  void PrintStats() const {
    detail::printBumpPtrAllocatorStats(Slabs.size(), BytesAllocated,
                                       getTotalMemory());
  }

private:
  /// The current pointer into the current slab.
  ///
  /// This points to the next free byte in the slab.
  char *CurPtr = nullptr;

  /// The end of the current slab.
  char *End = nullptr;

  /// The slabs allocated so far.
  SmallVector<void *, 4> Slabs;

  /// Custom-sized slabs allocated for too-large allocation requests.
  SmallVector<std::pair<void *, size_t>, 0> CustomSizedSlabs;

  /// How many bytes we've allocated.
  ///
  /// Used so that we can compute how much space was wasted.
  size_t BytesAllocated = 0;

  /// The number of bytes to put between allocations when running under
  /// a sanitizer.
  size_t RedZoneSize = 1;

  static size_t computeSlabSize(unsigned SlabIdx) {
    // Scale the actual allocated slab size based on the number of slabs
    // allocated. Every GrowthDelay slabs allocated, we double
    // the allocated size to reduce allocation frequency, but saturate at
    // multiplying the slab size by 2^30.
    return SlabSize *
           ((size_t)1 << std::min<size_t>(30, SlabIdx / GrowthDelay));
  }

  /// Allocate a new slab and move the bump pointers over into the new
  /// slab, modifying CurPtr and End.
  void StartNewSlab() {
    size_t AllocatedSlabSize = computeSlabSize(Slabs.size());

    void *NewSlab = this->getAllocator().Allocate(AllocatedSlabSize,
                                                  alignof(std::max_align_t));
    // We own the new slab and don't want anyone reading anything other than
    // pieces returned from this method.  So poison the whole slab.
    __asan_poison_memory_region(NewSlab, AllocatedSlabSize);

    Slabs.push_back(NewSlab);
    CurPtr = (char *)(NewSlab);
    End = ((char *)NewSlab) + AllocatedSlabSize;
  }

  /// Deallocate a sequence of slabs.
  void DeallocateSlabs(SmallVectorImpl<void *>::iterator I,
                       SmallVectorImpl<void *>::iterator E) {
    for (; I != E; ++I) {
      size_t AllocatedSlabSize =
          computeSlabSize(std::distance(Slabs.begin(), I));
      this->getAllocator().Deallocate(*I, AllocatedSlabSize,
                                      alignof(std::max_align_t));
    }
  }

  /// Deallocate all memory for custom sized slabs.
  void DeallocateCustomSizedSlabs() {
    for (auto &PtrAndSize : CustomSizedSlabs) {
      void *Ptr = PtrAndSize.first;
      size_t Size = PtrAndSize.second;
      this->getAllocator().Deallocate(Ptr, Size, alignof(std::max_align_t));
    }
  }

  template <typename T> friend class SpecificBumpPtrAllocator;
};

/// The standard BumpPtrAllocator which just uses the default template
/// parameters.
typedef BumpPtrAllocatorImpl<> BumpPtrAllocator;

/// A BumpPtrAllocator that allows only elements of a specific type to be
/// allocated.
///
/// This allows calling the destructor in DestroyAll() and when the allocator is
/// destroyed.
template <typename T> class SpecificBumpPtrAllocator {
  BumpPtrAllocator Allocator;

public:
  SpecificBumpPtrAllocator() {
    // Because SpecificBumpPtrAllocator walks the memory to call destructors,
    // it can't have red zones between allocations.
    Allocator.setRedZoneSize(0);
  }
  SpecificBumpPtrAllocator(SpecificBumpPtrAllocator &&Old)
      : Allocator(std::move(Old.Allocator)) {}
  ~SpecificBumpPtrAllocator() { DestroyAll(); }

  SpecificBumpPtrAllocator &operator=(SpecificBumpPtrAllocator &&RHS) {
    Allocator = std::move(RHS.Allocator);
    return *this;
  }

  /// Call the destructor of each allocated object and deallocate all but the
  /// current slab and reset the current pointer to the beginning of it, freeing
  /// all memory allocated so far.
  void DestroyAll() {
    auto DestroyElements = [](char *Begin, char *End) {
      assert(Begin == (char *)alignAddr(Begin, Align::Of<T>()));
      for (char *Ptr = Begin; Ptr + sizeof(T) <= End; Ptr += sizeof(T))
        reinterpret_cast<T *>(Ptr)->~T();
    };

    for (auto I = Allocator.Slabs.begin(), E = Allocator.Slabs.end(); I != E;
         ++I) {
      size_t AllocatedSlabSize = BumpPtrAllocator::computeSlabSize(
          std::distance(Allocator.Slabs.begin(), I));
      char *Begin = (char *)alignAddr(*I, Align::Of<T>());
      char *End = *I == Allocator.Slabs.back() ? Allocator.CurPtr
                                               : (char *)*I + AllocatedSlabSize;

      DestroyElements(Begin, End);
    }

    for (auto &PtrAndSize : Allocator.CustomSizedSlabs) {
      void *Ptr = PtrAndSize.first;
      size_t Size = PtrAndSize.second;
      DestroyElements((char *)alignAddr(Ptr, Align::Of<T>()),
                      (char *)Ptr + Size);
    }

    Allocator.Reset();
  }

  /// Allocate space for an array of objects without constructing them.
  T *Allocate(size_t num = 1) { return Allocator.Allocate<T>(num); }
};

} // end namespace llvm

template <typename AllocatorT, size_t SlabSize, size_t SizeThreshold,
          size_t GrowthDelay>
void *
operator new(size_t Size,
             llvm::BumpPtrAllocatorImpl<AllocatorT, SlabSize, SizeThreshold,
                                        GrowthDelay> &Allocator) {
  return Allocator.Allocate(Size, std::min((size_t)llvm::NextPowerOf2(Size),
                                           alignof(std::max_align_t)));
}

template <typename AllocatorT, size_t SlabSize, size_t SizeThreshold,
          size_t GrowthDelay>
void operator delete(void *,
                     llvm::BumpPtrAllocatorImpl<AllocatorT, SlabSize,
                                                SizeThreshold, GrowthDelay> &) {
}

#endif // LLVM_SUPPORT_ALLOCATOR_H
