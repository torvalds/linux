//===-- asan_mem_test.cpp -------------------------------------------------===//
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
#include <string.h>
#include "asan_test_utils.h"
#if defined(_GNU_SOURCE)
#include <strings.h>  // for bcmp
#endif
#include <vector>

template<typename T>
void MemSetOOBTestTemplate(size_t length) {
  if (length == 0) return;
  size_t size = Ident(sizeof(T) * length);
  T *array = Ident((T*)malloc(size));
  int element = Ident(42);
  int zero = Ident(0);
  void *(*MEMSET)(void *s, int c, size_t n) = Ident(memset);
  // memset interval inside array
  MEMSET(array, element, size);
  MEMSET(array, element, size - 1);
  MEMSET(array + length - 1, element, sizeof(T));
  MEMSET(array, element, 1);

  // memset 0 bytes
  MEMSET(array - 10, element, zero);
  MEMSET(array - 1, element, zero);
  MEMSET(array, element, zero);
  MEMSET(array + length, 0, zero);
  MEMSET(array + length + 1, 0, zero);

  // try to memset bytes after array
  EXPECT_DEATH(MEMSET(array, 0, size + 1),
               RightOOBWriteMessage(0));
  EXPECT_DEATH(MEMSET((char*)(array + length) - 1, element, 6),
               RightOOBWriteMessage(0));
  EXPECT_DEATH(MEMSET(array + 1, element, size + sizeof(T)),
               RightOOBWriteMessage(0));
  // whole interval is after
  EXPECT_DEATH(MEMSET(array + length + 1, 0, 10),
               RightOOBWriteMessage(sizeof(T)));

  // try to memset bytes before array
  EXPECT_DEATH(MEMSET((char*)array - 1, element, size),
               LeftOOBWriteMessage(1));
  EXPECT_DEATH(MEMSET((char*)array - 5, 0, 6),
               LeftOOBWriteMessage(5));
  if (length >= 100) {
    // Large OOB, we find it only if the redzone is large enough.
    EXPECT_DEATH(memset(array - 5, element, size + 5 * sizeof(T)),
                 LeftOOBWriteMessage(5 * sizeof(T)));
  }
  // whole interval is before
  EXPECT_DEATH(MEMSET(array - 2, 0, sizeof(T)),
               LeftOOBWriteMessage(2 * sizeof(T)));

  // try to memset bytes both before & after
  EXPECT_DEATH(MEMSET((char*)array - 2, element, size + 4),
               LeftOOBWriteMessage(2));

  free(array);
}

TEST(AddressSanitizer, MemSetOOBTest) {
  MemSetOOBTestTemplate<char>(100);
  MemSetOOBTestTemplate<int>(5);
  MemSetOOBTestTemplate<double>(256);
  // We can test arrays of structres/classes here, but what for?
}

// Try to allocate two arrays of 'size' bytes that are near each other.
// Strictly speaking we are not guaranteed to find such two pointers,
// but given the structure of asan's allocator we will.
static bool AllocateTwoAdjacentArrays(char **x1, char **x2, size_t size) {
  std::vector<uintptr_t> v;
  bool res = false;
  for (size_t i = 0; i < 1000U && !res; i++) {
    v.push_back(reinterpret_cast<uintptr_t>(new char[size]));
    if (i == 0) continue;
    sort(v.begin(), v.end());
    for (size_t j = 1; j < v.size(); j++) {
      assert(v[j] > v[j-1]);
      if ((size_t)(v[j] - v[j-1]) < size * 2) {
        *x2 = reinterpret_cast<char*>(v[j]);
        *x1 = reinterpret_cast<char*>(v[j-1]);
        res = true;
        break;
      }
    }
  }

  for (size_t i = 0; i < v.size(); i++) {
    char *p = reinterpret_cast<char *>(v[i]);
    if (res && p == *x1) continue;
    if (res && p == *x2) continue;
    delete [] p;
  }
  return res;
}

TEST(AddressSanitizer, LargeOOBInMemset) {
  for (size_t size = 200; size < 100000; size += size / 2) {
    char *x1, *x2;
    if (!Ident(AllocateTwoAdjacentArrays)(&x1, &x2, size))
      continue;
    // fprintf(stderr, "  large oob memset: %p %p %zd\n", x1, x2, size);
    // Do a memset on x1 with huge out-of-bound access that will end up in x2.
    EXPECT_DEATH(Ident(memset)(x1, 0, size * 2),
                 "is located 0 bytes after");
    delete [] x1;
    delete [] x2;
    return;
  }
  assert(0 && "Did not find two adjacent malloc-ed pointers");
}

