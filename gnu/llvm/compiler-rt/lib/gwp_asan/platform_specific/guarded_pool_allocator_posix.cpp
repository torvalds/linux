//===-- guarded_pool_allocator_posix.cpp ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/common.h"
#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/platform_specific/guarded_pool_allocator_tls.h"
#include "gwp_asan/utilities.h"

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifdef ANDROID
#include <sys/prctl.h>
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0
#endif // ANDROID

namespace {
void MaybeSetMappingName(void *Mapping, size_t Size, const char *Name) {
#ifdef ANDROID
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, Mapping, Size, Name);
#endif // ANDROID
  // Anonymous mapping names are only supported on Android.
  return;
}
} // anonymous namespace

namespace gwp_asan {

void GuardedPoolAllocator::initPRNG() {
  getThreadLocals()->RandomState =
      static_cast<uint32_t>(time(nullptr) + getThreadID());
}

void *GuardedPoolAllocator::map(size_t Size, const char *Name) const {
  assert((Size % State.PageSize) == 0);
  void *Ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  check(Ptr != MAP_FAILED, "Failed to map guarded pool allocator memory");
  MaybeSetMappingName(Ptr, Size, Name);
  return Ptr;
}

void GuardedPoolAllocator::unmap(void *Ptr, size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  check(munmap(Ptr, Size) == 0,
        "Failed to unmap guarded pool allocator memory.");
}

void *GuardedPoolAllocator::reserveGuardedPool(size_t Size) {
  assert((Size % State.PageSize) == 0);
  void *Ptr =
      mmap(nullptr, Size, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  check(Ptr != MAP_FAILED, "Failed to reserve guarded pool allocator memory");
  MaybeSetMappingName(Ptr, Size, kGwpAsanGuardPageName);
  return Ptr;
}

void GuardedPoolAllocator::unreserveGuardedPool() {
  unmap(reinterpret_cast<void *>(State.GuardedPagePool),
        State.GuardedPagePoolEnd - State.GuardedPagePool);
}

void GuardedPoolAllocator::allocateInGuardedPool(void *Ptr, size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  check(mprotect(Ptr, Size, PROT_READ | PROT_WRITE) == 0,
        "Failed to allocate in guarded pool allocator memory");
  MaybeSetMappingName(Ptr, Size, kGwpAsanAliveSlotName);
}

void GuardedPoolAllocator::deallocateInGuardedPool(void *Ptr,
                                                   size_t Size) const {
  assert((reinterpret_cast<uintptr_t>(Ptr) % State.PageSize) == 0);
  assert((Size % State.PageSize) == 0);
  // mmap() a PROT_NONE page over the address to release it to the system, if
  // we used mprotect() here the system would count pages in the quarantine
  // against the RSS.
  check(mmap(Ptr, Size, PROT_NONE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1,
             0) != MAP_FAILED,
        "Failed to deallocate in guarded pool allocator memory");
  MaybeSetMappingName(Ptr, Size, kGwpAsanGuardPageName);
}

size_t GuardedPoolAllocator::getPlatformPageSize() {
  return sysconf(_SC_PAGESIZE);
}

void GuardedPoolAllocator::installAtFork() {
  static bool AtForkInstalled = false;
  if (AtForkInstalled)
    return;
  AtForkInstalled = true;
  auto Disable = []() {
    if (auto *S = getSingleton())
      S->disable();
  };
  auto Enable = []() {
    if (auto *S = getSingleton())
      S->enable();
  };
  pthread_atfork(Disable, Enable, Enable);
}
} // namespace gwp_asan
