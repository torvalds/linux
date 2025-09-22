//===- ThreadSafeAllocator.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THREADSAFEALLOCATOR_H
#define LLVM_SUPPORT_THREADSAFEALLOCATOR_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Allocator.h"
#include <atomic>

namespace llvm {

/// Thread-safe allocator adaptor. Uses a spin lock on the assumption that
/// contention here is extremely rare.
///
/// TODO: Using a spin lock on every allocation can be quite expensive when
/// contention is high. Since this is mainly used for BumpPtrAllocator and
/// SpecificBumpPtrAllocator, it'd be better to have a specific thread-safe
/// BumpPtrAllocator implementation that only use a fair lock when allocating a
/// new slab but otherwise using atomic and be lock-free.
template <class AllocatorType> class ThreadSafeAllocator {
  struct LockGuard {
    LockGuard(std::atomic_flag &Flag) : Flag(Flag) {
      if (LLVM_UNLIKELY(Flag.test_and_set(std::memory_order_acquire)))
        while (Flag.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~LockGuard() { Flag.clear(std::memory_order_release); }
    std::atomic_flag &Flag;
  };

public:
  auto Allocate(size_t N) {
    return applyLocked([N](AllocatorType &Alloc) { return Alloc.Allocate(N); });
  }

  auto Allocate(size_t Size, size_t Align) {
    return applyLocked([Size, Align](AllocatorType &Alloc) {
      return Alloc.Allocate(Size, Align);
    });
  }

  template <typename FnT,
            typename T = typename llvm::function_traits<FnT>::result_t>
  T applyLocked(FnT Fn) {
    LockGuard Lock(Flag);
    return Fn(Alloc);
  }

private:
  AllocatorType Alloc;
  std::atomic_flag Flag = ATOMIC_FLAG_INIT;
};

} // namespace llvm

#endif // LLVM_SUPPORT_THREADSAFEALLOCATOR_H
