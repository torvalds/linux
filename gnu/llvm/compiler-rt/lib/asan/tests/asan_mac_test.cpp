//===-- asan_test_mac.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
//===----------------------------------------------------------------------===//

#include "asan_test_utils.h"

#include "asan_mac_test.h"

#include <malloc/malloc.h>
#include <AvailabilityMacros.h>  // For MAC_OS_X_VERSION_*
#include <CoreFoundation/CFString.h>

TEST(AddressSanitizerMac, CFAllocatorDefaultDoubleFree) {
  EXPECT_DEATH(
      CFAllocatorDefaultDoubleFree(NULL),
      "attempting double-free");
}

void CFAllocator_DoubleFreeOnPthread() {
  pthread_t child;
  PTHREAD_CREATE(&child, NULL, CFAllocatorDefaultDoubleFree, NULL);
  PTHREAD_JOIN(child, NULL);  // Shouldn't be reached.
}

TEST(AddressSanitizerMac, CFAllocatorDefaultDoubleFree_ChildPhread) {
  EXPECT_DEATH(CFAllocator_DoubleFreeOnPthread(), "attempting double-free");
}

namespace {

void *GLOB;

void *CFAllocatorAllocateToGlob(void *unused) {
  GLOB = CFAllocatorAllocate(NULL, 100, /*hint*/0);
  return NULL;
}

void *CFAllocatorDeallocateFromGlob(void *unused) {
  char *p = (char*)GLOB;
  p[100] = 'A';  // ASan should report an error here.
  CFAllocatorDeallocate(NULL, GLOB);
  return NULL;
}

void CFAllocator_PassMemoryToAnotherThread() {
  pthread_t th1, th2;
  PTHREAD_CREATE(&th1, NULL, CFAllocatorAllocateToGlob, NULL);
  PTHREAD_JOIN(th1, NULL);
  PTHREAD_CREATE(&th2, NULL, CFAllocatorDeallocateFromGlob, NULL);
  PTHREAD_JOIN(th2, NULL);
}

TEST(AddressSanitizerMac, CFAllocator_PassMemoryToAnotherThread) {
  EXPECT_DEATH(CFAllocator_PassMemoryToAnotherThread(),
               "heap-buffer-overflow");
}

}  // namespace

// TODO(glider): figure out whether we still need these tests. Is it correct
// to intercept the non-default CFAllocators?
TEST(AddressSanitizerMac, DISABLED_CFAllocatorSystemDefaultDoubleFree) {
  EXPECT_DEATH(
      CFAllocatorSystemDefaultDoubleFree(),
      "attempting double-free");
}

// We're intercepting malloc, so kCFAllocatorMalloc is routed to ASan.
TEST(AddressSanitizerMac, CFAllocatorMallocDoubleFree) {
  EXPECT_DEATH(CFAllocatorMallocDoubleFree(), "attempting double-free");
}

TEST(AddressSanitizerMac, DISABLED_CFAllocatorMallocZoneDoubleFree) {
  EXPECT_DEATH(CFAllocatorMallocZoneDoubleFree(), "attempting double-free");
}

// For libdispatch tests below we check that ASan got to the shadow byte
// legend, i.e. managed to print the thread stacks (this almost certainly
// means that the libdispatch task creation has been intercepted correctly).
TEST(AddressSanitizerMac, GCDDispatchAsync) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDDispatchAsync(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDDispatchSync) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDDispatchSync(), "Shadow byte legend");
}


TEST(AddressSanitizerMac, GCDReuseWqthreadsAsync) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDReuseWqthreadsAsync(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDReuseWqthreadsSync) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDReuseWqthreadsSync(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDDispatchAfter) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDDispatchAfter(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDSourceEvent) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDSourceEvent(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDSourceCancel) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDSourceCancel(), "Shadow byte legend");
}

