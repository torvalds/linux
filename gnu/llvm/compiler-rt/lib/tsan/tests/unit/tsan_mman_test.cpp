//===-- tsan_mman_test.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include <limits>
#include <sanitizer/allocator_interface.h>
#include "tsan_mman.h"
#include "tsan_rtl.h"
#include "gtest/gtest.h"

namespace __tsan {

TEST(Mman, Internal) {
  char *p = (char *)Alloc(10);
  EXPECT_NE(p, (char*)0);
  char *p2 = (char *)Alloc(20);
  EXPECT_NE(p2, (char*)0);
  EXPECT_NE(p2, p);
  for (int i = 0; i < 10; i++) {
    p[i] = 42;
  }
  for (int i = 0; i < 20; i++) {
    ((char*)p2)[i] = 42;
  }
  Free(p);
  Free(p2);
}

TEST(Mman, User) {
  ThreadState *thr = cur_thread();
  uptr pc = 0;
  char *p = (char*)user_alloc(thr, pc, 10);
  EXPECT_NE(p, (char*)0);
  char *p2 = (char*)user_alloc(thr, pc, 20);
  EXPECT_NE(p2, (char*)0);
  EXPECT_NE(p2, p);
  EXPECT_EQ(10U, user_alloc_usable_size(p));
  EXPECT_EQ(20U, user_alloc_usable_size(p2));
  user_free(thr, pc, p);
  user_free(thr, pc, p2);
}

TEST(Mman, UserRealloc) {
  ThreadState *thr = cur_thread();
  uptr pc = 0;
  {
    void *p = user_realloc(thr, pc, 0, 0);
    // Realloc(NULL, N) is equivalent to malloc(N), thus must return
    // non-NULL pointer.
    EXPECT_NE(p, (void*)0);
    user_free(thr, pc, p);
  }
  {
    void *p = user_realloc(thr, pc, 0, 100);
    EXPECT_NE(p, (void*)0);
    memset(p, 0xde, 100);
    user_free(thr, pc, p);
  }
  {
    void *p = user_alloc(thr, pc, 100);
    EXPECT_NE(p, (void*)0);
    memset(p, 0xde, 100);
    // Realloc(P, 0) is equivalent to free(P) and returns NULL.
    void *p2 = user_realloc(thr, pc, p, 0);
    EXPECT_EQ(p2, (void*)0);
  }
  {
    void *p = user_realloc(thr, pc, 0, 100);
    EXPECT_NE(p, (void*)0);
    memset(p, 0xde, 100);
    void *p2 = user_realloc(thr, pc, p, 10000);
    EXPECT_NE(p2, (void*)0);
    for (int i = 0; i < 100; i++)
      EXPECT_EQ(((char*)p2)[i], (char)0xde);
    memset(p2, 0xde, 10000);
    user_free(thr, pc, p2);
  }
  {
    void *p = user_realloc(thr, pc, 0, 10000);
    EXPECT_NE(p, (void*)0);
    memset(p, 0xde, 10000);
    void *p2 = user_realloc(thr, pc, p, 10);
    EXPECT_NE(p2, (void*)0);
    for (int i = 0; i < 10; i++)
      EXPECT_EQ(((char*)p2)[i], (char)0xde);
    user_free(thr, pc, p2);
  }
}

TEST(Mman, UsableSize) {
  ThreadState *thr = cur_thread();
  uptr pc = 0;
  char *p = (char*)user_alloc(thr, pc, 10);
  char *p2 = (char*)user_alloc(thr, pc, 20);
  EXPECT_EQ(0U, user_alloc_usable_size(NULL));
  EXPECT_EQ(10U, user_alloc_usable_size(p));
  EXPECT_EQ(20U, user_alloc_usable_size(p2));
  user_free(thr, pc, p);
  user_free(thr, pc, p2);
  EXPECT_EQ(0U, user_alloc_usable_size((void*)0x4123));
}

TEST(Mman, Stats) {
  ThreadState *thr = cur_thread();

  uptr alloc0 = __sanitizer_get_current_allocated_bytes();
  uptr heap0 = __sanitizer_get_heap_size();
  uptr free0 = __sanitizer_get_free_bytes();
  uptr unmapped0 = __sanitizer_get_unmapped_bytes();

  EXPECT_EQ(10U, __sanitizer_get_estimated_allocated_size(10));
  EXPECT_EQ(20U, __sanitizer_get_estimated_allocated_size(20));
  EXPECT_EQ(100U, __sanitizer_get_estimated_allocated_size(100));

  char *p = (char*)user_alloc(thr, 0, 10);
  EXPECT_TRUE(__sanitizer_get_ownership(p));
  EXPECT_EQ(10U, __sanitizer_get_allocated_size(p));

  EXPECT_EQ(alloc0 + 16, __sanitizer_get_current_allocated_bytes());
  EXPECT_GE(__sanitizer_get_heap_size(), heap0);
  EXPECT_EQ(free0, __sanitizer_get_free_bytes());
  EXPECT_EQ(unmapped0, __sanitizer_get_unmapped_bytes());

  user_free(thr, 0, p);

  EXPECT_EQ(alloc0, __sanitizer_get_current_allocated_bytes());
  EXPECT_GE(__sanitizer_get_heap_size(), heap0);
  EXPECT_EQ(free0, __sanitizer_get_free_bytes());
  EXPECT_EQ(unmapped0, __sanitizer_get_unmapped_bytes());
}

TEST(Mman, Valloc) {
  ThreadState *thr = cur_thread();
  uptr page_size = GetPageSizeCached();

  void *p = user_valloc(thr, 0, 100);
  EXPECT_NE(p, (void*)0);
  user_free(thr, 0, p);

  p = user_pvalloc(thr, 0, 100);
  EXPECT_NE(p, (void*)0);
  user_free(thr, 0, p);

  p = user_pvalloc(thr, 0, 0);
  EXPECT_NE(p, (void*)0);
  EXPECT_EQ(page_size, __sanitizer_get_allocated_size(p));
  user_free(thr, 0, p);
}

#if !SANITIZER_DEBUG
// EXPECT_DEATH clones a thread with 4K stack,
// which is overflown by tsan memory accesses functions in debug mode.

TEST(Mman, Memalign) {
  ThreadState *thr = cur_thread();

  void *p = user_memalign(thr, 0, 8, 100);
  EXPECT_NE(p, (void*)0);
  user_free(thr, 0, p);

  // TODO(alekseyshl): Remove this death test when memalign is verified by
  // tests in sanitizer_common.
  p = NULL;
  EXPECT_DEATH(p = user_memalign(thr, 0, 7, 100),
               "invalid-allocation-alignment");
  EXPECT_EQ(0L, p);
}

#endif

TEST(Mman, PosixMemalign) {
  ThreadState *thr = cur_thread();

  void *p = NULL;
  int res = user_posix_memalign(thr, 0, &p, 8, 100);
  EXPECT_NE(p, (void*)0);
  EXPECT_EQ(res, 0);
  user_free(thr, 0, p);
}

TEST(Mman, AlignedAlloc) {
  ThreadState *thr = cur_thread();

  void *p = user_aligned_alloc(thr, 0, 8, 64);
  EXPECT_NE(p, (void*)0);
  user_free(thr, 0, p);
}

}  // namespace __tsan