// Same test for memcpy and memmove functions
template <typename T, class M>
void MemTransferOOBTestTemplate(size_t length) {
  if (length == 0) return;
  size_t size = Ident(sizeof(T) * length);
  T *src = Ident((T*)malloc(size));
  T *dest = Ident((T*)malloc(size));
  int zero = Ident(0);

  // valid transfer of bytes between arrays
  M::transfer(dest, src, size);
  M::transfer(dest + 1, src, size - sizeof(T));
  M::transfer(dest, src + length - 1, sizeof(T));
  M::transfer(dest, src, 1);

  // transfer zero bytes
  M::transfer(dest - 1, src, 0);
  M::transfer(dest + length, src, zero);
  M::transfer(dest, src - 1, zero);
  M::transfer(dest, src, zero);

  // try to change mem after dest
  EXPECT_DEATH(M::transfer(dest + 1, src, size),
               RightOOBWriteMessage(0));
  EXPECT_DEATH(M::transfer((char*)(dest + length) - 1, src, 5),
               RightOOBWriteMessage(0));

  // try to change mem before dest
  EXPECT_DEATH(M::transfer(dest - 2, src, size),
               LeftOOBWriteMessage(2 * sizeof(T)));
  EXPECT_DEATH(M::transfer((char*)dest - 3, src, 4),
               LeftOOBWriteMessage(3));

  // try to access mem after src
  EXPECT_DEATH(M::transfer(dest, src + 2, size),
               RightOOBReadMessage(0));
  EXPECT_DEATH(M::transfer(dest, (char*)(src + length) - 3, 6),
               RightOOBReadMessage(0));

  // try to access mem before src
  EXPECT_DEATH(M::transfer(dest, src - 1, size),
               LeftOOBReadMessage(sizeof(T)));
  EXPECT_DEATH(M::transfer(dest, (char*)src - 6, 7),
               LeftOOBReadMessage(6));

  // Generally we don't need to test cases where both accessing src and writing
  // to dest address to poisoned memory.

  T *big_src = Ident((T*)malloc(size * 2));
  T *big_dest = Ident((T*)malloc(size * 2));
  // try to change mem to both sides of dest
  EXPECT_DEATH(M::transfer(dest - 1, big_src, size * 2),
               LeftOOBWriteMessage(sizeof(T)));
  // try to access mem to both sides of src
  EXPECT_DEATH(M::transfer(big_dest, src - 2, size * 2),
               LeftOOBReadMessage(2 * sizeof(T)));

  free(src);
  free(dest);
  free(big_src);
  free(big_dest);
}

class MemCpyWrapper {
 public:
  static void* transfer(void *to, const void *from, size_t size) {
    return Ident(memcpy)(to, from, size);
  }
};

TEST(AddressSanitizer, MemCpyOOBTest) {
  MemTransferOOBTestTemplate<char, MemCpyWrapper>(100);
  MemTransferOOBTestTemplate<int, MemCpyWrapper>(1024);
}

class MemMoveWrapper {
 public:
  static void* transfer(void *to, const void *from, size_t size) {
    return Ident(memmove)(to, from, size);
  }
};

TEST(AddressSanitizer, MemMoveOOBTest) {
  MemTransferOOBTestTemplate<char, MemMoveWrapper>(100);
  MemTransferOOBTestTemplate<int, MemMoveWrapper>(1024);
}

template <int (*cmpfn)(const void *, const void *, size_t)>
void CmpOOBTestCommon() {
  size_t size = Ident(100);
  char *s1 = MallocAndMemsetString(size);
  char *s2 = MallocAndMemsetString(size);
  // Normal cmpfn calls.
  Ident(cmpfn(s1, s2, size));
  Ident(cmpfn(s1 + size - 1, s2 + size - 1, 1));
  Ident(cmpfn(s1 - 1, s2 - 1, 0));
  // One of arguments points to not allocated memory.
  EXPECT_DEATH(Ident(cmpfn)(s1 - 1, s2, 1), LeftOOBReadMessage(1));
  EXPECT_DEATH(Ident(cmpfn)(s1, s2 - 1, 1), LeftOOBReadMessage(1));
  EXPECT_DEATH(Ident(cmpfn)(s1 + size, s2, 1), RightOOBReadMessage(0));
  EXPECT_DEATH(Ident(cmpfn)(s1, s2 + size, 1), RightOOBReadMessage(0));
  // Hit unallocated memory and die.
  EXPECT_DEATH(Ident(cmpfn)(s1 + 1, s2 + 1, size), RightOOBReadMessage(0));
  EXPECT_DEATH(Ident(cmpfn)(s1 + size - 1, s2, 2), RightOOBReadMessage(0));
  // Zero bytes are not terminators and don't prevent from OOB.
  s1[size - 1] = '\0';
  s2[size - 1] = '\0';
  EXPECT_DEATH(Ident(cmpfn)(s1, s2, size + 1), RightOOBReadMessage(0));

  // Even if the buffers differ in the first byte, we still assume that
  // cmpfn may access the whole buffer and thus reporting the overflow here:
  s1[0] = 1;
  s2[0] = 123;
  EXPECT_DEATH(Ident(cmpfn)(s1, s2, size + 1), RightOOBReadMessage(0));

  free(s1);
  free(s2);
}

TEST(AddressSanitizer, MemCmpOOBTest) { CmpOOBTestCommon<memcmp>(); }

TEST(AddressSanitizer, BCmpOOBTest) {
#if (defined(__linux__) && !defined(__ANDROID__) && defined(_GNU_SOURCE)) || \
    defined(__NetBSD__) || defined(__FreeBSD__)
  CmpOOBTestCommon<bcmp>();
#endif
}
