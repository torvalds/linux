//===- PerThreadBumpPtrAllocator.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H
#define LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H

#include "llvm/Support/Allocator.h"
#include "llvm/Support/Parallel.h"

namespace llvm {
namespace parallel {

/// PerThreadAllocator is used in conjunction with ThreadPoolExecutor to allow
/// per-thread allocations. It wraps a possibly thread-unsafe allocator,
/// e.g. BumpPtrAllocator. PerThreadAllocator must be used with only main thread
/// or threads created by ThreadPoolExecutor, as it utilizes getThreadIndex,
/// which is set by ThreadPoolExecutor. To work properly, ThreadPoolExecutor
/// should be initialized before PerThreadAllocator is created.
/// TODO: The same approach might be implemented for ThreadPool.

template <typename AllocatorTy>
class PerThreadAllocator
    : public AllocatorBase<PerThreadAllocator<AllocatorTy>> {
public:
  PerThreadAllocator()
      : NumOfAllocators(parallel::getThreadCount()),
        Allocators(std::make_unique<AllocatorTy[]>(NumOfAllocators)) {}

  /// \defgroup Methods which could be called asynchronously:
  ///
  /// @{

  using AllocatorBase<PerThreadAllocator<AllocatorTy>>::Allocate;

  using AllocatorBase<PerThreadAllocator<AllocatorTy>>::Deallocate;

  /// Allocate \a Size bytes of \a Alignment aligned memory.
  void *Allocate(size_t Size, size_t Alignment) {
    assert(getThreadIndex() < NumOfAllocators);
    return Allocators[getThreadIndex()].Allocate(Size, Alignment);
  }

  /// Deallocate \a Ptr to \a Size bytes of memory allocated by this
  /// allocator.
  void Deallocate(const void *Ptr, size_t Size, size_t Alignment) {
    assert(getThreadIndex() < NumOfAllocators);
    return Allocators[getThreadIndex()].Deallocate(Ptr, Size, Alignment);
  }

  /// Return allocator corresponding to the current thread.
  AllocatorTy &getThreadLocalAllocator() {
    assert(getThreadIndex() < NumOfAllocators);
    return Allocators[getThreadIndex()];
  }

  // Return number of used allocators.
  size_t getNumberOfAllocators() const { return NumOfAllocators; }
  /// @}

  /// \defgroup Methods which could not be called asynchronously:
  ///
  /// @{

  /// Reset state of allocators.
  void Reset() {
    for (size_t Idx = 0; Idx < getNumberOfAllocators(); Idx++)
      Allocators[Idx].Reset();
  }

  /// Return total memory size used by all allocators.
  size_t getTotalMemory() const {
    size_t TotalMemory = 0;

    for (size_t Idx = 0; Idx < getNumberOfAllocators(); Idx++)
      TotalMemory += Allocators[Idx].getTotalMemory();

    return TotalMemory;
  }

  /// Return allocated size by all allocators.
  size_t getBytesAllocated() const {
    size_t BytesAllocated = 0;

    for (size_t Idx = 0; Idx < getNumberOfAllocators(); Idx++)
      BytesAllocated += Allocators[Idx].getBytesAllocated();

    return BytesAllocated;
  }

  /// Set red zone for all allocators.
  void setRedZoneSize(size_t NewSize) {
    for (size_t Idx = 0; Idx < getNumberOfAllocators(); Idx++)
      Allocators[Idx].setRedZoneSize(NewSize);
  }

  /// Print statistic for each allocator.
  void PrintStats() const {
    for (size_t Idx = 0; Idx < getNumberOfAllocators(); Idx++) {
      errs() << "\n Allocator " << Idx << "\n";
      Allocators[Idx].PrintStats();
    }
  }
  /// @}

protected:
  size_t NumOfAllocators;
  std::unique_ptr<AllocatorTy[]> Allocators;
};

using PerThreadBumpPtrAllocator = PerThreadAllocator<BumpPtrAllocator>;

} // end namespace parallel
} // end namespace llvm

#endif // LLVM_SUPPORT_PERTHREADBUMPPTRALLOCATOR_H