TEST(AddressSanitizerMac, GCDGroupAsync) {
  // Make sure the whole ASan report is printed, i.e. that we don't die
  // on a CHECK.
  EXPECT_DEATH(TestGCDGroupAsync(), "Shadow byte legend");
}

void *MallocIntrospectionLockWorker(void *_) {
  const int kNumPointers = 100;
  int i;
  void *pointers[kNumPointers];
  for (i = 0; i < kNumPointers; i++) {
    pointers[i] = malloc(i + 1);
  }
  for (i = 0; i < kNumPointers; i++) {
    free(pointers[i]);
  }

  return NULL;
}

void *MallocIntrospectionLockForker(void *_) {
  pid_t result = fork();
  if (result == -1) {
    perror("fork");
  }
  assert(result != -1);
  if (result == 0) {
    // Call malloc in the child process to make sure we won't deadlock.
    void *ptr = malloc(42);
    free(ptr);
    exit(0);
  } else {
    // Return in the parent process.
    return NULL;
  }
}

TEST(AddressSanitizerMac, MallocIntrospectionLock) {
  // Incorrect implementation of force_lock and force_unlock in our malloc zone
  // will cause forked processes to deadlock.
  // TODO(glider): need to detect that none of the child processes deadlocked.
  const int kNumWorkers = 5, kNumIterations = 100;
  int i, iter;
  for (iter = 0; iter < kNumIterations; iter++) {
    pthread_t workers[kNumWorkers], forker;
    for (i = 0; i < kNumWorkers; i++) {
      PTHREAD_CREATE(&workers[i], 0, MallocIntrospectionLockWorker, 0);
    }
    PTHREAD_CREATE(&forker, 0, MallocIntrospectionLockForker, 0);
    for (i = 0; i < kNumWorkers; i++) {
      PTHREAD_JOIN(workers[i], 0);
    }
    PTHREAD_JOIN(forker, 0);
  }
}

void *TSDAllocWorker(void *test_key) {
  if (test_key) {
    void *mem = malloc(10);
    pthread_setspecific(*(pthread_key_t*)test_key, mem);
  }
  return NULL;
}

TEST(AddressSanitizerMac, DISABLED_TSDWorkqueueTest) {
  pthread_t th;
  pthread_key_t test_key;
  pthread_key_create(&test_key, CallFreeOnWorkqueue);
  PTHREAD_CREATE(&th, NULL, TSDAllocWorker, &test_key);
  PTHREAD_JOIN(th, NULL);
  pthread_key_delete(test_key);
}

// Test that CFStringCreateCopy does not copy constant strings.
TEST(AddressSanitizerMac, CFStringCreateCopy) {
  CFStringRef str = CFSTR("Hello world!\n");
  CFStringRef str2 = CFStringCreateCopy(0, str);
  EXPECT_EQ(str, str2);
}

TEST(AddressSanitizerMac, NSObjectOOB) {
  // Make sure that our allocators are used for NSObjects.
  EXPECT_DEATH(TestOOBNSObjects(), "heap-buffer-overflow");
}

// Make sure that correct pointer is passed to free() when deallocating a
// NSURL object.
// See https://github.com/google/sanitizers/issues/70.
TEST(AddressSanitizerMac, NSURLDeallocation) {
  TestNSURLDeallocation();
}

// See https://github.com/google/sanitizers/issues/109.
TEST(AddressSanitizerMac, Mstats) {
  malloc_statistics_t stats1, stats2;
  malloc_zone_statistics(/*all zones*/NULL, &stats1);
  const size_t kMallocSize = 100000;
  void *alloc = Ident(malloc(kMallocSize));
  malloc_zone_statistics(/*all zones*/NULL, &stats2);
  EXPECT_GT(stats2.blocks_in_use, stats1.blocks_in_use);
  EXPECT_GE(stats2.size_in_use - stats1.size_in_use, kMallocSize);
  free(alloc);
  // Even the default OSX allocator may not change the stats after free().
}

