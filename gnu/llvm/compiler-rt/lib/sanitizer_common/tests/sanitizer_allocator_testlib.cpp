//===-- sanitizer_allocator_testlib.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Malloc replacement library based on CombinedAllocator.
// The primary purpose of this file is an end-to-end integration test
// for CombinedAllocator.
//===----------------------------------------------------------------------===//
/* Usage:
clang++ -std=c++11 -fno-exceptions  -g -fPIC -I. -I../include -Isanitizer \
 sanitizer_common/tests/sanitizer_allocator_testlib.cpp \
 $(\ls sanitizer_common/sanitizer_*.cpp | grep -v sanitizer_common_nolibc.cpp) \
  sanitizer_common/sanitizer_linux_x86_64.S \
 -shared -lpthread -o testmalloc.so
LD_PRELOAD=`pwd`/testmalloc.so /your/app
*/
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_common.h"
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#ifndef SANITIZER_MALLOC_HOOK
# define SANITIZER_MALLOC_HOOK(p, s)
#endif

#ifndef SANITIZER_FREE_HOOK
# define SANITIZER_FREE_HOOK(p)
#endif

static const uptr kAllocatorSpace = 0x600000000000ULL;
static const uptr kAllocatorSize  =  0x10000000000ULL;  // 1T.

struct __AP64 {
  static const uptr kSpaceBeg = ~(uptr)0;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef CompactSizeClassMap SizeClassMap;
  typedef NoOpMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags =
      SizeClassAllocator64FlagMasks::kRandomShuffleChunks;
};

namespace {

typedef SizeClassAllocator64<__AP64> PrimaryAllocator;
typedef CombinedAllocator<PrimaryAllocator> Allocator;
typedef Allocator::AllocatorCache AllocatorCache;

static Allocator allocator;
static bool global_inited;
static THREADLOCAL AllocatorCache cache;
static THREADLOCAL bool thread_inited;
static pthread_key_t pkey;

static void thread_dtor(void *v) {
  if ((uptr)v != 3) {
    pthread_setspecific(pkey, (void*)((uptr)v + 1));
    return;
  }
  allocator.SwallowCache(&cache);
}

static size_t GetRss() {
  if (FILE *f = fopen("/proc/self/statm", "r")) {
    size_t size = 0, rss = 0;
    fscanf(f, "%zd %zd", &size, &rss);
    fclose(f);
    return rss << 12;  // rss is in pages.
  }
  return 0;
}

struct AtExit {
  ~AtExit() {
    allocator.PrintStats();
    Printf("RSS: %zdM\n", GetRss() >> 20);
  }
};

static AtExit at_exit;

static void NOINLINE thread_init() {
  if (!global_inited) {
    global_inited = true;
    allocator.Init(false /*may_return_null*/);
    pthread_key_create(&pkey, thread_dtor);
  }
  thread_inited = true;
  pthread_setspecific(pkey, (void*)1);
  cache.Init(nullptr);
}
}  // namespace

extern "C" {
void *malloc(size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  void *p = allocator.Allocate(&cache, size, 8);
  SANITIZER_MALLOC_HOOK(p, size);
  return p;
}

void free(void *p) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  SANITIZER_FREE_HOOK(p);
  allocator.Deallocate(&cache, p);
}

void *calloc(size_t nmemb, size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  size *= nmemb;
  void *p = allocator.Allocate(&cache, size, 8, false);
  memset(p, 0, size);
  SANITIZER_MALLOC_HOOK(p, size);
  return p;
}

void *realloc(void *p, size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  if (p) {
    SANITIZER_FREE_HOOK(p);
  }
  p = allocator.Reallocate(&cache, p, size, 8);
  if (p) {
    SANITIZER_MALLOC_HOOK(p, size);
  }
  return p;
}

#if SANITIZER_INTERCEPT_MEMALIGN
void *memalign(size_t alignment, size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  void *p = allocator.Allocate(&cache, size, alignment);
  SANITIZER_MALLOC_HOOK(p, size);
  return p;
}
#endif // SANITIZER_INTERCEPT_MEMALIGN

int posix_memalign(void **memptr, size_t alignment, size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  *memptr = allocator.Allocate(&cache, size, alignment);
  SANITIZER_MALLOC_HOOK(*memptr, size);
  return 0;
}

void *valloc(size_t size) {
  if (UNLIKELY(!thread_inited))
    thread_init();
  if (size == 0)
    size = GetPageSizeCached();
  void *p = allocator.Allocate(&cache, size, GetPageSizeCached());
  SANITIZER_MALLOC_HOOK(p, size);
  return p;
}

#if SANITIZER_INTERCEPT_CFREE
void cfree(void *p) ALIAS(free);
#endif // SANITIZER_INTERCEPT_CFREE
#if SANITIZER_INTERCEPT_PVALLOC
void *pvalloc(size_t size) ALIAS(valloc);
#endif // SANITIZER_INTERCEPT_PVALLOC
#if SANITIZER_INTERCEPT_MEMALIGN
void *__libc_memalign(size_t alignment, size_t size) ALIAS(memalign);
#endif // SANITIZER_INTERCEPT_MEMALIGN

void malloc_usable_size() {
}

#if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
void mallinfo() {
}

void mallopt() {
}
#endif // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
}  // extern "C"

namespace std {
  struct nothrow_t;
}

void *operator new(size_t size) ALIAS(malloc);
void *operator new[](size_t size) ALIAS(malloc);
void *operator new(size_t size, std::nothrow_t const&) ALIAS(malloc);
void *operator new[](size_t size, std::nothrow_t const&) ALIAS(malloc);
void operator delete(void *ptr) throw() ALIAS(free);
void operator delete[](void *ptr) throw() ALIAS(free);
void operator delete(void *ptr, std::nothrow_t const&) ALIAS(free);
void operator delete[](void *ptr, std::nothrow_t const&) ALIAS(free);
