//===-- msan_test.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// MemorySanitizer unit tests.
//===----------------------------------------------------------------------===//

#ifndef MSAN_EXTERNAL_TEST_CONFIG
#include "msan_test_config.h"
#endif // MSAN_EXTERNAL_TEST_CONFIG

#include "sanitizer_common/tests/sanitizer_test_utils.h"

#include "sanitizer/allocator_interface.h"
#include "sanitizer/msan_interface.h"

#if defined(__FreeBSD__)
# define _KERNEL  // To declare 'shminfo' structure.
# include <sys/shm.h>
# undef _KERNEL
extern "C" {
// <sys/shm.h> doesn't declare these functions in _KERNEL mode.
void *shmat(int, const void *, int);
int shmget(key_t, size_t, int);
int shmctl(int, int, struct shmid_ds *);
int shmdt(const void *);
}
#endif

#if defined(__linux__) && !defined(__GLIBC__) && !defined(__ANDROID__)
#define MUSL 1
#endif

#include <inttypes.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>
#include <math.h>

#include <arpa/inet.h>
#include <dlfcn.h>
#include <grp.h>
#include <unistd.h>
#include <link.h>
#include <limits.h>
#include <sys/time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <dirent.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <wordexp.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if defined(__NetBSD__)
# include <signal.h>
# include <netinet/in.h>
# include <sys/uio.h>
# include <sys/mount.h>
# include <sys/sysctl.h>
# include <net/if.h>
# include <net/if_ether.h>
#elif defined(__FreeBSD__)
# include <signal.h>
# include <netinet/in.h>
# include <pthread_np.h>
# include <sys/uio.h>
# include <sys/mount.h>
# include <sys/sysctl.h>
# include <net/ethernet.h>
# define f_namelen f_namemax  // FreeBSD names this statfs field so.
# define cpu_set_t cpuset_t
extern "C" {
// FreeBSD's <ssp/string.h> defines mempcpy() to be a macro expanding into
// a __builtin___mempcpy_chk() call, but since Msan RTL defines it as an
// ordinary function, we can declare it here to complete the tests.
void *mempcpy(void *dest, const void *src, size_t n);
}
#else
# include <malloc.h>
# include <sys/sysinfo.h>
# include <sys/vfs.h>
# include <mntent.h>
# include <netinet/ether.h>
# if defined(__linux__)
#  include <sys/uio.h>
# endif
#endif

#if defined(__i386__) || defined(__x86_64__)
# include <emmintrin.h>
# define MSAN_HAS_M128 1
#else
# define MSAN_HAS_M128 0
#endif

#ifdef __AVX2__
# include <immintrin.h>
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
# define FILE_TO_READ "/bin/cat"
# define DIR_TO_READ "/bin"
# define SUBFILE_TO_READ "cat"
# define SYMLINK_TO_READ "/usr/bin/tar"
# define SUPERUSER_GROUP "wheel"
#else
# define FILE_TO_READ "/proc/self/stat"
# define DIR_TO_READ "/proc/self"
# define SUBFILE_TO_READ "stat"
# define SYMLINK_TO_READ "/proc/self/exe"
# define SUPERUSER_GROUP "root"
#endif

static uintptr_t GetPageSize() {
  return sysconf(_SC_PAGESIZE);
}

const size_t kMaxPathLength = 4096;

typedef unsigned char U1;
typedef unsigned short U2;
typedef unsigned int U4;
typedef unsigned long long U8;
typedef signed char S1;
typedef signed short S2;
typedef signed int S4;
typedef signed long long S8;
#define NOINLINE      __attribute__((noinline))
#define ALWAYS_INLINE __attribute__((always_inline))

static bool TrackingOrigins() {
  S8 x;
  __msan_set_origin(&x, sizeof(x), 0x1234);
  U4 origin = __msan_get_origin(&x);
  __msan_set_origin(&x, sizeof(x), 0);
  return __msan_origin_is_descendant_or_same(origin, 0x1234);
}

#define EXPECT_ORIGIN(expected, origin) \
  EXPECT_TRUE(__msan_origin_is_descendant_or_same((origin), (expected)))

#define EXPECT_UMR(action) \
    do {                        \
      __msan_set_expect_umr(1); \
      action;                   \
      __msan_set_expect_umr(0); \
    } while (0)

#define EXPECT_UMR_O(action, origin)                                       \
  do {                                                                     \
    __msan_set_expect_umr(1);                                              \
    action;                                                                \
    __msan_set_expect_umr(0);                                              \
    if (TrackingOrigins()) EXPECT_ORIGIN(origin, __msan_get_umr_origin()); \
  } while (0)

#define EXPECT_POISONED(x) ExpectPoisoned(x)

template <typename T>
void ExpectPoisoned(const T& t) {
  EXPECT_NE(-1, __msan_test_shadow((void*)&t, sizeof(t)));
}

#define EXPECT_POISONED_O(x, origin) \
  ExpectPoisonedWithOrigin(x, origin)

template<typename T>
void ExpectPoisonedWithOrigin(const T& t, unsigned origin) {
  EXPECT_NE(-1, __msan_test_shadow((void*)&t, sizeof(t)));
  if (TrackingOrigins()) EXPECT_ORIGIN(origin, __msan_get_origin((void *)&t));
}

#define EXPECT_NOT_POISONED(x) EXPECT_EQ(true, TestForNotPoisoned((x)))
#define EXPECT_NOT_POISONED2(data, size) \
  EXPECT_EQ(true, TestForNotPoisoned((data), (size)))

bool TestForNotPoisoned(const void *data, size_t size) {
  return __msan_test_shadow(data, size) == -1;
}

template<typename T>
bool TestForNotPoisoned(const T& t) {
  return TestForNotPoisoned((void *)&t, sizeof(t));
}

static U8 poisoned_array[100];
template<class T>
T *GetPoisoned(int i = 0, T val = 0) {
  T *res = (T*)&poisoned_array[i];
  *res = val;
  __msan_poison(&poisoned_array[i], sizeof(T));
  return res;
}

template<class T>
T *GetPoisonedO(int i, U4 origin, T val = 0) {
  T *res = (T*)&poisoned_array[i];
  *res = val;
  __msan_poison(&poisoned_array[i], sizeof(T));
  __msan_set_origin(&poisoned_array[i], sizeof(T), origin);
  return res;
}

template<typename T>
T Poisoned(T v = 0, T s = (T)(-1)) {
  __msan_partial_poison(&v, &s, sizeof(T));
  return v;
}

template<class T> NOINLINE T ReturnPoisoned() { return *GetPoisoned<T>(); }

static volatile int g_one = 1;
static volatile int g_zero = 0;
static volatile int g_0 = 0;
static volatile int g_1 = 1;

S4 a_s4[100];
S8 a_s8[100];

// Check that malloc poisons memory.
// A lot of tests below depend on this.
TEST(MemorySanitizerSanity, PoisonInMalloc) {
  int *x = (int*)malloc(sizeof(int));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, NegativeTest1) {
  S4 *x = GetPoisoned<S4>();
  if (g_one)
    *x = 0;
  EXPECT_NOT_POISONED(*x);
}

TEST(MemorySanitizer, PositiveTest1) {
  // Load to store.
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S8>());

  // S->S conversions.
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S1>());
  EXPECT_POISONED(*GetPoisoned<S1>());

  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S2>());
  EXPECT_POISONED(*GetPoisoned<S2>());

  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S4>());
  EXPECT_POISONED(*GetPoisoned<S4>());

  EXPECT_POISONED(*GetPoisoned<S8>());
  EXPECT_POISONED(*GetPoisoned<S8>());
  EXPECT_POISONED(*GetPoisoned<S8>());

  // ZExt
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U1>());
  EXPECT_POISONED(*GetPoisoned<U2>());
  EXPECT_POISONED(*GetPoisoned<U2>());
  EXPECT_POISONED(*GetPoisoned<U4>());

  // Unary ops.
  EXPECT_POISONED(- *GetPoisoned<S4>());

  EXPECT_UMR(a_s4[g_zero] = 100 / *GetPoisoned<S4>(0, 1));


  a_s4[g_zero] = 1 - *GetPoisoned<S4>();
  a_s4[g_zero] = 1 + *GetPoisoned<S4>();
}

TEST(MemorySanitizer, Phi1) {
  S4 c;
  if (g_one) {
    c = *GetPoisoned<S4>();
  } else {
    break_optimization(0);
    c = 0;
  }
  EXPECT_POISONED(c);
}

TEST(MemorySanitizer, Phi2) {
  S4 i = *GetPoisoned<S4>();
  S4 n = g_one;
  EXPECT_UMR(for (; i < g_one; i++););
  EXPECT_POISONED(i);
}

NOINLINE void Arg1ExpectUMR(S4 a1) { EXPECT_POISONED(a1); }
NOINLINE void Arg2ExpectUMR(S4 a1, S4 a2) { EXPECT_POISONED(a2); }
NOINLINE void Arg3ExpectUMR(S1 a1, S4 a2, S8 a3) { EXPECT_POISONED(a3); }

TEST(MemorySanitizer, ArgTest) {
  Arg1ExpectUMR(*GetPoisoned<S4>());
  Arg2ExpectUMR(0, *GetPoisoned<S4>());
  Arg3ExpectUMR(0, 1, *GetPoisoned<S8>());
}


TEST(MemorySanitizer, CallAndRet) {
  ReturnPoisoned<S1>();
  ReturnPoisoned<S2>();
  ReturnPoisoned<S4>();
  ReturnPoisoned<S8>();

  EXPECT_POISONED(ReturnPoisoned<S1>());
  EXPECT_POISONED(ReturnPoisoned<S2>());
  EXPECT_POISONED(ReturnPoisoned<S4>());
  EXPECT_POISONED(ReturnPoisoned<S8>());
}

// malloc() in the following test may be optimized to produce a compile-time
// undef value. Check that we trap on the volatile assignment anyway.
TEST(MemorySanitizer, DISABLED_MallocNoIdent) {
  S4 *x = (int*)malloc(sizeof(S4));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, Malloc) {
  S4 *x = (int*)Ident(malloc(sizeof(S4)));
  EXPECT_POISONED(*x);
  free(x);
}

TEST(MemorySanitizer, Realloc) {
  S4 *x = (int*)Ident(realloc(0, sizeof(S4)));
  EXPECT_POISONED(x[0]);
  x[0] = 1;
  x = (int*)Ident(realloc(x, 2 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[1]);
  x = (int*)Ident(realloc(x, 3 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[2]);
  EXPECT_POISONED(x[1]);
  x[2] = 1;  // Init this here. Check that after realloc it is poisoned again.
  x = (int*)Ident(realloc(x, 2 * sizeof(S4)));
  EXPECT_NOT_POISONED(x[0]);  // Ok, was inited before.
  EXPECT_POISONED(x[1]);
  x = (int*)Ident(realloc(x, 3 * sizeof(S4)));
  EXPECT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  free(x);
}

TEST(MemorySanitizer, Calloc) {
  S4 *x = (int*)Ident(calloc(1, sizeof(S4)));
  EXPECT_NOT_POISONED(*x);  // Should not be poisoned.
  EXPECT_EQ(0, *x);
  free(x);
}

TEST(MemorySanitizer, CallocReturnsZeroMem) {
  size_t sizes[] = {16, 1000, 10000, 100000, 2100000};
  for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
    size_t size = sizes[s];
    for (size_t iter = 0; iter < 5; iter++) {
      char *x = Ident((char*)calloc(1, size));
      EXPECT_EQ(x[0], 0);
      EXPECT_EQ(x[size - 1], 0);
      EXPECT_EQ(x[size / 2], 0);
      EXPECT_EQ(x[size / 3], 0);
      EXPECT_EQ(x[size / 4], 0);
      memset(x, 0x42, size);
      free(Ident(x));
    }
  }
}

TEST(MemorySanitizer, AndOr) {
  U4 *p = GetPoisoned<U4>();
  // We poison two bytes in the midle of a 4-byte word to make the test
  // correct regardless of endianness.
  ((U1*)p)[1] = 0;
  ((U1*)p)[2] = 0xff;
  EXPECT_NOT_POISONED(*p & 0x00ffff00);
  EXPECT_NOT_POISONED(*p & 0x00ff0000);
  EXPECT_NOT_POISONED(*p & 0x0000ff00);
  EXPECT_POISONED(*p & 0xff000000);
  EXPECT_POISONED(*p & 0x000000ff);
  EXPECT_POISONED(*p & 0x0000ffff);
  EXPECT_POISONED(*p & 0xffff0000);

  EXPECT_NOT_POISONED(*p | 0xff0000ff);
  EXPECT_NOT_POISONED(*p | 0xff00ffff);
  EXPECT_NOT_POISONED(*p | 0xffff00ff);
  EXPECT_POISONED(*p | 0xff000000);
  EXPECT_POISONED(*p | 0x000000ff);
  EXPECT_POISONED(*p | 0x0000ffff);
  EXPECT_POISONED(*p | 0xffff0000);

  EXPECT_POISONED((int)*GetPoisoned<bool>() & (int)*GetPoisoned<bool>());
}

template<class T>
static bool applyNot(T value, T shadow) {
  __msan_partial_poison(&value, &shadow, sizeof(T));
  return !value;
}

TEST(MemorySanitizer, Not) {
  EXPECT_NOT_POISONED(applyNot<U4>(0x0, 0x0));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFFFFFFFF, 0x0));
  EXPECT_POISONED(applyNot<U4>(0xFFFFFFFF, 0xFFFFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x0FFFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x00FFFFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x0000FFFF));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF000000, 0x00000000));
  EXPECT_POISONED(applyNot<U4>(0xFF000000, 0xFF000000));
  EXPECT_NOT_POISONED(applyNot<U4>(0xFF800000, 0xFF000000));
  EXPECT_POISONED(applyNot<U4>(0x00008000, 0x00008000));

  EXPECT_NOT_POISONED(applyNot<U1>(0x0, 0x0));
  EXPECT_NOT_POISONED(applyNot<U1>(0xFF, 0xFE));
  EXPECT_NOT_POISONED(applyNot<U1>(0xFF, 0x0));
  EXPECT_POISONED(applyNot<U1>(0xFF, 0xFF));

  EXPECT_POISONED(applyNot<void*>((void*)0xFFFFFF, (void*)(-1)));
  EXPECT_NOT_POISONED(applyNot<void*>((void*)0xFFFFFF, (void*)(-2)));
}

TEST(MemorySanitizer, Shift) {
  U4 *up = GetPoisoned<U4>();
  ((U1*)up)[0] = 0;
  ((U1*)up)[3] = 0xff;
  EXPECT_NOT_POISONED(*up >> 30);
  EXPECT_NOT_POISONED(*up >> 24);
  EXPECT_POISONED(*up >> 23);
  EXPECT_POISONED(*up >> 10);

  EXPECT_NOT_POISONED(*up << 30);
  EXPECT_NOT_POISONED(*up << 24);
  EXPECT_POISONED(*up << 23);
  EXPECT_POISONED(*up << 10);

  S4 *sp = (S4*)up;
  EXPECT_NOT_POISONED(*sp >> 30);
  EXPECT_NOT_POISONED(*sp >> 24);
  EXPECT_POISONED(*sp >> 23);
  EXPECT_POISONED(*sp >> 10);

  sp = GetPoisoned<S4>();
  ((S1*)sp)[1] = 0;
  ((S1*)sp)[2] = 0;
  EXPECT_POISONED(*sp >> 31);

  EXPECT_POISONED(100 >> *GetPoisoned<S4>());
  EXPECT_POISONED(100U >> *GetPoisoned<S4>());
}

NOINLINE static int GetPoisonedZero() {
  int *zero = new int;
  *zero = 0;
  __msan_poison(zero, sizeof(*zero));
  int res = *zero;
  delete zero;
  return res;
}

TEST(MemorySanitizer, LoadFromDirtyAddress) {
  int *a = new int;
  *a = 0;
  EXPECT_UMR(break_optimization((void*)(U8)a[GetPoisonedZero()]));
  delete a;
}

TEST(MemorySanitizer, StoreToDirtyAddress) {
  int *a = new int;
  EXPECT_UMR(a[GetPoisonedZero()] = 0);
  break_optimization(a);
  delete a;
}


NOINLINE void StackTestFunc() {
  S4 p4;
  S4 ok4 = 1;
  S2 p2;
  S2 ok2 = 1;
  S1 p1;
  S1 ok1 = 1;
  break_optimization(&p4);
  break_optimization(&ok4);
  break_optimization(&p2);
  break_optimization(&ok2);
  break_optimization(&p1);
  break_optimization(&ok1);

  EXPECT_POISONED(p4);
  EXPECT_POISONED(p2);
  EXPECT_POISONED(p1);
  EXPECT_NOT_POISONED(ok1);
  EXPECT_NOT_POISONED(ok2);
  EXPECT_NOT_POISONED(ok4);
}

TEST(MemorySanitizer, StackTest) {
  StackTestFunc();
}

NOINLINE void StackStressFunc() {
  int foo[10000];
  break_optimization(foo);
}

TEST(MemorySanitizer, DISABLED_StackStressTest) {
  for (int i = 0; i < 1000000; i++)
    StackStressFunc();
}

template<class T>
void TestFloatingPoint() {
  static volatile T v;
  static T g[100];
  break_optimization(&g);
  T *x = GetPoisoned<T>();
  T *y = GetPoisoned<T>(1);
  EXPECT_POISONED(*x);
  EXPECT_POISONED((long long)*x);
  EXPECT_POISONED((int)*x);
  g[0] = *x;
  g[1] = *x + *y;
  g[2] = *x - *y;
  g[3] = *x * *y;
}

TEST(MemorySanitizer, FloatingPointTest) {
  TestFloatingPoint<float>();
  TestFloatingPoint<double>();
}

TEST(MemorySanitizer, DynMem) {
  S4 x = 0;
  S4 *y = GetPoisoned<S4>();
  memcpy(y, &x, g_one * sizeof(S4));
  EXPECT_NOT_POISONED(*y);
}

static char *DynRetTestStr;

TEST(MemorySanitizer, DynRet) {
  ReturnPoisoned<S8>();
  EXPECT_NOT_POISONED(atoi("0"));
}

TEST(MemorySanitizer, DynRet1) {
  ReturnPoisoned<S8>();
}

struct LargeStruct {
  S4 x[10];
};

NOINLINE
LargeStruct LargeRetTest() {
  LargeStruct res;
  res.x[0] = *GetPoisoned<S4>();
  res.x[1] = *GetPoisoned<S4>();
  res.x[2] = *GetPoisoned<S4>();
  res.x[3] = *GetPoisoned<S4>();
  res.x[4] = *GetPoisoned<S4>();
  res.x[5] = *GetPoisoned<S4>();
  res.x[6] = *GetPoisoned<S4>();
  res.x[7] = *GetPoisoned<S4>();
  res.x[8] = *GetPoisoned<S4>();
  res.x[9] = *GetPoisoned<S4>();
  return res;
}

TEST(MemorySanitizer, LargeRet) {
  LargeStruct a = LargeRetTest();
  EXPECT_POISONED(a.x[0]);
  EXPECT_POISONED(a.x[9]);
}

TEST(MemorySanitizer, strerror) {
  char *buf = strerror(EINVAL);
  EXPECT_NOT_POISONED(strlen(buf));
  buf = strerror(123456);
  EXPECT_NOT_POISONED(strlen(buf));
}

TEST(MemorySanitizer, strerror_r) {
  errno = 0;
  char buf[1000];
  char *res = (char*) (size_t) strerror_r(EINVAL, buf, sizeof(buf));
  ASSERT_EQ(0, errno);
  if (!res) res = buf; // POSIX version success.
  EXPECT_NOT_POISONED(strlen(res));
}

TEST(MemorySanitizer, fread) {
  char *x = new char[32];
  FILE *f = fopen(FILE_TO_READ, "r");
  ASSERT_TRUE(f != NULL);
  fread(x, 1, 32, f);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  fclose(f);
  delete[] x;
}

TEST(MemorySanitizer, read) {
  char *x = new char[32];
  int fd = open(FILE_TO_READ, O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = read(fd, x, 32);
  ASSERT_EQ(sz, 32);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  close(fd);
  delete[] x;
}

TEST(MemorySanitizer, pread) {
  char *x = new char[32];
  int fd = open(FILE_TO_READ, O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = pread(fd, x, 32, 0);
  ASSERT_EQ(sz, 32);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[16]);
  EXPECT_NOT_POISONED(x[31]);
  close(fd);
  delete[] x;
}

TEST(MemorySanitizer, readv) {
  char buf[2011];
  struct iovec iov[2];
  iov[0].iov_base = buf + 1;
  iov[0].iov_len = 5;
  iov[1].iov_base = buf + 10;
  iov[1].iov_len = 2000;
  int fd = open(FILE_TO_READ, O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = readv(fd, iov, 2);
  ASSERT_GE(sz, 0);
  ASSERT_LE(sz, 5 + 2000);
  ASSERT_GT((size_t)sz, iov[0].iov_len);
  EXPECT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[1]);
  EXPECT_NOT_POISONED(buf[5]);
  EXPECT_POISONED(buf[6]);
  EXPECT_POISONED(buf[9]);
  EXPECT_NOT_POISONED(buf[10]);
  EXPECT_NOT_POISONED(buf[10 + (sz - 1) - 5]);
  EXPECT_POISONED(buf[11 + (sz - 1) - 5]);
  close(fd);
}

TEST(MemorySanitizer, preadv) {
  char buf[2011];
  struct iovec iov[2];
  iov[0].iov_base = buf + 1;
  iov[0].iov_len = 5;
  iov[1].iov_base = buf + 10;
  iov[1].iov_len = 2000;
  int fd = open(FILE_TO_READ, O_RDONLY);
  ASSERT_GT(fd, 0);
  int sz = preadv(fd, iov, 2, 3);
  ASSERT_GE(sz, 0);
  ASSERT_LE(sz, 5 + 2000);
  ASSERT_GT((size_t)sz, iov[0].iov_len);
  EXPECT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[1]);
  EXPECT_NOT_POISONED(buf[5]);
  EXPECT_POISONED(buf[6]);
  EXPECT_POISONED(buf[9]);
  EXPECT_NOT_POISONED(buf[10]);
  EXPECT_NOT_POISONED(buf[10 + (sz - 1) - 5]);
  EXPECT_POISONED(buf[11 + (sz - 1) - 5]);
  close(fd);
}

// FIXME: fails now.
TEST(MemorySanitizer, DISABLED_ioctl) {
  struct winsize ws;
  EXPECT_EQ(ioctl(2, TIOCGWINSZ, &ws), 0);
  EXPECT_NOT_POISONED(ws.ws_col);
}

TEST(MemorySanitizer, readlink) {
  char *x = new char[1000];
  readlink(SYMLINK_TO_READ, x, 1000);
  EXPECT_NOT_POISONED(x[0]);
  delete [] x;
}

TEST(MemorySanitizer, readlinkat) {
  char *x = new char[1000];
  readlinkat(AT_FDCWD, SYMLINK_TO_READ, x, 1000);
  EXPECT_NOT_POISONED(x[0]);
  delete[] x;
}

TEST(MemorySanitizer, stat) {
  struct stat* st = new struct stat;
  int res = stat(FILE_TO_READ, st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st->st_dev);
  EXPECT_NOT_POISONED(st->st_mode);
  EXPECT_NOT_POISONED(st->st_size);
}

TEST(MemorySanitizer, fstatat) {
  struct stat* st = new struct stat;
  int dirfd = open(DIR_TO_READ, O_RDONLY);
  ASSERT_GT(dirfd, 0);
  int res = fstatat(dirfd, SUBFILE_TO_READ, st, 0);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st->st_dev);
  EXPECT_NOT_POISONED(st->st_mode);
  EXPECT_NOT_POISONED(st->st_size);
  close(dirfd);
}

#if !defined(__NetBSD__)
TEST(MemorySanitizer, statfs) {
  struct statfs st;
  int res = statfs("/", &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_type);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namelen);
}
#endif

TEST(MemorySanitizer, statvfs) {
  struct statvfs st;
  int res = statvfs("/", &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_bsize);
  EXPECT_NOT_POISONED(st.f_blocks);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namemax);
}

TEST(MemorySanitizer, fstatvfs) {
  struct statvfs st;
  int fd = open("/", O_RDONLY | O_DIRECTORY);
  int res = fstatvfs(fd, &st);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(st.f_bsize);
  EXPECT_NOT_POISONED(st.f_blocks);
  EXPECT_NOT_POISONED(st.f_bfree);
  EXPECT_NOT_POISONED(st.f_namemax);
  close(fd);
}

TEST(MemorySanitizer, pipe) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pipefd[0]);
  EXPECT_NOT_POISONED(pipefd[1]);
  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, pipe2) {
  int* pipefd = new int[2];
  int res = pipe2(pipefd, O_NONBLOCK);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pipefd[0]);
  EXPECT_NOT_POISONED(pipefd[1]);
  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, socketpair) {
  int sv[2];
  int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(sv[0]);
  EXPECT_NOT_POISONED(sv[1]);
  close(sv[0]);
  close(sv[1]);
}

TEST(MemorySanitizer, poll) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  char data = 42;
  res = write(pipefd[1], &data, 1);
  ASSERT_EQ(1, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipefd[1];
  fds[1].events = POLLIN;
  res = poll(fds, 2, 500);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(fds[0].revents);
  EXPECT_NOT_POISONED(fds[1].revents);

  close(pipefd[0]);
  close(pipefd[1]);
}

#if !defined (__FreeBSD__) && !defined (__NetBSD__)
TEST(MemorySanitizer, ppoll) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  char data = 42;
  res = write(pipefd[1], &data, 1);
  ASSERT_EQ(1, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  fds[1].fd = pipefd[1];
  fds[1].events = POLLIN;
  sigset_t ss;
  sigemptyset(&ss);
  res = ppoll(fds, 2, NULL, &ss);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(fds[0].revents);
  EXPECT_NOT_POISONED(fds[1].revents);

  close(pipefd[0]);
  close(pipefd[1]);
}
#endif

TEST(MemorySanitizer, poll_positive) {
  int* pipefd = new int[2];
  int res = pipe(pipefd);
  ASSERT_EQ(0, res);

  pollfd fds[2];
  fds[0].fd = pipefd[0];
  fds[0].events = POLLIN;
  // fds[1].fd uninitialized
  fds[1].events = POLLIN;
  EXPECT_UMR(poll(fds, 2, 0));

  close(pipefd[0]);
  close(pipefd[1]);
}

TEST(MemorySanitizer, bind_getsockname) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);

  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_UNIX;
  int res = bind(sock, (struct sockaddr *)&sai, sizeof(sai));

  ASSERT_EQ(0, res);
  char buf[200];
  socklen_t addrlen;
  EXPECT_UMR(getsockname(sock, (struct sockaddr *)&buf, &addrlen));

  addrlen = sizeof(buf);
  res = getsockname(sock, (struct sockaddr *)&buf, &addrlen);
  EXPECT_NOT_POISONED(addrlen);
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[addrlen - 1]);
  EXPECT_POISONED(buf[addrlen]);
  close(sock);
}

class SocketAddr {
 public:
  virtual ~SocketAddr() = default;
  virtual struct sockaddr *ptr() = 0;
  virtual size_t size() const = 0;

  template <class... Args>
  static std::unique_ptr<SocketAddr> Create(int family, Args... args);
};

class SocketAddr4 : public SocketAddr {
 public:
  SocketAddr4() { EXPECT_POISONED(sai_); }
  explicit SocketAddr4(uint16_t port) {
    memset(&sai_, 0, sizeof(sai_));
    sai_.sin_family = AF_INET;
    sai_.sin_port = port;
    sai_.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  sockaddr *ptr() override { return reinterpret_cast<sockaddr *>(&sai_); }

  size_t size() const override { return sizeof(sai_); }

 private:
  sockaddr_in sai_;
};

class SocketAddr6 : public SocketAddr {
 public:
  SocketAddr6() { EXPECT_POISONED(sai_); }
  explicit SocketAddr6(uint16_t port) {
    memset(&sai_, 0, sizeof(sai_));
    sai_.sin6_family = AF_INET6;
    sai_.sin6_port = port;
    sai_.sin6_addr = in6addr_loopback;
  }

  sockaddr *ptr() override { return reinterpret_cast<sockaddr *>(&sai_); }

  size_t size() const override { return sizeof(sai_); }

 private:
  sockaddr_in6 sai_;
};

template <class... Args>
std::unique_ptr<SocketAddr> SocketAddr::Create(int family, Args... args) {
  if (family == AF_INET)
    return std::unique_ptr<SocketAddr>(new SocketAddr4(args...));
  return std::unique_ptr<SocketAddr>(new SocketAddr6(args...));
}

class MemorySanitizerIpTest : public ::testing::TestWithParam<int> {
 public:
  void SetUp() override {
    ASSERT_TRUE(GetParam() == AF_INET || GetParam() == AF_INET6);
  }

  template <class... Args>
  std::unique_ptr<SocketAddr> CreateSockAddr(Args... args) const {
    return SocketAddr::Create(GetParam(), args...);
  }

  int CreateSocket(int socket_type) const {
    return socket(GetParam(), socket_type, 0);
  }
};

std::vector<int> GetAvailableIpSocketFamilies() {
  std::vector<int> result;

  for (int i : {AF_INET, AF_INET6}) {
    int s = socket(i, SOCK_STREAM, 0);
    if (s > 0) {
      auto sai = SocketAddr::Create(i, 0);
      if (bind(s, sai->ptr(), sai->size()) == 0) result.push_back(i);
      close(s);
    }
  }

  return result;
}

INSTANTIATE_TEST_SUITE_P(IpTests, MemorySanitizerIpTest,
                         ::testing::ValuesIn(GetAvailableIpSocketFamilies()));

TEST_P(MemorySanitizerIpTest, accept) {
  int listen_socket = CreateSocket(SOCK_STREAM);
  ASSERT_LT(0, listen_socket);

  auto sai = CreateSockAddr(0);
  int res = bind(listen_socket, sai->ptr(), sai->size());
  ASSERT_EQ(0, res);

  res = listen(listen_socket, 1);
  ASSERT_EQ(0, res);

  socklen_t sz = sai->size();
  res = getsockname(listen_socket, sai->ptr(), &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sai->size(), sz);

  int connect_socket = CreateSocket(SOCK_STREAM);
  ASSERT_LT(0, connect_socket);
  res = fcntl(connect_socket, F_SETFL, O_NONBLOCK);
  ASSERT_EQ(0, res);
  res = connect(connect_socket, sai->ptr(), sai->size());
  // On FreeBSD this connection completes immediately.
  if (res != 0) {
    ASSERT_EQ(-1, res);
    ASSERT_EQ(EINPROGRESS, errno);
  }

  __msan_poison(sai->ptr(), sai->size());
  int new_sock = accept(listen_socket, sai->ptr(), &sz);
  ASSERT_LT(0, new_sock);
  ASSERT_EQ(sai->size(), sz);
  EXPECT_NOT_POISONED2(sai->ptr(), sai->size());

  __msan_poison(sai->ptr(), sai->size());
  res = getpeername(new_sock, sai->ptr(), &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sai->size(), sz);
  EXPECT_NOT_POISONED2(sai->ptr(), sai->size());

  close(new_sock);
  close(connect_socket);
  close(listen_socket);
}

TEST_P(MemorySanitizerIpTest, recvmsg) {
  int server_socket = CreateSocket(SOCK_DGRAM);
  ASSERT_LT(0, server_socket);

  auto sai = CreateSockAddr(0);
  int res = bind(server_socket, sai->ptr(), sai->size());
  ASSERT_EQ(0, res);

  socklen_t sz = sai->size();
  res = getsockname(server_socket, sai->ptr(), &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sai->size(), sz);

  int client_socket = CreateSocket(SOCK_DGRAM);
  ASSERT_LT(0, client_socket);

  auto client_sai = CreateSockAddr(0);
  res = bind(client_socket, client_sai->ptr(), client_sai->size());
  ASSERT_EQ(0, res);

  sz = client_sai->size();
  res = getsockname(client_socket, client_sai->ptr(), &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(client_sai->size(), sz);

  const char *s = "message text";
  struct iovec iov;
  iov.iov_base = (void *)s;
  iov.iov_len = strlen(s) + 1;
  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = sai->ptr();
  msg.msg_namelen = sai->size();
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  res = sendmsg(client_socket, &msg, 0);
  ASSERT_LT(0, res);

  char buf[1000];
  struct iovec recv_iov;
  recv_iov.iov_base = (void *)&buf;
  recv_iov.iov_len = sizeof(buf);
  auto recv_sai = CreateSockAddr();
  struct msghdr recv_msg;
  memset(&recv_msg, 0, sizeof(recv_msg));
  recv_msg.msg_name = recv_sai->ptr();
  recv_msg.msg_namelen = recv_sai->size();
  recv_msg.msg_iov = &recv_iov;
  recv_msg.msg_iovlen = 1;
  res = recvmsg(server_socket, &recv_msg, 0);
  ASSERT_LT(0, res);

  ASSERT_EQ(recv_sai->size(), recv_msg.msg_namelen);
  EXPECT_NOT_POISONED2(recv_sai->ptr(), recv_sai->size());
  EXPECT_STREQ(s, buf);

  close(server_socket);
  close(client_socket);
}

#define EXPECT_HOSTENT_NOT_POISONED(he)        \
  do {                                         \
    EXPECT_NOT_POISONED(*(he));                \
    ASSERT_NE((void *)0, (he)->h_name);        \
    ASSERT_NE((void *)0, (he)->h_aliases);     \
    ASSERT_NE((void *)0, (he)->h_addr_list);   \
    EXPECT_NOT_POISONED(strlen((he)->h_name)); \
    char **p = (he)->h_aliases;                \
    while (*p) {                               \
      EXPECT_NOT_POISONED(strlen(*p));         \
      ++p;                                     \
    }                                          \
    char **q = (he)->h_addr_list;              \
    while (*q) {                               \
      EXPECT_NOT_POISONED(*q[0]);              \
      ++q;                                     \
    }                                          \
    EXPECT_NOT_POISONED(*q);                   \
  } while (0)

TEST(MemorySanitizer, gethostent) {
  sethostent(0);
  struct hostent *he = gethostent();
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

#ifndef MSAN_TEST_DISABLE_GETHOSTBYNAME

TEST(MemorySanitizer, gethostbyname) {
  struct hostent *he = gethostbyname("localhost");
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

#endif  // MSAN_TEST_DISABLE_GETHOSTBYNAME

TEST(MemorySanitizer, getaddrinfo) {
  struct addrinfo *ai;
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  int res = getaddrinfo("localhost", NULL, &hints, &ai);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(*ai);
  ASSERT_EQ(sizeof(sockaddr_in), ai->ai_addrlen);
  EXPECT_NOT_POISONED(*(sockaddr_in *)ai->ai_addr);
}

TEST(MemorySanitizer, getnameinfo) {
  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(sai));
  sai.sin_family = AF_INET;
  sai.sin_port = 80;
  sai.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  char host[500];
  char serv[500];
  int res = getnameinfo((struct sockaddr *)&sai, sizeof(sai), host,
                        sizeof(host), serv, sizeof(serv), 0);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(host[0]);
  EXPECT_POISONED(host[sizeof(host) - 1]);

  ASSERT_NE(0U, strlen(host));
  EXPECT_NOT_POISONED(serv[0]);
  EXPECT_POISONED(serv[sizeof(serv) - 1]);
  ASSERT_NE(0U, strlen(serv));
}

TEST(MemorySanitizer, gethostbyname2) {
  struct hostent *he = gethostbyname2("localhost", AF_INET);
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

TEST(MemorySanitizer, gethostbyaddr) {
  in_addr_t addr = inet_addr("127.0.0.1");
  EXPECT_NOT_POISONED(addr);
  struct hostent *he = gethostbyaddr(&addr, sizeof(addr), AF_INET);
  ASSERT_NE((void *)NULL, he);
  EXPECT_HOSTENT_NOT_POISONED(he);
}

#if defined(__GLIBC__) || defined(__FreeBSD__)
TEST(MemorySanitizer, gethostent_r) {
  sethostent(0);
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostent_r(&he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, gethostbyname_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname_r("localhost", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, gethostbyname_r_bad_host_name) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname_r("bad-host-name", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ((struct hostent *)0, result);
  EXPECT_NOT_POISONED(err);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, gethostbyname_r_erange) {
  char buf[5];
  struct hostent he;
  struct hostent *result;
  int err;
  gethostbyname_r("localhost", &he, buf, sizeof(buf), &result, &err);
  ASSERT_EQ(ERANGE, errno);
  EXPECT_NOT_POISONED(err);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, gethostbyname2_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  int res = gethostbyname2_r("localhost", AF_INET, &he, buf, sizeof(buf),
                             &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, gethostbyaddr_r) {
  char buf[2000];
  struct hostent he;
  struct hostent *result;
  int err;
  in_addr_t addr = inet_addr("127.0.0.1");
  EXPECT_NOT_POISONED(addr);
  int res = gethostbyaddr_r(&addr, sizeof(addr), AF_INET, &he, buf, sizeof(buf),
                            &result, &err);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(result);
  ASSERT_NE((void *)NULL, result);
  EXPECT_HOSTENT_NOT_POISONED(result);
  EXPECT_NOT_POISONED(err);
}
#endif

TEST(MemorySanitizer, getsockopt) {
  int sock = socket(AF_UNIX, SOCK_STREAM, 0);
  struct linger l[2];
  socklen_t sz = sizeof(l[0]);
  int res = getsockopt(sock, SOL_SOCKET, SO_LINGER, &l[0], &sz);
  ASSERT_EQ(0, res);
  ASSERT_EQ(sizeof(l[0]), sz);
  EXPECT_NOT_POISONED(l[0]);
  EXPECT_POISONED(*(char *)(l + 1));
}

TEST(MemorySanitizer, getcwd) {
  char path[PATH_MAX + 1];
  char* res = getcwd(path, sizeof(path));
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(path[0]);
}

TEST(MemorySanitizer, getcwd_gnu) {
  char* res = getcwd(NULL, 0);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, get_current_dir_name) {
  char* res = get_current_dir_name();
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}
#endif

TEST(MemorySanitizer, shmctl) {
  int id = shmget(IPC_PRIVATE, 4096, 0644 | IPC_CREAT);
  ASSERT_GT(id, -1);

  struct shmid_ds ds;
  int res = shmctl(id, IPC_STAT, &ds);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(ds);

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
  struct shminfo si;
  res = shmctl(id, IPC_INFO, (struct shmid_ds *)&si);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(si);

  struct shm_info s_i;
  res = shmctl(id, SHM_INFO, (struct shmid_ds *)&s_i);
  ASSERT_GT(res, -1);
  EXPECT_NOT_POISONED(s_i);
#endif

  res = shmctl(id, IPC_RMID, 0);
  ASSERT_GT(res, -1);
}

TEST(MemorySanitizer, shmat) {
  const int kShmSize = 4096;
  void *mapping_start = mmap(NULL, kShmSize + SHMLBA, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(MAP_FAILED, mapping_start);

  void *p = (void *)(((unsigned long)mapping_start + SHMLBA - 1) / SHMLBA * SHMLBA);
  // p is now SHMLBA-aligned;

  ((char *)p)[10] = *GetPoisoned<U1>();
  ((char *)p)[kShmSize - 1] = *GetPoisoned<U1>();

  int res = munmap(mapping_start, kShmSize + SHMLBA);
  ASSERT_EQ(0, res);

  int id = shmget(IPC_PRIVATE, kShmSize, 0644 | IPC_CREAT);
  ASSERT_GT(id, -1);

  void *q = shmat(id, p, 0);
  ASSERT_EQ(p, q);

  EXPECT_NOT_POISONED(((char *)q)[0]);
  EXPECT_NOT_POISONED(((char *)q)[10]);
  EXPECT_NOT_POISONED(((char *)q)[kShmSize - 1]);

  res = shmdt(q);
  ASSERT_EQ(0, res);

  res = shmctl(id, IPC_RMID, 0);
  ASSERT_GT(res, -1);
}

#ifdef __GLIBC__
TEST(MemorySanitizer, random_r) {
  int32_t x;
  char z[64];
  memset(z, 0, sizeof(z));

  struct random_data buf;
  memset(&buf, 0, sizeof(buf));

  int res = initstate_r(0, z, sizeof(z), &buf);
  ASSERT_EQ(0, res);

  res = random_r(&buf, &x);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(x);
}
#endif

TEST(MemorySanitizer, confstr) {
  char buf[3];
  size_t res = confstr(_CS_PATH, buf, sizeof(buf));
  ASSERT_GT(res, sizeof(buf));
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[sizeof(buf) - 1]);

  char buf2[1000];
  res = confstr(_CS_PATH, buf2, sizeof(buf2));
  ASSERT_LT(res, sizeof(buf2));
  EXPECT_NOT_POISONED(buf2[0]);
  EXPECT_NOT_POISONED(buf2[res - 1]);
  EXPECT_POISONED(buf2[res]);
  ASSERT_EQ(res, strlen(buf2) + 1);
}

TEST(MemorySanitizer, opendir) {
  DIR *dir = opendir(".");
  closedir(dir);

  char name[10] = ".";
  __msan_poison(name, sizeof(name));
  EXPECT_UMR(dir = opendir(name));
  closedir(dir);
}

TEST(MemorySanitizer, readdir) {
  DIR *dir = opendir(".");
  struct dirent *d = readdir(dir);
  ASSERT_TRUE(d != NULL);
  EXPECT_NOT_POISONED(d->d_name[0]);
  closedir(dir);
}

TEST(MemorySanitizer, readdir_r) {
  DIR *dir = opendir(".");
  struct dirent d;
  struct dirent *pd;
  int res = readdir_r(dir, &d, &pd);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pd);
  EXPECT_NOT_POISONED(d.d_name[0]);
  closedir(dir);
}

TEST(MemorySanitizer, realpath) {
  const char* relpath = ".";
  char path[PATH_MAX + 1];
  char* res = realpath(relpath, path);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(path[0]);
}

TEST(MemorySanitizer, realpath_null) {
  const char* relpath = ".";
  char* res = realpath(relpath, NULL);
  printf("%d, %s\n", errno, strerror(errno));
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}

#ifdef __GLIBC__
TEST(MemorySanitizer, canonicalize_file_name) {
  const char* relpath = ".";
  char* res = canonicalize_file_name(relpath);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(res[0]);
  free(res);
}
#endif

extern char **environ;

TEST(MemorySanitizer, setenv) {
  setenv("AAA", "BBB", 1);
  for (char **envp = environ; *envp; ++envp) {
    EXPECT_NOT_POISONED(*envp);
    EXPECT_NOT_POISONED(*envp[0]);
  }
}

TEST(MemorySanitizer, putenv) {
  char s[] = "AAA=BBB";
  putenv(s);
  for (char **envp = environ; *envp; ++envp) {
    EXPECT_NOT_POISONED(*envp);
    EXPECT_NOT_POISONED(*envp[0]);
  }
}

TEST(MemorySanitizer, memcpy) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  memcpy(y, x, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

void TestUnalignedMemcpy(unsigned left, unsigned right, bool src_is_aligned,
                         bool src_is_poisoned, bool dst_is_poisoned) {
  fprintf(stderr, "%s(%d, %d, %d, %d, %d)\n", __func__, left, right,
          src_is_aligned, src_is_poisoned, dst_is_poisoned);

  const unsigned sz = 20;
  U4 dst_origin, src_origin;
  char *dst = (char *)malloc(sz);
  if (dst_is_poisoned)
    dst_origin = __msan_get_origin(dst);
  else
    memset(dst, 0, sz);

  char *src = (char *)malloc(sz);
  if (src_is_poisoned)
    src_origin = __msan_get_origin(src);
  else
    memset(src, 0, sz);

  memcpy(dst + left, src_is_aligned ? src + left : src, sz - left - right);

  for (unsigned i = 0; i < (left & (~3U)); ++i)
    if (dst_is_poisoned)
      EXPECT_POISONED_O(dst[i], dst_origin);
    else
      EXPECT_NOT_POISONED(dst[i]);

  for (unsigned i = 0; i < (right & (~3U)); ++i)
    if (dst_is_poisoned)
      EXPECT_POISONED_O(dst[sz - i - 1], dst_origin);
    else
      EXPECT_NOT_POISONED(dst[sz - i - 1]);

  for (unsigned i = left; i < sz - right; ++i)
    if (src_is_poisoned)
      EXPECT_POISONED_O(dst[i], src_origin);
    else
      EXPECT_NOT_POISONED(dst[i]);

  free(dst);
  free(src);
}

TEST(MemorySanitizer, memcpy_unaligned) {
  for (int i = 0; i < 10; ++i)
    for (int j = 0; j < 10; ++j)
      for (int aligned = 0; aligned < 2; ++aligned)
        for (int srcp = 0; srcp < 2; ++srcp)
          for (int dstp = 0; dstp < 2; ++dstp)
            TestUnalignedMemcpy(i, j, aligned, srcp, dstp);
}

TEST(MemorySanitizer, memmove) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  memmove(y, x, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

TEST(MemorySanitizer, memccpy_nomatch) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  memccpy(y, x, 'd', 4);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_NOT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
  EXPECT_NOT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_match) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  memccpy(y, x, 'b', 4);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_NOT_POISONED(y[1]);
  EXPECT_POISONED(y[2]);
  EXPECT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_nomatch_positive) {
  char* x = new char[5];
  char* y = new char[5];
  strcpy(x, "abc");
  EXPECT_UMR(memccpy(y, x, 'd', 5));
  break_optimization(y);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, memccpy_match_positive) {
  char* x = new char[5];
  char* y = new char[5];
  x[0] = 'a';
  x[2] = 'b';
  EXPECT_UMR(memccpy(y, x, 'b', 5));
  break_optimization(y);
  delete[] x;
  delete[] y;
}

TEST(MemorySanitizer, bcopy) {
  char* x = new char[2];
  char* y = new char[2];
  x[0] = 1;
  x[1] = *GetPoisoned<char>();
  bcopy(x, y, 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
}

TEST(MemorySanitizer, strdup) {
  char buf[4] = "abc";
  __msan_poison(buf + 2, sizeof(*buf));
  char *x = strdup(buf);
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  EXPECT_NOT_POISONED(x[3]);
  free(x);
}

TEST(MemorySanitizer, strndup) {
  char buf[4] = "abc";
  __msan_poison(buf + 2, sizeof(*buf));
  char *x;
  EXPECT_UMR(x = strndup(buf, 3));
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[1]);
  EXPECT_POISONED(x[2]);
  EXPECT_NOT_POISONED(x[3]);
  free(x);
  // Check handling of non 0 terminated strings.
  buf[3] = 'z';
  __msan_poison(buf + 3, sizeof(*buf));
  EXPECT_UMR(x = strndup(buf + 3, 1));
  EXPECT_POISONED(x[0]);
  EXPECT_NOT_POISONED(x[1]);
  free(x);
}

TEST(MemorySanitizer, strndup_short) {
  char buf[4] = "abc";
  __msan_poison(buf + 1, sizeof(*buf));
  __msan_poison(buf + 2, sizeof(*buf));
  char *x;
  EXPECT_UMR(x = strndup(buf, 2));
  EXPECT_NOT_POISONED(x[0]);
  EXPECT_POISONED(x[1]);
  EXPECT_NOT_POISONED(x[2]);
  free(x);
}


template<class T, int size>
void TestOverlapMemmove() {
  T *x = new T[size];
  ASSERT_GE(size, 3);
  x[2] = 0;
  memmove(x, x + 1, (size - 1) * sizeof(T));
  EXPECT_NOT_POISONED(x[1]);
  EXPECT_POISONED(x[0]);
  EXPECT_POISONED(x[2]);
  delete [] x;
}

TEST(MemorySanitizer, overlap_memmove) {
  TestOverlapMemmove<U1, 10>();
  TestOverlapMemmove<U1, 1000>();
  TestOverlapMemmove<U8, 4>();
  TestOverlapMemmove<U8, 1000>();
}

TEST(MemorySanitizer, strcpy) {
  char* x = new char[3];
  char* y = new char[3];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = 0;
  strcpy(y, x);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
}

TEST(MemorySanitizer, strncpy) {
  char* x = new char[3];
  char* y = new char[5];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = '\0';
  strncpy(y, x, 4);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
  EXPECT_NOT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
}

TEST(MemorySanitizer, stpcpy) {
  char* x = new char[3];
  char* y = new char[3];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = 0;
  char *res = stpcpy(y, x);
  ASSERT_EQ(res, y + 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
}

TEST(MemorySanitizer, stpncpy) {
  char *x = new char[3];
  char *y = new char[5];
  x[0] = 'a';
  x[1] = *GetPoisoned<char>(1, 1);
  x[2] = '\0';
  char *res = stpncpy(y, x, 4);
  ASSERT_EQ(res, y + 2);
  EXPECT_NOT_POISONED(y[0]);
  EXPECT_POISONED(y[1]);
  EXPECT_NOT_POISONED(y[2]);
  EXPECT_NOT_POISONED(y[3]);
  EXPECT_POISONED(y[4]);
}

TEST(MemorySanitizer, strcat) {
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strcat(a, b);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_NOT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

TEST(MemorySanitizer, strncat) {
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strncat(a, b, 5);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_NOT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

TEST(MemorySanitizer, strncat_overflow) {
  char a[10];
  char b[] = "def";
  strcpy(a, "abc");
  __msan_poison(b + 1, 1);
  strncat(a, b, 2);
  EXPECT_NOT_POISONED(a[3]);
  EXPECT_POISONED(a[4]);
  EXPECT_NOT_POISONED(a[5]);
  EXPECT_POISONED(a[6]);
  EXPECT_POISONED(a[7]);
}

TEST(MemorySanitizer, wcscat) {
  wchar_t a[10];
  wchar_t b[] = L"def";
  wcscpy(a, L"abc");

  wcscat(a, b);
  EXPECT_EQ(6U, wcslen(a));
  EXPECT_POISONED(a[7]);

  a[3] = 0;
  __msan_poison(b + 1, sizeof(wchar_t));
  EXPECT_UMR(wcscat(a, b));

  __msan_unpoison(b + 1, sizeof(wchar_t));
  __msan_poison(a + 2, sizeof(wchar_t));
  EXPECT_UMR(wcscat(a, b));
}

TEST(MemorySanitizer, wcsncat) {
  wchar_t a[10];
  wchar_t b[] = L"def";
  wcscpy(a, L"abc");

  wcsncat(a, b, 5);
  EXPECT_EQ(6U, wcslen(a));
  EXPECT_POISONED(a[7]);

  a[3] = 0;
  __msan_poison(a + 4, sizeof(wchar_t) * 6);
  wcsncat(a, b, 2);
  EXPECT_EQ(5U, wcslen(a));
  EXPECT_POISONED(a[6]);

  a[3] = 0;
  __msan_poison(b + 1, sizeof(wchar_t));
  EXPECT_UMR(wcsncat(a, b, 2));

  __msan_unpoison(b + 1, sizeof(wchar_t));
  __msan_poison(a + 2, sizeof(wchar_t));
  EXPECT_UMR(wcsncat(a, b, 2));
}

#define TEST_STRTO_INT(func_name, char_type, str_prefix) \
  TEST(MemorySanitizer, func_name) {                     \
    char_type *e;                                        \
    EXPECT_EQ(1U, func_name(str_prefix##"1", &e, 10));   \
    EXPECT_NOT_POISONED((S8)e);                          \
  }

#define TEST_STRTO_FLOAT(func_name, char_type, str_prefix) \
  TEST(MemorySanitizer, func_name) {                       \
    char_type *e;                                          \
    EXPECT_NE(0, func_name(str_prefix##"1.5", &e));        \
    EXPECT_NOT_POISONED((S8)e);                            \
  }

#define TEST_STRTO_FLOAT_LOC(func_name, char_type, str_prefix)   \
  TEST(MemorySanitizer, func_name) {                             \
    locale_t loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); \
    char_type *e;                                                \
    EXPECT_NE(0, func_name(str_prefix##"1.5", &e, loc));         \
    EXPECT_NOT_POISONED((S8)e);                                  \
    freelocale(loc);                                             \
  }

#define TEST_STRTO_INT_LOC(func_name, char_type, str_prefix)     \
  TEST(MemorySanitizer, func_name) {                             \
    locale_t loc = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0); \
    char_type *e;                                                \
    ASSERT_EQ(1U, func_name(str_prefix##"1", &e, 10, loc));      \
    EXPECT_NOT_POISONED((S8)e);                                  \
    freelocale(loc);                                             \
  }

TEST_STRTO_INT(strtol, char, )
TEST_STRTO_INT(strtoll, char, )
TEST_STRTO_INT(strtoul, char, )
TEST_STRTO_INT(strtoull, char, )
#ifndef MUSL
TEST_STRTO_INT(strtouq, char, )
#endif

TEST_STRTO_FLOAT(strtof, char, )
TEST_STRTO_FLOAT(strtod, char, )
TEST_STRTO_FLOAT(strtold, char, )

#ifndef MUSL
TEST_STRTO_FLOAT_LOC(strtof_l, char, )
TEST_STRTO_FLOAT_LOC(strtod_l, char, )
TEST_STRTO_FLOAT_LOC(strtold_l, char, )

TEST_STRTO_INT_LOC(strtol_l, char, )
TEST_STRTO_INT_LOC(strtoll_l, char, )
TEST_STRTO_INT_LOC(strtoul_l, char, )
TEST_STRTO_INT_LOC(strtoull_l, char, )
#endif

TEST_STRTO_INT(wcstol, wchar_t, L)
TEST_STRTO_INT(wcstoll, wchar_t, L)
TEST_STRTO_INT(wcstoul, wchar_t, L)
TEST_STRTO_INT(wcstoull, wchar_t, L)

TEST_STRTO_FLOAT(wcstof, wchar_t, L)
TEST_STRTO_FLOAT(wcstod, wchar_t, L)
TEST_STRTO_FLOAT(wcstold, wchar_t, L)

#ifndef MUSL
TEST_STRTO_FLOAT_LOC(wcstof_l, wchar_t, L)
TEST_STRTO_FLOAT_LOC(wcstod_l, wchar_t, L)
TEST_STRTO_FLOAT_LOC(wcstold_l, wchar_t, L)

TEST_STRTO_INT_LOC(wcstol_l, wchar_t, L)
TEST_STRTO_INT_LOC(wcstoll_l, wchar_t, L)
TEST_STRTO_INT_LOC(wcstoul_l, wchar_t, L)
TEST_STRTO_INT_LOC(wcstoull_l, wchar_t, L)
#endif


TEST(MemorySanitizer, strtoimax) {
  char *e;
  ASSERT_EQ(1, strtoimax("1", &e, 10));
  EXPECT_NOT_POISONED((S8) e);
}

TEST(MemorySanitizer, strtoumax) {
  char *e;
  ASSERT_EQ(1U, strtoumax("1", &e, 10));
  EXPECT_NOT_POISONED((S8) e);
}

#ifdef __GLIBC__
extern "C" float __strtof_l(const char *nptr, char **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtof_l, char, )
extern "C" double __strtod_l(const char *nptr, char **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtod_l, char, )
extern "C" long double __strtold_l(const char *nptr, char **endptr,
                                   locale_t loc);
TEST_STRTO_FLOAT_LOC(__strtold_l, char, )

extern "C" float __wcstof_l(const wchar_t *nptr, wchar_t **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__wcstof_l, wchar_t, L)
extern "C" double __wcstod_l(const wchar_t *nptr, wchar_t **endptr, locale_t loc);
TEST_STRTO_FLOAT_LOC(__wcstod_l, wchar_t, L)
extern "C" long double __wcstold_l(const wchar_t *nptr, wchar_t **endptr,
                                   locale_t loc);
TEST_STRTO_FLOAT_LOC(__wcstold_l, wchar_t, L)
#endif  // __GLIBC__

TEST(MemorySanitizer, modf) {
  double y;
  modf(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

TEST(MemorySanitizer, modff) {
  float y;
  modff(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

TEST(MemorySanitizer, modfl) {
  long double y;
  modfl(2.1, &y);
  EXPECT_NOT_POISONED(y);
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, sincos) {
  double s, c;
  sincos(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}
#endif

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, sincosf) {
  float s, c;
  sincosf(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}
#endif

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, sincosl) {
  long double s, c;
  sincosl(0.2, &s, &c);
  EXPECT_NOT_POISONED(s);
  EXPECT_NOT_POISONED(c);
}
#endif

TEST(MemorySanitizer, remquo) {
  int quo;
  double res = remquo(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}

TEST(MemorySanitizer, remquof) {
  int quo;
  float res = remquof(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}

#if !defined(__NetBSD__)
TEST(MemorySanitizer, remquol) {
  int quo;
  long double res = remquof(29.0, 3.0, &quo);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(quo);
}
#endif

TEST(MemorySanitizer, lgamma) {
  double res = lgamma(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}

TEST(MemorySanitizer, lgammaf) {
  float res = lgammaf(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}

#if !defined(__NetBSD__)
TEST(MemorySanitizer, lgammal) {
  long double res = lgammal(1.1);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(signgam);
}
#endif

TEST(MemorySanitizer, lgamma_r) {
  int sgn;
  double res = lgamma_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}

TEST(MemorySanitizer, lgammaf_r) {
  int sgn;
  float res = lgammaf_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, lgammal_r) {
  int sgn;
  long double res = lgammal_r(1.1, &sgn);
  ASSERT_NE(0.0, res);
  EXPECT_NOT_POISONED(sgn);
}
#endif

#ifdef __GLIBC__
TEST(MemorySanitizer, drand48_r) {
  struct drand48_data buf;
  srand48_r(0, &buf);
  double d;
  drand48_r(&buf, &d);
  EXPECT_NOT_POISONED(d);
}

TEST(MemorySanitizer, lrand48_r) {
  struct drand48_data buf;
  srand48_r(0, &buf);
  long d;
  lrand48_r(&buf, &d);
  EXPECT_NOT_POISONED(d);
}
#endif

TEST(MemorySanitizer, sprintf) {
  char buff[10];
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = sprintf(buff, "%d", 1234567);
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], 0);
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, snprintf) {
  char buff[10];
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = snprintf(buff, sizeof(buff), "%d", 1234567);
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], 0);
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, swprintf) {
  wchar_t buff[10];
  ASSERT_EQ(4U, sizeof(wchar_t));
  break_optimization(buff);
  EXPECT_POISONED(buff[0]);
  int res = swprintf(buff, 9, L"%d", 1234567);
  ASSERT_EQ(res, 7);
  ASSERT_EQ(buff[0], '1');
  ASSERT_EQ(buff[1], '2');
  ASSERT_EQ(buff[2], '3');
  ASSERT_EQ(buff[6], '7');
  ASSERT_EQ(buff[7], L'\0');
  EXPECT_POISONED(buff[8]);
}

TEST(MemorySanitizer, asprintf) {
  char *pbuf;
  EXPECT_POISONED(pbuf);
  int res = asprintf(&pbuf, "%d", 1234567);
  ASSERT_EQ(res, 7);
  EXPECT_NOT_POISONED(pbuf);
  ASSERT_EQ(pbuf[0], '1');
  ASSERT_EQ(pbuf[1], '2');
  ASSERT_EQ(pbuf[2], '3');
  ASSERT_EQ(pbuf[6], '7');
  ASSERT_EQ(pbuf[7], 0);
  free(pbuf);
}

TEST(MemorySanitizer, mbstowcs) {
  const char *x = "abc";
  wchar_t buff[10];
  int res = mbstowcs(buff, x, 2);
  EXPECT_EQ(2, res);
  EXPECT_EQ(L'a', buff[0]);
  EXPECT_EQ(L'b', buff[1]);
  EXPECT_POISONED(buff[2]);
  res = mbstowcs(buff, x, 10);
  EXPECT_EQ(3, res);
  EXPECT_NOT_POISONED(buff[3]);
}

TEST(MemorySanitizer, wcstombs) {
  const wchar_t *x = L"abc";
  char buff[10];
  int res = wcstombs(buff, x, 4);
  EXPECT_EQ(res, 3);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_EQ(buff[2], 'c');
}

TEST(MemorySanitizer, wcsrtombs) {
  const wchar_t *x = L"abc";
  const wchar_t *p = x;
  char buff[10];
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  int res = wcsrtombs(buff, &p, 4, &mbs);
  EXPECT_EQ(res, 3);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_EQ(buff[2], 'c');
  EXPECT_EQ(buff[3], '\0');
  EXPECT_POISONED(buff[4]);
}

TEST(MemorySanitizer, wcsnrtombs) {
  const wchar_t *x = L"abc";
  const wchar_t *p = x;
  char buff[10];
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  int res = wcsnrtombs(buff, &p, 2, 4, &mbs);
  EXPECT_EQ(res, 2);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_EQ(buff[1], 'b');
  EXPECT_POISONED(buff[2]);
}

TEST(MemorySanitizer, wcrtomb) {
  wchar_t x = L'a';
  char buff[10];
  mbstate_t mbs;
  memset(&mbs, 0, sizeof(mbs));
  size_t res = wcrtomb(buff, x, &mbs);
  EXPECT_EQ(res, (size_t)1);
  EXPECT_EQ(buff[0], 'a');
}

TEST(MemorySanitizer, wctomb) {
  wchar_t x = L'a';
  char buff[10];
  wctomb(nullptr, x);
  int res = wctomb(buff, x);
  EXPECT_EQ(res, 1);
  EXPECT_EQ(buff[0], 'a');
  EXPECT_POISONED(buff[1]);
}

TEST(MemorySanitizer, wmemset) {
    wchar_t x[25];
    break_optimization(x);
    EXPECT_POISONED(x[0]);
    wmemset(x, L'A', 10);
    EXPECT_EQ(x[0], L'A');
    EXPECT_EQ(x[9], L'A');
    EXPECT_POISONED(x[10]);
}

TEST(MemorySanitizer, mbtowc) {
  const char *x = "abc";
  wchar_t wx;
  int res = mbtowc(&wx, x, 3);
  EXPECT_GT(res, 0);
  EXPECT_NOT_POISONED(wx);
}

TEST(MemorySanitizer, mbrtowc) {
  mbstate_t mbs = {};

  wchar_t wc;
  size_t res = mbrtowc(&wc, "\377", 1, &mbs);
  EXPECT_EQ(res, -1ULL);

  res = mbrtowc(&wc, "abc", 3, &mbs);
  EXPECT_GT(res, 0ULL);
  EXPECT_NOT_POISONED(wc);
}

TEST(MemorySanitizer, wcsftime) {
  wchar_t x[100];
  time_t t = time(NULL);
  struct tm tms;
  struct tm *tmres = localtime_r(&t, &tms);
  ASSERT_NE((void *)0, tmres);
  size_t res = wcsftime(x, sizeof(x) / sizeof(x[0]), L"%Y-%m-%d", tmres);
  EXPECT_GT(res, 0UL);
  EXPECT_EQ(res, wcslen(x));
}

TEST(MemorySanitizer, gettimeofday) {
  struct timeval tv;
  struct timezone tz;
  break_optimization(&tv);
  break_optimization(&tz);
  ASSERT_EQ(16U, sizeof(tv));
  ASSERT_EQ(8U, sizeof(tz));
  EXPECT_POISONED(tv.tv_sec);
  EXPECT_POISONED(tv.tv_usec);
  EXPECT_POISONED(tz.tz_minuteswest);
  EXPECT_POISONED(tz.tz_dsttime);
  ASSERT_EQ(0, gettimeofday(&tv, &tz));
  EXPECT_NOT_POISONED(tv.tv_sec);
  EXPECT_NOT_POISONED(tv.tv_usec);
  EXPECT_NOT_POISONED(tz.tz_minuteswest);
  EXPECT_NOT_POISONED(tz.tz_dsttime);
}

TEST(MemorySanitizer, clock_gettime) {
  struct timespec tp;
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_gettime(CLOCK_REALTIME, &tp));
  EXPECT_NOT_POISONED(tp.tv_sec);
  EXPECT_NOT_POISONED(tp.tv_nsec);
}

TEST(MemorySanitizer, clock_getres) {
  struct timespec tp;
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_getres(CLOCK_REALTIME, 0));
  EXPECT_POISONED(tp.tv_sec);
  EXPECT_POISONED(tp.tv_nsec);
  ASSERT_EQ(0, clock_getres(CLOCK_REALTIME, &tp));
  EXPECT_NOT_POISONED(tp.tv_sec);
  EXPECT_NOT_POISONED(tp.tv_nsec);
}

TEST(MemorySanitizer, getitimer) {
  struct itimerval it1, it2;
  int res;
  EXPECT_POISONED(it1.it_interval.tv_sec);
  EXPECT_POISONED(it1.it_interval.tv_usec);
  EXPECT_POISONED(it1.it_value.tv_sec);
  EXPECT_POISONED(it1.it_value.tv_usec);
  res = getitimer(ITIMER_VIRTUAL, &it1);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(it1.it_interval.tv_sec);
  EXPECT_NOT_POISONED(it1.it_interval.tv_usec);
  EXPECT_NOT_POISONED(it1.it_value.tv_sec);
  EXPECT_NOT_POISONED(it1.it_value.tv_usec);

  it1.it_interval.tv_sec = it1.it_value.tv_sec = 10000;
  it1.it_interval.tv_usec = it1.it_value.tv_usec = 0;

  res = setitimer(ITIMER_VIRTUAL, &it1, &it2);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(it2.it_interval.tv_sec);
  EXPECT_NOT_POISONED(it2.it_interval.tv_usec);
  EXPECT_NOT_POISONED(it2.it_value.tv_sec);
  EXPECT_NOT_POISONED(it2.it_value.tv_usec);

  // Check that old_value can be 0, and disable the timer.
  memset(&it1, 0, sizeof(it1));
  res = setitimer(ITIMER_VIRTUAL, &it1, 0);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, setitimer_null) {
  setitimer(ITIMER_VIRTUAL, 0, 0);
  // Not testing the return value, since it the behaviour seems to differ
  // between libc implementations and POSIX.
  // Should never crash, though.
}

TEST(MemorySanitizer, time) {
  time_t t;
  EXPECT_POISONED(t);
  time_t t2 = time(&t);
  ASSERT_NE(t2, (time_t)-1);
  EXPECT_NOT_POISONED(t);
}

TEST(MemorySanitizer, strptime) {
  struct tm time;
  char *p = strptime("11/1/2013-05:39", "%m/%d/%Y-%H:%M", &time);
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(time.tm_sec);
  EXPECT_NOT_POISONED(time.tm_hour);
  EXPECT_NOT_POISONED(time.tm_year);
}

TEST(MemorySanitizer, localtime) {
  time_t t = 123;
  struct tm *time = localtime(&t);
  ASSERT_TRUE(time != NULL);
  EXPECT_NOT_POISONED(time->tm_sec);
  EXPECT_NOT_POISONED(time->tm_hour);
  EXPECT_NOT_POISONED(time->tm_year);
  EXPECT_NOT_POISONED(time->tm_isdst);
  EXPECT_NE(0U, strlen(time->tm_zone));
}

TEST(MemorySanitizer, localtime_r) {
  time_t t = 123;
  struct tm time;
  struct tm *res = localtime_r(&t, &time);
  ASSERT_TRUE(res != NULL);
  EXPECT_NOT_POISONED(time.tm_sec);
  EXPECT_NOT_POISONED(time.tm_hour);
  EXPECT_NOT_POISONED(time.tm_year);
  EXPECT_NOT_POISONED(time.tm_isdst);
  EXPECT_NE(0U, strlen(time.tm_zone));
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
/* Creates a temporary file with contents similar to /etc/fstab to be used
   with getmntent{_r}.  */
class TempFstabFile {
 public:
   TempFstabFile() : fd (-1) { }
   ~TempFstabFile() {
     if (fd >= 0)
       close (fd);
   }

   bool Create(void) {
     snprintf(tmpfile, sizeof(tmpfile), "/tmp/msan.getmntent.tmp.XXXXXX");

     fd = mkstemp(tmpfile);
     if (fd == -1)
       return false;

     const char entry[] = "/dev/root / ext4 errors=remount-ro 0 1";
     size_t entrylen = sizeof(entry);

     size_t bytesWritten = write(fd, entry, entrylen);
     if (entrylen != bytesWritten)
       return false;

     return true;
   }

   const char* FileName(void) {
     return tmpfile;
   }

 private:
  char tmpfile[128];
  int fd;
};
#endif

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, getmntent) {
  TempFstabFile fstabtmp;
  ASSERT_TRUE(fstabtmp.Create());
  FILE *fp = setmntent(fstabtmp.FileName(), "r");

  struct mntent *mnt = getmntent(fp);
  ASSERT_TRUE(mnt != NULL);
  ASSERT_NE(0U, strlen(mnt->mnt_fsname));
  ASSERT_NE(0U, strlen(mnt->mnt_dir));
  ASSERT_NE(0U, strlen(mnt->mnt_type));
  ASSERT_NE(0U, strlen(mnt->mnt_opts));
  EXPECT_NOT_POISONED(mnt->mnt_freq);
  EXPECT_NOT_POISONED(mnt->mnt_passno);
  fclose(fp);
}
#endif

#ifdef __GLIBC__
TEST(MemorySanitizer, getmntent_r) {
  TempFstabFile fstabtmp;
  ASSERT_TRUE(fstabtmp.Create());
  FILE *fp = setmntent(fstabtmp.FileName(), "r");

  struct mntent mntbuf;
  char buf[1000];
  struct mntent *mnt = getmntent_r(fp, &mntbuf, buf, sizeof(buf));
  ASSERT_TRUE(mnt != NULL);
  ASSERT_NE(0U, strlen(mnt->mnt_fsname));
  ASSERT_NE(0U, strlen(mnt->mnt_dir));
  ASSERT_NE(0U, strlen(mnt->mnt_type));
  ASSERT_NE(0U, strlen(mnt->mnt_opts));
  EXPECT_NOT_POISONED(mnt->mnt_freq);
  EXPECT_NOT_POISONED(mnt->mnt_passno);
  fclose(fp);
}
#endif

#if !defined(__NetBSD__)
TEST(MemorySanitizer, ether) {
  const char *asc = "11:22:33:44:55:66";
  struct ether_addr *paddr = ether_aton(asc);
  EXPECT_NOT_POISONED(*paddr);

  struct ether_addr addr;
  paddr = ether_aton_r(asc, &addr);
  ASSERT_EQ(paddr, &addr);
  EXPECT_NOT_POISONED(addr);

  char *s = ether_ntoa(&addr);
  ASSERT_NE(0U, strlen(s));

  char buf[100];
  s = ether_ntoa_r(&addr, buf);
  ASSERT_EQ(s, buf);
  ASSERT_NE(0U, strlen(buf));
}
#endif

TEST(MemorySanitizer, mmap) {
  const int size = 4096;
  void *p1, *p2;
  p1 = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
  __msan_poison(p1, size);
  munmap(p1, size);
  for (int i = 0; i < 1000; i++) {
    p2 = mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0);
    if (p2 == p1)
      break;
    else
      munmap(p2, size);
  }
  if (p1 == p2) {
    EXPECT_NOT_POISONED(*(char*)p2);
    munmap(p2, size);
  }
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
// FIXME: enable and add ecvt.
// FIXME: check why msandr does nt handle fcvt.
TEST(MemorySanitizer, fcvt) {
  int a, b;
  break_optimization(&a);
  break_optimization(&b);
  EXPECT_POISONED(a);
  EXPECT_POISONED(b);
  char *str = fcvt(12345.6789, 10, &a, &b);
  EXPECT_NOT_POISONED(a);
  EXPECT_NOT_POISONED(b);
  ASSERT_NE(nullptr, str);
  EXPECT_NOT_POISONED(str[0]);
  ASSERT_NE(0U, strlen(str));
}
#endif

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, fcvt_long) {
  int a, b;
  break_optimization(&a);
  break_optimization(&b);
  EXPECT_POISONED(a);
  EXPECT_POISONED(b);
  char *str = fcvt(111111112345.6789, 10, &a, &b);
  EXPECT_NOT_POISONED(a);
  EXPECT_NOT_POISONED(b);
  ASSERT_NE(nullptr, str);
  EXPECT_NOT_POISONED(str[0]);
  ASSERT_NE(0U, strlen(str));
}
#endif

TEST(MemorySanitizer, memchr) {
  char x[10];
  break_optimization(x);
  EXPECT_POISONED(x[0]);
  x[2] = '2';
  void *res;
  EXPECT_UMR(res = memchr(x, '2', 10));
  EXPECT_NOT_POISONED(res);
  x[0] = '0';
  x[1] = '1';
  res = memchr(x, '2', 10);
  EXPECT_EQ(&x[2], res);
  EXPECT_UMR(res = memchr(x, '3', 10));
  EXPECT_NOT_POISONED(res);
}

TEST(MemorySanitizer, memrchr) {
  char x[10];
  break_optimization(x);
  EXPECT_POISONED(x[0]);
  x[9] = '9';
  void *res;
  EXPECT_UMR(res = memrchr(x, '9', 10));
  EXPECT_NOT_POISONED(res);
  x[0] = '0';
  x[1] = '1';
  res = memrchr(x, '0', 2);
  EXPECT_EQ(&x[0], res);
  EXPECT_UMR(res = memrchr(x, '7', 10));
  EXPECT_NOT_POISONED(res);
}

TEST(MemorySanitizer, frexp) {
  int x;
  x = *GetPoisoned<int>();
  double r = frexp(1.1, &x);
  EXPECT_NOT_POISONED(r);
  EXPECT_NOT_POISONED(x);

  x = *GetPoisoned<int>();
  float rf = frexpf(1.1, &x);
  EXPECT_NOT_POISONED(rf);
  EXPECT_NOT_POISONED(x);

  x = *GetPoisoned<int>();
  double rl = frexpl(1.1, &x);
  EXPECT_NOT_POISONED(rl);
  EXPECT_NOT_POISONED(x);
}

namespace {

static int cnt;

void SigactionHandler(int signo, siginfo_t* si, void* uc) {
  ASSERT_EQ(signo, SIGPROF);
  ASSERT_TRUE(si != NULL);
  EXPECT_NOT_POISONED(si->si_errno);
  EXPECT_NOT_POISONED(si->si_pid);
#ifdef _UC_MACHINE_PC
  EXPECT_NOT_POISONED(_UC_MACHINE_PC((ucontext_t*)uc));
#else
# if __linux__
#  if defined(__x86_64__)
  EXPECT_NOT_POISONED(((ucontext_t*)uc)->uc_mcontext.gregs[REG_RIP]);
#  elif defined(__i386__)
  EXPECT_NOT_POISONED(((ucontext_t*)uc)->uc_mcontext.gregs[REG_EIP]);
#  endif
# endif
#endif
  ++cnt;
}

TEST(MemorySanitizer, sigaction) {
  struct sigaction act = {};
  struct sigaction oldact = {};
  struct sigaction origact = {};

  sigaction(SIGPROF, 0, &origact);

  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = &SigactionHandler;
  sigaction(SIGPROF, &act, 0);

  kill(getpid(), SIGPROF);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_DFL;
  sigaction(SIGPROF, &act, 0);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_IGN;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_FALSE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(SIG_DFL, oldact.sa_handler);
  kill(getpid(), SIGPROF);

  act.sa_flags |= SA_SIGINFO;
  act.sa_sigaction = &SigactionHandler;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_FALSE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(SIG_IGN, oldact.sa_handler);
  kill(getpid(), SIGPROF);

  act.sa_flags &= ~SA_SIGINFO;
  act.sa_handler = SIG_DFL;
  sigaction(SIGPROF, &act, &oldact);
  EXPECT_TRUE(oldact.sa_flags & SA_SIGINFO);
  EXPECT_EQ(&SigactionHandler, oldact.sa_sigaction);
  EXPECT_EQ(2, cnt);

  sigaction(SIGPROF, &origact, 0);
}

} // namespace


TEST(MemorySanitizer, sigemptyset) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigemptyset(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigfillset) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigfillset(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigpending) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigpending(&s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, sigprocmask) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = sigprocmask(SIG_BLOCK, 0, &s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

TEST(MemorySanitizer, pthread_sigmask) {
  sigset_t s;
  EXPECT_POISONED(s);
  int res = pthread_sigmask(SIG_BLOCK, 0, &s);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(s);
}

struct StructWithDtor {
  ~StructWithDtor();
};

NOINLINE StructWithDtor::~StructWithDtor() {
  break_optimization(0);
}

TEST(MemorySanitizer, Invoke) {
  StructWithDtor s;  // Will cause the calls to become invokes.
  EXPECT_NOT_POISONED(0);
  EXPECT_POISONED(*GetPoisoned<int>());
  EXPECT_NOT_POISONED(0);
  EXPECT_POISONED(*GetPoisoned<int>());
  EXPECT_POISONED(ReturnPoisoned<S4>());
}

TEST(MemorySanitizer, ptrtoint) {
  // Test that shadow is propagated through pointer-to-integer conversion.
  unsigned char c = 0;
  __msan_poison(&c, 1);
  uintptr_t u = (uintptr_t)c << 8;
  EXPECT_NOT_POISONED(u & 0xFF00FF);
  EXPECT_POISONED(u & 0xFF00);

  break_optimization(&u);
  void* p = (void*)u;

  break_optimization(&p);
  EXPECT_POISONED(p);
  EXPECT_NOT_POISONED(((uintptr_t)p) & 0xFF00FF);
  EXPECT_POISONED(((uintptr_t)p) & 0xFF00);
}

static void vaargsfn2(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  va_end(vl);
}

static void vaargsfn(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  // The following call will overwrite __msan_param_tls.
  // Checks after it test that arg shadow was somehow saved across the call.
  vaargsfn2(1, 2, 3, 4, *GetPoisoned<double>());
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgTest) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn(1, 13, *x, 42, *y);
}

static void vaargsfn_many(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgManyTest) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_many(1, 2, *x, 3, 4, 5, 6, 7, 8, 9, *y);
}

static void vaargsfn_manyfix(int g1, int g2, int g3, int g4, int g5, int g6, int g7, int g8, int g9, ...) {
  va_list vl;
  va_start(vl, g9);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgManyFixTest) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>();
  vaargsfn_manyfix(1, *x, 3, 4, 5, 6, 7, 8, 9, 10, *y);
}

static void vaargsfn_pass2(va_list vl) {
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
}

static void vaargsfn_pass(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_POISONED(va_arg(vl, int));
  vaargsfn_pass2(vl);
  va_end(vl);
}

TEST(MemorySanitizer, VAArgPass) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_pass(1, *x, 2, 3, *y);
}

static void vaargsfn_copy2(va_list vl) {
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
}

static void vaargsfn_copy(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_list vl2;
  va_copy(vl2, vl);
  vaargsfn_copy2(vl2);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgCopy) {
  int* x = GetPoisoned<int>();
  int* y = GetPoisoned<int>(4);
  vaargsfn_copy(1, 2, *x, 3, *y);
}

static void vaargsfn_ptr(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int*));
  EXPECT_POISONED(va_arg(vl, int*));
  EXPECT_NOT_POISONED(va_arg(vl, int*));
  EXPECT_POISONED(va_arg(vl, double*));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgPtr) {
  int** x = GetPoisoned<int*>();
  double** y = GetPoisoned<double*>(8);
  int z;
  vaargsfn_ptr(1, &z, *x, &z, *y);
}

static void vaargsfn_overflow(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, int));

  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, double));

  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));

  EXPECT_NOT_POISONED(va_arg(vl, int));
  EXPECT_NOT_POISONED(va_arg(vl, double));
  EXPECT_NOT_POISONED(va_arg(vl, int*));

  EXPECT_POISONED(va_arg(vl, int));
  EXPECT_POISONED(va_arg(vl, double));
  EXPECT_POISONED(va_arg(vl, int*));

  va_end(vl);
}

TEST(MemorySanitizer, VAArgOverflow) {
  int* x = GetPoisoned<int>();
  double* y = GetPoisoned<double>(8);
  int** p = GetPoisoned<int*>(16);
  int z;
  vaargsfn_overflow(1,
      1, 2, *x, 4, 5, 6,
      1.1, 2.2, 3.3, *y, 5.5, *p, 7.7, 8.8,
      // the following args will overflow for sure
      *x, *y, *p,
      7, 9.9, &z,
      *x, *y, *p);
}

static void vaargsfn_tlsoverwrite2(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  for (int i = 0; i < 20; ++i)
    EXPECT_NOT_POISONED(va_arg(vl, int));
  va_end(vl);
}

static void vaargsfn_tlsoverwrite(int guard, ...) {
  // This call will overwrite TLS contents unless it's backed up somewhere.
  vaargsfn_tlsoverwrite2(2,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42,
      42, 42, 42, 42, 42); // 20x
  va_list vl;
  va_start(vl, guard);
  for (int i = 0; i < 20; ++i)
    EXPECT_POISONED(va_arg(vl, int));
  va_end(vl);
}

TEST(MemorySanitizer, VAArgTLSOverwrite) {
  int* x = GetPoisoned<int>();
  vaargsfn_tlsoverwrite(1,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x,
      *x, *x, *x, *x, *x); // 20x

}

struct StructByVal {
  int a, b, c, d, e, f;
};

static void vaargsfn_structbyval(int guard, ...) {
  va_list vl;
  va_start(vl, guard);
  {
    StructByVal s = va_arg(vl, StructByVal);
    EXPECT_NOT_POISONED(s.a);
    EXPECT_POISONED(s.b);
    EXPECT_NOT_POISONED(s.c);
    EXPECT_POISONED(s.d);
    EXPECT_NOT_POISONED(s.e);
    EXPECT_POISONED(s.f);
  }
  {
    StructByVal s = va_arg(vl, StructByVal);
    EXPECT_NOT_POISONED(s.a);
    EXPECT_POISONED(s.b);
    EXPECT_NOT_POISONED(s.c);
    EXPECT_POISONED(s.d);
    EXPECT_NOT_POISONED(s.e);
    EXPECT_POISONED(s.f);
  }
  va_end(vl);
}

TEST(MemorySanitizer, VAArgStructByVal) {
  StructByVal s;
  s.a = 1;
  s.b = *GetPoisoned<int>();
  s.c = 2;
  s.d = *GetPoisoned<int>();
  s.e = 3;
  s.f = *GetPoisoned<int>();
  vaargsfn_structbyval(0, s, s);
}

NOINLINE void StructByValTestFunc(struct StructByVal s) {
  EXPECT_NOT_POISONED(s.a);
  EXPECT_POISONED(s.b);
  EXPECT_NOT_POISONED(s.c);
  EXPECT_POISONED(s.d);
  EXPECT_NOT_POISONED(s.e);
  EXPECT_POISONED(s.f);
}

NOINLINE void StructByValTestFunc1(struct StructByVal s) {
  StructByValTestFunc(s);
}

NOINLINE void StructByValTestFunc2(int z, struct StructByVal s) {
  StructByValTestFunc(s);
}

TEST(MemorySanitizer, StructByVal) {
  // Large aggregates are passed as "byval" pointer argument in LLVM.
  struct StructByVal s;
  s.a = 1;
  s.b = *GetPoisoned<int>();
  s.c = 2;
  s.d = *GetPoisoned<int>();
  s.e = 3;
  s.f = *GetPoisoned<int>();
  StructByValTestFunc(s);
  StructByValTestFunc1(s);
  StructByValTestFunc2(0, s);
}


#if MSAN_HAS_M128
NOINLINE __m128i m128Eq(__m128i *a, __m128i *b) { return _mm_cmpeq_epi16(*a, *b); }
NOINLINE __m128i m128Lt(__m128i *a, __m128i *b) { return _mm_cmplt_epi16(*a, *b); }
TEST(MemorySanitizer, m128) {
  __m128i a = _mm_set1_epi16(0x1234);
  __m128i b = _mm_set1_epi16(0x7890);
  EXPECT_NOT_POISONED(m128Eq(&a, &b));
  EXPECT_NOT_POISONED(m128Lt(&a, &b));
}
// FIXME: add more tests for __m128i.
#endif  // MSAN_HAS_M128

// We should not complain when copying this poisoned hole.
struct StructWithHole {
  U4  a;
  // 4-byte hole.
  U8  b;
};

NOINLINE StructWithHole ReturnStructWithHole() {
  StructWithHole res;
  __msan_poison(&res, sizeof(res));
  res.a = 1;
  res.b = 2;
  return res;
}

TEST(MemorySanitizer, StructWithHole) {
  StructWithHole a = ReturnStructWithHole();
  break_optimization(&a);
}

template <class T>
NOINLINE T ReturnStruct() {
  T res;
  __msan_poison(&res, sizeof(res));
  res.a = 1;
  return res;
}

template <class T>
NOINLINE void TestReturnStruct() {
  T s1 = ReturnStruct<T>();
  EXPECT_NOT_POISONED(s1.a);
  EXPECT_POISONED(s1.b);
}

struct SSS1 {
  int a, b, c;
};
struct SSS2 {
  int b, a, c;
};
struct SSS3 {
  int b, c, a;
};
struct SSS4 {
  int c, b, a;
};

struct SSS5 {
  int a;
  float b;
};
struct SSS6 {
  int a;
  double b;
};
struct SSS7 {
  S8 b;
  int a;
};
struct SSS8 {
  S2 b;
  S8 a;
};

TEST(MemorySanitizer, IntStruct3) {
  TestReturnStruct<SSS1>();
  TestReturnStruct<SSS2>();
  TestReturnStruct<SSS3>();
  TestReturnStruct<SSS4>();
  TestReturnStruct<SSS5>();
  TestReturnStruct<SSS6>();
  TestReturnStruct<SSS7>();
  TestReturnStruct<SSS8>();
}

struct LongStruct {
  U1 a1, b1;
  U2 a2, b2;
  U4 a4, b4;
  U8 a8, b8;
};

NOINLINE LongStruct ReturnLongStruct1() {
  LongStruct res;
  __msan_poison(&res, sizeof(res));
  res.a1 = res.a2 = res.a4 = res.a8 = 111;
  // leaves b1, .., b8 poisoned.
  return res;
}

NOINLINE LongStruct ReturnLongStruct2() {
  LongStruct res;
  __msan_poison(&res, sizeof(res));
  res.b1 = res.b2 = res.b4 = res.b8 = 111;
  // leaves a1, .., a8 poisoned.
  return res;
}

TEST(MemorySanitizer, LongStruct) {
  LongStruct s1 = ReturnLongStruct1();
  __msan_print_shadow(&s1, sizeof(s1));
  EXPECT_NOT_POISONED(s1.a1);
  EXPECT_NOT_POISONED(s1.a2);
  EXPECT_NOT_POISONED(s1.a4);
  EXPECT_NOT_POISONED(s1.a8);

  EXPECT_POISONED(s1.b1);
  EXPECT_POISONED(s1.b2);
  EXPECT_POISONED(s1.b4);
  EXPECT_POISONED(s1.b8);

  LongStruct s2 = ReturnLongStruct2();
  __msan_print_shadow(&s2, sizeof(s2));
  EXPECT_NOT_POISONED(s2.b1);
  EXPECT_NOT_POISONED(s2.b2);
  EXPECT_NOT_POISONED(s2.b4);
  EXPECT_NOT_POISONED(s2.b8);

  EXPECT_POISONED(s2.a1);
  EXPECT_POISONED(s2.a2);
  EXPECT_POISONED(s2.a4);
  EXPECT_POISONED(s2.a8);
}

#if defined(__FreeBSD__) || defined(__NetBSD__)
#define MSAN_TEST_PRLIMIT 0
#elif defined(__GLIBC__)
#define MSAN_TEST_PRLIMIT __GLIBC_PREREQ(2, 13)
#else
#define MSAN_TEST_PRLIMIT 1
#endif

TEST(MemorySanitizer, getrlimit) {
  struct rlimit limit;
  __msan_poison(&limit, sizeof(limit));
  int result = getrlimit(RLIMIT_DATA, &limit);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(limit.rlim_cur);
  EXPECT_NOT_POISONED(limit.rlim_max);

#if MSAN_TEST_PRLIMIT
  struct rlimit limit2;
  __msan_poison(&limit2, sizeof(limit2));
  result = prlimit(getpid(), RLIMIT_DATA, &limit, &limit2);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(limit2.rlim_cur);
  EXPECT_NOT_POISONED(limit2.rlim_max);

  __msan_poison(&limit, sizeof(limit));
  result = prlimit(getpid(), RLIMIT_DATA, nullptr, &limit);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(limit.rlim_cur);
  EXPECT_NOT_POISONED(limit.rlim_max);

  result = prlimit(getpid(), RLIMIT_DATA, &limit, nullptr);
  ASSERT_EQ(result, 0);
#endif
}

TEST(MemorySanitizer, getrusage) {
  struct rusage usage;
  __msan_poison(&usage, sizeof(usage));
  int result = getrusage(RUSAGE_SELF, &usage);
  ASSERT_EQ(result, 0);
  EXPECT_NOT_POISONED(usage.ru_utime.tv_sec);
  EXPECT_NOT_POISONED(usage.ru_utime.tv_usec);
  EXPECT_NOT_POISONED(usage.ru_stime.tv_sec);
  EXPECT_NOT_POISONED(usage.ru_stime.tv_usec);
  EXPECT_NOT_POISONED(usage.ru_maxrss);
  EXPECT_NOT_POISONED(usage.ru_minflt);
  EXPECT_NOT_POISONED(usage.ru_majflt);
  EXPECT_NOT_POISONED(usage.ru_inblock);
  EXPECT_NOT_POISONED(usage.ru_oublock);
  EXPECT_NOT_POISONED(usage.ru_nvcsw);
  EXPECT_NOT_POISONED(usage.ru_nivcsw);
}

#if defined(__FreeBSD__) || defined(__NetBSD__)
static void GetProgramPath(char *buf, size_t sz) {
#if defined(__FreeBSD__)
  int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
#elif defined(__NetBSD__)
  int mib[4] = { CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME};
#endif
  int res = sysctl(mib, 4, buf, &sz, NULL, 0);
  ASSERT_EQ(0, res);
}
#elif defined(__GLIBC__) || defined(MUSL)
static void GetProgramPath(char *buf, size_t sz) {
  extern char *program_invocation_name;
  int res = snprintf(buf, sz, "%s", program_invocation_name);
  ASSERT_GE(res, 0);
  ASSERT_LT((size_t)res, sz);
}
#else
# error "TODO: port this"
#endif

static void dladdr_testfn() {}

TEST(MemorySanitizer, dladdr) {
  Dl_info info;
  __msan_poison(&info, sizeof(info));
  int result = dladdr((const void*)dladdr_testfn, &info);
  ASSERT_NE(result, 0);
  EXPECT_NOT_POISONED((unsigned long)info.dli_fname);
  if (info.dli_fname)
    EXPECT_NOT_POISONED(strlen(info.dli_fname));
  EXPECT_NOT_POISONED((unsigned long)info.dli_fbase);
  EXPECT_NOT_POISONED((unsigned long)info.dli_sname);
  if (info.dli_sname)
    EXPECT_NOT_POISONED(strlen(info.dli_sname));
  EXPECT_NOT_POISONED((unsigned long)info.dli_saddr);
}

#ifndef MSAN_TEST_DISABLE_DLOPEN

static int dl_phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
  (*(int *)data)++;
  EXPECT_NOT_POISONED(info->dlpi_addr);
  EXPECT_NOT_POISONED(strlen(info->dlpi_name));
  EXPECT_NOT_POISONED(info->dlpi_phnum);
  for (int i = 0; i < info->dlpi_phnum; ++i)
    EXPECT_NOT_POISONED(info->dlpi_phdr[i]);
  return 0;
}

// Compute the path to our loadable DSO.  We assume it's in the same
// directory.  Only use string routines that we intercept so far to do this.
static void GetPathToLoadable(char *buf, size_t sz) {
  char program_path[kMaxPathLength];
  GetProgramPath(program_path, sizeof(program_path));

  const char *last_slash = strrchr(program_path, '/');
  ASSERT_NE(nullptr, last_slash);
  size_t dir_len = (size_t)(last_slash - program_path);
#  if defined(__x86_64__)
  static const char basename[] = "libmsan_loadable.x86_64.so";
#  elif defined(__MIPSEB__) || defined(MIPSEB)
  static const char basename[] = "libmsan_loadable.mips64.so";
#  elif defined(__mips64)
  static const char basename[] = "libmsan_loadable.mips64el.so";
#  elif defined(__aarch64__)
  static const char basename[] = "libmsan_loadable.aarch64.so";
#  elif defined(__loongarch_lp64)
  static const char basename[] = "libmsan_loadable.loongarch64.so";
#  elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  static const char basename[] = "libmsan_loadable.powerpc64.so";
#  elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  static const char basename[] = "libmsan_loadable.powerpc64le.so";
#  endif
  int res = snprintf(buf, sz, "%.*s/%s",
                     (int)dir_len, program_path, basename);
  ASSERT_GE(res, 0);
  ASSERT_LT((size_t)res, sz);
}

TEST(MemorySanitizer, dl_iterate_phdr) {
  char path[kMaxPathLength];
  GetPathToLoadable(path, sizeof(path));

  // Having at least one dlopen'ed library in the process makes this more
  // entertaining.
  void *lib = dlopen(path, RTLD_LAZY);
  ASSERT_NE((void*)0, lib);

  int count = 0;
  int result = dl_iterate_phdr(dl_phdr_callback, &count);
  ASSERT_GT(count, 0);

  dlclose(lib);
}

TEST(MemorySanitizer, dlopen) {
  char path[kMaxPathLength];
  GetPathToLoadable(path, sizeof(path));

  // We need to clear shadow for globals when doing dlopen.  In order to test
  // this, we have to poison the shadow for the DSO before we load it.  In
  // general this is difficult, but the loader tends to reload things in the
  // same place, so we open, close, and then reopen.  The global should always
  // start out clean after dlopen.
  for (int i = 0; i < 2; i++) {
    void *lib = dlopen(path, RTLD_LAZY);
    if (lib == NULL) {
      printf("dlerror: %s\n", dlerror());
      ASSERT_TRUE(lib != NULL);
    }
    void **(*get_dso_global)() = (void **(*)())dlsym(lib, "get_dso_global");
    ASSERT_TRUE(get_dso_global != NULL);
    void **dso_global = get_dso_global();
    EXPECT_NOT_POISONED(*dso_global);
    __msan_poison(dso_global, sizeof(*dso_global));
    EXPECT_POISONED(*dso_global);
    dlclose(lib);
  }
}

// Regression test for a crash in dlopen() interceptor.
TEST(MemorySanitizer, dlopenFailed) {
  const char *path = "/libmsan_loadable_does_not_exist.so";
  void *lib = dlopen(path, RTLD_LAZY);
  ASSERT_TRUE(lib == NULL);
}

#endif // MSAN_TEST_DISABLE_DLOPEN

#if !defined(__NetBSD__)
TEST(MemorySanitizer, sched_getaffinity) {
  cpu_set_t mask;
  if (sched_getaffinity(getpid(), sizeof(mask), &mask) == 0)
    EXPECT_NOT_POISONED(mask);
  else {
    // The call to sched_getaffinity() may have failed because the Affinity
    // mask is too small for the number of CPUs on the system (i.e. the
    // system has more than 1024 CPUs). Allocate a mask large enough for
    // twice as many CPUs.
    cpu_set_t *DynAffinity;
    DynAffinity = CPU_ALLOC(2048);
    int res = sched_getaffinity(getpid(), CPU_ALLOC_SIZE(2048), DynAffinity);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(*DynAffinity);
  }
}
#endif

TEST(MemorySanitizer, scanf) {
  const char *input = "42 hello";
  int* d = new int;
  char* s = new char[7];
  int res = sscanf(input, "%d %5s", d, s);
  printf("res %d\n", res);
  ASSERT_EQ(res, 2);
  EXPECT_NOT_POISONED(*d);
  EXPECT_NOT_POISONED(s[0]);
  EXPECT_NOT_POISONED(s[1]);
  EXPECT_NOT_POISONED(s[2]);
  EXPECT_NOT_POISONED(s[3]);
  EXPECT_NOT_POISONED(s[4]);
  EXPECT_NOT_POISONED(s[5]);
  EXPECT_POISONED(s[6]);
  delete[] s;
  delete d;
}

static void *SimpleThread_threadfn(void* data) {
  return new int;
}

TEST(MemorySanitizer, SimpleThread) {
  pthread_t t;
  void *p;
  int res = pthread_create(&t, NULL, SimpleThread_threadfn, NULL);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(t);
  res = pthread_join(t, &p);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(p);
  delete (int*)p;
}

static void *SmallStackThread_threadfn(void* data) {
  return 0;
}

static int GetThreadStackMin() {
#ifdef PTHREAD_STACK_MIN
  return PTHREAD_STACK_MIN;
#else
  return 0;
#endif
}

TEST(MemorySanitizer, SmallStackThread) {
  pthread_attr_t attr;
  pthread_t t;
  void *p;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  res = pthread_attr_setstacksize(&attr,
                                  std::max(GetThreadStackMin(), 64 * 1024));
  ASSERT_EQ(0, res);
  res = pthread_create(&t, &attr, SmallStackThread_threadfn, NULL);
  ASSERT_EQ(0, res);
  res = pthread_join(t, &p);
  ASSERT_EQ(0, res);
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, SmallPreAllocatedStackThread) {
  pthread_attr_t attr;
  pthread_t t;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  void *stack;
  const size_t kStackSize = std::max(GetThreadStackMin(), 32 * 1024);
  res = posix_memalign(&stack, 4096, kStackSize);
  ASSERT_EQ(0, res);
  res = pthread_attr_setstack(&attr, stack, kStackSize);
  ASSERT_EQ(0, res);
  res = pthread_create(&t, &attr, SmallStackThread_threadfn, NULL);
  EXPECT_EQ(0, res);
  res = pthread_join(t, NULL);
  ASSERT_EQ(0, res);
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, pthread_attr_get) {
  pthread_attr_t attr;
  int res;
  res = pthread_attr_init(&attr);
  ASSERT_EQ(0, res);
  {
    int v;
    res = pthread_attr_getdetachstate(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    size_t v;
    res = pthread_attr_getguardsize(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    struct sched_param v;
    res = pthread_attr_getschedparam(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getschedpolicy(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getinheritsched(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    int v;
    res = pthread_attr_getscope(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    size_t v;
    res = pthread_attr_getstacksize(&attr, &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
  {
    void *v;
    size_t w;
    res = pthread_attr_getstack(&attr, &v, &w);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
    EXPECT_NOT_POISONED(w);
  }
#ifdef __GLIBC__
  {
    cpu_set_t v;
    res = pthread_attr_getaffinity_np(&attr, sizeof(v), &v);
    ASSERT_EQ(0, res);
    EXPECT_NOT_POISONED(v);
  }
#endif
  res = pthread_attr_destroy(&attr);
  ASSERT_EQ(0, res);
}

TEST(MemorySanitizer, pthread_getschedparam) {
  int policy;
  struct sched_param param;
  int res = pthread_getschedparam(pthread_self(), &policy, &param);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(policy);
  EXPECT_NOT_POISONED(param.sched_priority);
}

TEST(MemorySanitizer, pthread_key_create) {
  pthread_key_t key;
  int res = pthread_key_create(&key, NULL);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(key);
  res = pthread_key_delete(key);
  ASSERT_EQ(0, res);
}

namespace {
struct SignalCondArg {
  pthread_cond_t* cond;
  pthread_mutex_t* mu;
  bool broadcast;
};

void *SignalCond(void *param) {
  SignalCondArg *arg = reinterpret_cast<SignalCondArg *>(param);
  pthread_mutex_lock(arg->mu);
  if (arg->broadcast)
    pthread_cond_broadcast(arg->cond);
  else
    pthread_cond_signal(arg->cond);
  pthread_mutex_unlock(arg->mu);
  return 0;
}
}  // namespace

TEST(MemorySanitizer, pthread_cond_wait) {
  pthread_cond_t cond;
  pthread_mutex_t mu;
  SignalCondArg args = {&cond, &mu, false};
  pthread_cond_init(&cond, 0);
  pthread_mutex_init(&mu, 0);
  pthread_mutex_lock(&mu);

  // signal
  pthread_t thr;
  pthread_create(&thr, 0, SignalCond, &args);
  int res = pthread_cond_wait(&cond, &mu);
  ASSERT_EQ(0, res);
  pthread_join(thr, 0);

  // broadcast
  args.broadcast = true;
  pthread_create(&thr, 0, SignalCond, &args);
  res = pthread_cond_wait(&cond, &mu);
  ASSERT_EQ(0, res);
  pthread_join(thr, 0);

  pthread_mutex_unlock(&mu);
  pthread_mutex_destroy(&mu);
  pthread_cond_destroy(&cond);
}

TEST(MemorySanitizer, tmpnam) {
  char s[L_tmpnam];
  char *res = tmpnam(s);
  ASSERT_EQ(s, res);
  EXPECT_NOT_POISONED(strlen(res));
}

TEST(MemorySanitizer, tempnam) {
  char *res = tempnam(NULL, "zzz");
  EXPECT_NOT_POISONED(strlen(res));
  free(res);
}

TEST(MemorySanitizer, posix_memalign) {
  void *p;
  EXPECT_POISONED(p);
  int res = posix_memalign(&p, 4096, 13);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(p);
  EXPECT_EQ(0U, (uintptr_t)p % 4096);
  free(p);
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, memalign) {
  void *p = memalign(4096, 13);
  EXPECT_EQ(0U, (uintptr_t)p % 4096);
  free(p);
}
#endif

TEST(MemorySanitizer, valloc) {
  void *a = valloc(100);
  uintptr_t PageSize = GetPageSize();
  EXPECT_EQ(0U, (uintptr_t)a % PageSize);
  free(a);
}

#ifdef __GLIBC__
TEST(MemorySanitizer, pvalloc) {
  uintptr_t PageSize = GetPageSize();
  void *p = pvalloc(PageSize + 100);
  EXPECT_EQ(0U, (uintptr_t)p % PageSize);
  EXPECT_EQ(2 * PageSize, __sanitizer_get_allocated_size(p));
  free(p);

  p = pvalloc(0);  // pvalloc(0) should allocate at least one page.
  EXPECT_EQ(0U, (uintptr_t)p % PageSize);
  EXPECT_EQ(PageSize, __sanitizer_get_allocated_size(p));
  free(p);
}
#endif

TEST(MemorySanitizer, inet_pton) {
  const char *s = "1:0:0:0:0:0:0:8";
  unsigned char buf[sizeof(struct in6_addr)];
  int res = inet_pton(AF_INET6, s, buf);
  ASSERT_EQ(1, res);
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_NOT_POISONED(buf[sizeof(struct in6_addr) - 1]);

  char s_out[INET6_ADDRSTRLEN];
  EXPECT_POISONED(s_out[3]);
  const char *q = inet_ntop(AF_INET6, buf, s_out, INET6_ADDRSTRLEN);
  ASSERT_NE((void*)0, q);
  EXPECT_NOT_POISONED(s_out[3]);
}

TEST(MemorySanitizer, inet_aton) {
  const char *s = "127.0.0.1";
  struct in_addr in[2];
  int res = inet_aton(s, in);
  ASSERT_NE(0, res);
  EXPECT_NOT_POISONED(in[0]);
  EXPECT_POISONED(*(char *)(in + 1));
}

TEST(MemorySanitizer, uname) {
  struct utsname u;
  int res = uname(&u);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(strlen(u.sysname));
  EXPECT_NOT_POISONED(strlen(u.nodename));
  EXPECT_NOT_POISONED(strlen(u.release));
  EXPECT_NOT_POISONED(strlen(u.version));
  EXPECT_NOT_POISONED(strlen(u.machine));
}

TEST(MemorySanitizer, gethostname) {
  char buf[1000];
  EXPECT_EQ(-1, gethostname(buf, 1));
  EXPECT_EQ(ENAMETOOLONG, errno);
  EXPECT_NOT_POISONED(buf[0]);
  EXPECT_POISONED(buf[1]);

  __msan_poison(buf, sizeof(buf));
  EXPECT_EQ(0, gethostname(buf, sizeof(buf)));
  EXPECT_NOT_POISONED(strlen(buf));
}

#if !defined(__FreeBSD__) && !defined(__NetBSD__)
TEST(MemorySanitizer, sysinfo) {
  struct sysinfo info;
  int res = sysinfo(&info);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(info);
}
#endif

TEST(MemorySanitizer, getpwuid) {
  struct passwd *p = getpwuid(0); // root
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(p->pw_name);
  ASSERT_TRUE(p->pw_name != NULL);
  EXPECT_NOT_POISONED(p->pw_name[0]);
  EXPECT_NOT_POISONED(p->pw_uid);
  ASSERT_EQ(0U, p->pw_uid);
}

TEST(MemorySanitizer, getpwuid_r) {
  struct passwd pwd;
  struct passwd *pwdres;
  char buf[10000];
  int res = getpwuid_r(0, &pwd, buf, sizeof(buf), &pwdres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pwd.pw_name);
  ASSERT_TRUE(pwd.pw_name != NULL);
  EXPECT_NOT_POISONED(pwd.pw_name[0]);
  EXPECT_NOT_POISONED(pwd.pw_uid);
  ASSERT_EQ(0U, pwd.pw_uid);
  EXPECT_NOT_POISONED(pwdres);
}

TEST(MemorySanitizer, getpwnam_r) {
  struct passwd pwd;
  struct passwd *pwdres;
  char buf[10000];
  int res = getpwnam_r("root", &pwd, buf, sizeof(buf), &pwdres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pwd.pw_name);
  ASSERT_TRUE(pwd.pw_name != NULL);
  EXPECT_NOT_POISONED(pwd.pw_name[0]);
  EXPECT_NOT_POISONED(pwd.pw_uid);
  ASSERT_EQ(0U, pwd.pw_uid);
  EXPECT_NOT_POISONED(pwdres);
}

TEST(MemorySanitizer, getpwnam_r_positive) {
  struct passwd pwd;
  struct passwd *pwdres;
  char s[5];
  strncpy(s, "abcd", 5);
  __msan_poison(s, 5);
  char buf[10000];
  EXPECT_UMR(getpwnam_r(s, &pwd, buf, sizeof(buf), &pwdres));
}

TEST(MemorySanitizer, getgrnam_r) {
  struct group grp;
  struct group *grpres;
  char buf[10000];
  int res = getgrnam_r(SUPERUSER_GROUP, &grp, buf, sizeof(buf), &grpres);
  ASSERT_EQ(0, res);
  // Note that getgrnam_r() returns 0 if the matching group is not found.
  ASSERT_NE(nullptr, grpres);
  EXPECT_NOT_POISONED(grp.gr_name);
  ASSERT_TRUE(grp.gr_name != NULL);
  EXPECT_NOT_POISONED(grp.gr_name[0]);
  EXPECT_NOT_POISONED(grp.gr_gid);
  EXPECT_NOT_POISONED(grpres);
}

TEST(MemorySanitizer, getpwent) {
  setpwent();
  struct passwd *p = getpwent();
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(p->pw_name);
  ASSERT_TRUE(p->pw_name != NULL);
  EXPECT_NOT_POISONED(p->pw_name[0]);
  EXPECT_NOT_POISONED(p->pw_uid);
}

#ifndef MUSL
TEST(MemorySanitizer, getpwent_r) {
  struct passwd pwd;
  struct passwd *pwdres;
  char buf[10000];
  setpwent();
  int res = getpwent_r(&pwd, buf, sizeof(buf), &pwdres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(pwd.pw_name);
  ASSERT_TRUE(pwd.pw_name != NULL);
  EXPECT_NOT_POISONED(pwd.pw_name[0]);
  EXPECT_NOT_POISONED(pwd.pw_uid);
  EXPECT_NOT_POISONED(pwdres);
}
#endif

#ifdef __GLIBC__
TEST(MemorySanitizer, fgetpwent) {
  FILE *fp = fopen("/etc/passwd", "r");
  struct passwd *p = fgetpwent(fp);
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(p->pw_name);
  ASSERT_TRUE(p->pw_name != NULL);
  EXPECT_NOT_POISONED(p->pw_name[0]);
  EXPECT_NOT_POISONED(p->pw_uid);
  fclose(fp);
}
#endif

TEST(MemorySanitizer, getgrent) {
  setgrent();
  struct group *p = getgrent();
  ASSERT_TRUE(p != NULL);
  EXPECT_NOT_POISONED(p->gr_name);
  ASSERT_TRUE(p->gr_name != NULL);
  EXPECT_NOT_POISONED(p->gr_name[0]);
  EXPECT_NOT_POISONED(p->gr_gid);
}

#ifdef __GLIBC__
TEST(MemorySanitizer, fgetgrent) {
  FILE *fp = fopen("/etc/group", "r");
  struct group *grp = fgetgrent(fp);
  ASSERT_TRUE(grp != NULL);
  EXPECT_NOT_POISONED(grp->gr_name);
  ASSERT_TRUE(grp->gr_name != NULL);
  EXPECT_NOT_POISONED(grp->gr_name[0]);
  EXPECT_NOT_POISONED(grp->gr_gid);
  for (char **p = grp->gr_mem; *p; ++p) {
    EXPECT_NOT_POISONED((*p)[0]);
    EXPECT_TRUE(strlen(*p) > 0);
  }
  fclose(fp);
}
#endif

#if defined(__GLIBC__) || defined(__FreeBSD__)
TEST(MemorySanitizer, getgrent_r) {
  struct group grp;
  struct group *grpres;
  char buf[10000];
  setgrent();
  int res = getgrent_r(&grp, buf, sizeof(buf), &grpres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(grp.gr_name);
  ASSERT_TRUE(grp.gr_name != NULL);
  EXPECT_NOT_POISONED(grp.gr_name[0]);
  EXPECT_NOT_POISONED(grp.gr_gid);
  EXPECT_NOT_POISONED(grpres);
}
#endif

#ifdef __GLIBC__
TEST(MemorySanitizer, fgetgrent_r) {
  FILE *fp = fopen("/etc/group", "r");
  struct group grp;
  struct group *grpres;
  char buf[10000];
  setgrent();
  int res = fgetgrent_r(fp, &grp, buf, sizeof(buf), &grpres);
  ASSERT_EQ(0, res);
  EXPECT_NOT_POISONED(grp.gr_name);
  ASSERT_TRUE(grp.gr_name != NULL);
  EXPECT_NOT_POISONED(grp.gr_name[0]);
  EXPECT_NOT_POISONED(grp.gr_gid);
  EXPECT_NOT_POISONED(grpres);
  fclose(fp);
}
#endif

TEST(MemorySanitizer, getgroups) {
  int n = getgroups(0, 0);
  gid_t *gids = new gid_t[n];
  int res = getgroups(n, gids);
  ASSERT_EQ(n, res);
  for (int i = 0; i < n; ++i)
    EXPECT_NOT_POISONED(gids[i]);
}

TEST(MemorySanitizer, getgroups_zero) {
  gid_t group;
  int n = getgroups(0, &group);
  ASSERT_GE(n, 0);
}

TEST(MemorySanitizer, getgroups_negative) {
  gid_t group;
  int n = getgroups(-1, 0);
  ASSERT_EQ(-1, n);

  n = getgroups(-1, 0);
  ASSERT_EQ(-1, n);
}

TEST(MemorySanitizer, wordexp_empty) {
  wordexp_t w;
  int res = wordexp("", &w, 0);
  ASSERT_EQ(0, res);
  ASSERT_EQ(0U, w.we_wordc);
  ASSERT_STREQ(nullptr, w.we_wordv[0]);
}

TEST(MemorySanitizer, wordexp) {
  wordexp_t w;
  int res = wordexp("a b c", &w, 0);
  ASSERT_EQ(0, res);
  ASSERT_EQ(3U, w.we_wordc);
  ASSERT_STREQ("a", w.we_wordv[0]);
  ASSERT_STREQ("b", w.we_wordv[1]);
  ASSERT_STREQ("c", w.we_wordv[2]);
}

TEST(MemorySanitizer, wordexp_initial_offset) {
  wordexp_t w;
  w.we_offs = 1;
  int res = wordexp("a b c", &w, WRDE_DOOFFS);
  ASSERT_EQ(0, res);
  ASSERT_EQ(3U, w.we_wordc);
  ASSERT_EQ(nullptr, w.we_wordv[0]);
  ASSERT_STREQ("a", w.we_wordv[1]);
  ASSERT_STREQ("b", w.we_wordv[2]);
  ASSERT_STREQ("c", w.we_wordv[3]);
}

template<class T>
static bool applySlt(T value, T shadow) {
  __msan_partial_poison(&value, &shadow, sizeof(T));
  volatile bool zzz = true;
  // This "|| zzz" trick somehow makes LLVM emit "icmp slt" instead of
  // a shift-and-trunc to get at the highest bit.
  volatile bool v = value < 0 || zzz;
  return v;
}

TEST(MemorySanitizer, SignedCompareWithZero) {
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xFF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0xFFFFFF));
  EXPECT_NOT_POISONED(applySlt<S4>(0xF, 0x7FFFFFF));
  EXPECT_UMR(applySlt<S4>(0xF, 0x80FFFFFF));
  EXPECT_UMR(applySlt<S4>(0xF, 0xFFFFFFFF));
}

template <class T, class S>
static T poisoned(T Va, S Sa) {
  char SIZE_CHECK1[(ssize_t)sizeof(T) - (ssize_t)sizeof(S)];
  char SIZE_CHECK2[(ssize_t)sizeof(S) - (ssize_t)sizeof(T)];
  T a;
  a = Va;
  __msan_partial_poison(&a, &Sa, sizeof(T));
  return a;
}

TEST(MemorySanitizer, ICmpRelational) {
  EXPECT_NOT_POISONED(poisoned(0, 0) < poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) < poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) < poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) < poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) < poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) <
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) <
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) <= poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) <= poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) <= poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) <= poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) <= poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) <=
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) <=
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) > poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) > poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) > poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) > poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) > poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) >
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) >
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_NOT_POISONED(poisoned(0, 0) >= poisoned(0, 0));
  EXPECT_NOT_POISONED(poisoned(0U, 0) >= poisoned(0U, 0));
  EXPECT_NOT_POISONED(poisoned(0LL, 0LLU) >= poisoned(0LL, 0LLU));
  EXPECT_NOT_POISONED(poisoned(0LLU, 0LLU) >= poisoned(0LLU, 0LLU));
  EXPECT_POISONED(poisoned(0xFF, 0xFF) >= poisoned(0xFF, 0xFF));
  EXPECT_POISONED(poisoned(0xFFFFFFFFU, 0xFFFFFFFFU) >=
                  poisoned(0xFFFFFFFFU, 0xFFFFFFFFU));
  EXPECT_POISONED(poisoned(-1, 0xFFFFFFFFU) >=
                  poisoned(-1, 0xFFFFFFFFU));

  EXPECT_POISONED(poisoned(6, 0xF) > poisoned(7, 0));
  EXPECT_POISONED(poisoned(0xF, 0xF) > poisoned(7, 0));
  // Note that "icmp op X, Y" is approximated with "or shadow(X), shadow(Y)"
  // and therefore may generate false positives in some cases, e.g. the
  // following one:
  // EXPECT_NOT_POISONED(poisoned(-1, 0x80000000U) >= poisoned(-1, 0U));
}

#if MSAN_HAS_M128
TEST(MemorySanitizer, ICmpVectorRelational) {
  EXPECT_NOT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0)),
                   poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0))));
  EXPECT_NOT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi32(0), _mm_set1_epi32(0)),
                   poisoned(_mm_set1_epi32(0), _mm_set1_epi32(0))));
  EXPECT_POISONED(
      _mm_cmplt_epi16(poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0xFFFF)),
                   poisoned(_mm_set1_epi16(0), _mm_set1_epi16(0xFFFF))));
  EXPECT_POISONED(_mm_cmpgt_epi16(poisoned(_mm_set1_epi16(6), _mm_set1_epi16(0xF)),
                               poisoned(_mm_set1_epi16(7), _mm_set1_epi16(0))));
}

TEST(MemorySanitizer, stmxcsr_ldmxcsr) {
  U4 x = _mm_getcsr();
  EXPECT_NOT_POISONED(x);

  _mm_setcsr(x);

  __msan_poison(&x, sizeof(x));
  U4 origin = __LINE__;
  __msan_set_origin(&x, sizeof(x), origin);
  EXPECT_UMR_O(_mm_setcsr(x), origin);
}
#endif

// Volatile bitfield store is implemented as load-mask-store
// Test that we don't warn on the store of (uninitialized) padding.
struct VolatileBitfieldStruct {
  volatile unsigned x : 1;
  unsigned y : 1;
};

TEST(MemorySanitizer, VolatileBitfield) {
  VolatileBitfieldStruct *S = new VolatileBitfieldStruct;
  S->x = 1;
  EXPECT_NOT_POISONED((unsigned)S->x);
  EXPECT_POISONED((unsigned)S->y);
}

TEST(MemorySanitizer, UnalignedLoad) {
  char x[32] __attribute__((aligned(8)));
  U4 origin = __LINE__;
  for (unsigned i = 0; i < sizeof(x) / 4; ++i)
    __msan_set_origin(x + 4 * i, 4, origin + i);

  memset(x + 8, 0, 16);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x + 6), origin + 1);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x + 7), origin + 1);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x + 8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x + 9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load16(x + 22));
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x + 23), origin + 6);
  EXPECT_POISONED_O(__sanitizer_unaligned_load16(x + 24), origin + 6);

  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x + 4), origin + 1);
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x + 7), origin + 1);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x + 8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x + 9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load32(x + 20));
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x + 21), origin + 6);
  EXPECT_POISONED_O(__sanitizer_unaligned_load32(x + 24), origin + 6);

  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x + 1), origin);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x + 7), origin + 1);
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x + 8));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x + 9));
  EXPECT_NOT_POISONED(__sanitizer_unaligned_load64(x + 16));
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x + 17), origin + 6);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x + 21), origin + 6);
  EXPECT_POISONED_O(__sanitizer_unaligned_load64(x + 24), origin + 6);
}

TEST(MemorySanitizer, UnalignedStore16) {
  char x[5] __attribute__((aligned(4)));
  U2 y2 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y2, 1);
  __msan_set_origin(&y2, 1, origin);

  __sanitizer_unaligned_store16(x + 1, y2);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_NOT_POISONED(x[2]);
  EXPECT_POISONED_O(x[3], origin);
}

TEST(MemorySanitizer, UnalignedStore32) {
  char x[8] __attribute__((aligned(4)));
  U4 y4 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y4, 2);
  __msan_set_origin(&y4, 2, origin);

  __sanitizer_unaligned_store32(x + 3, y4);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_POISONED_O(x[2], origin);
  EXPECT_POISONED_O(x[3], origin);
  EXPECT_POISONED_O(x[4], origin);
  EXPECT_NOT_POISONED(x[5]);
  EXPECT_NOT_POISONED(x[6]);
  EXPECT_POISONED_O(x[7], origin);
}

TEST(MemorySanitizer, UnalignedStore64) {
  char x[16] __attribute__((aligned(8)));
  U8 y8 = 0;
  U4 origin = __LINE__;
  __msan_poison(&y8, 3);
  __msan_poison(((char *)&y8) + sizeof(y8) - 2, 1);
  __msan_set_origin(&y8, 8, origin);

  __sanitizer_unaligned_store64(x + 3, y8);
  EXPECT_POISONED_O(x[0], origin);
  EXPECT_POISONED_O(x[1], origin);
  EXPECT_POISONED_O(x[2], origin);
  EXPECT_POISONED_O(x[3], origin);
  EXPECT_POISONED_O(x[4], origin);
  EXPECT_POISONED_O(x[5], origin);
  EXPECT_NOT_POISONED(x[6]);
  EXPECT_NOT_POISONED(x[7]);
  EXPECT_NOT_POISONED(x[8]);
  EXPECT_POISONED_O(x[9], origin);
  EXPECT_NOT_POISONED(x[10]);
  EXPECT_POISONED_O(x[11], origin);
}

TEST(MemorySanitizer, UnalignedStore16_precise) {
  char x[8] __attribute__((aligned(4)));
  U2 y = 0;
  U4 originx1 = __LINE__;
  U4 originx2 = __LINE__;
  U4 originy = __LINE__;
  __msan_poison(x, sizeof(x));
  __msan_set_origin(x, 4, originx1);
  __msan_set_origin(x + 4, 4, originx2);
  __msan_poison(((char *)&y) + 1, 1);
  __msan_set_origin(&y, sizeof(y), originy);

  __sanitizer_unaligned_store16(x + 3, y);
  EXPECT_POISONED_O(x[0], originx1);
  EXPECT_POISONED_O(x[1], originx1);
  EXPECT_POISONED_O(x[2], originx1);
  EXPECT_NOT_POISONED(x[3]);
  EXPECT_POISONED_O(x[4], originy);
  EXPECT_POISONED_O(x[5], originy);
  EXPECT_POISONED_O(x[6], originy);
  EXPECT_POISONED_O(x[7], originy);
}

TEST(MemorySanitizer, UnalignedStore16_precise2) {
  char x[8] __attribute__((aligned(4)));
  U2 y = 0;
  U4 originx1 = __LINE__;
  U4 originx2 = __LINE__;
  U4 originy = __LINE__;
  __msan_poison(x, sizeof(x));
  __msan_set_origin(x, 4, originx1);
  __msan_set_origin(x + 4, 4, originx2);
  __msan_poison(((char *)&y), 1);
  __msan_set_origin(&y, sizeof(y), originy);

  __sanitizer_unaligned_store16(x + 3, y);
  EXPECT_POISONED_O(x[0], originy);
  EXPECT_POISONED_O(x[1], originy);
  EXPECT_POISONED_O(x[2], originy);
  EXPECT_POISONED_O(x[3], originy);
  EXPECT_NOT_POISONED(x[4]);
  EXPECT_POISONED_O(x[5], originx2);
  EXPECT_POISONED_O(x[6], originx2);
  EXPECT_POISONED_O(x[7], originx2);
}

TEST(MemorySanitizer, UnalignedStore64_precise) {
  char x[12] __attribute__((aligned(8)));
  U8 y = 0;
  U4 originx1 = __LINE__;
  U4 originx2 = __LINE__;
  U4 originx3 = __LINE__;
  U4 originy = __LINE__;
  __msan_poison(x, sizeof(x));
  __msan_set_origin(x, 4, originx1);
  __msan_set_origin(x + 4, 4, originx2);
  __msan_set_origin(x + 8, 4, originx3);
  __msan_poison(((char *)&y) + 1, 1);
  __msan_poison(((char *)&y) + 7, 1);
  __msan_set_origin(&y, sizeof(y), originy);

  __sanitizer_unaligned_store64(x + 2, y);
  EXPECT_POISONED_O(x[0], originy);
  EXPECT_POISONED_O(x[1], originy);
  EXPECT_NOT_POISONED(x[2]);
  EXPECT_POISONED_O(x[3], originy);

  EXPECT_NOT_POISONED(x[4]);
  EXPECT_NOT_POISONED(x[5]);
  EXPECT_NOT_POISONED(x[6]);
  EXPECT_NOT_POISONED(x[7]);

  EXPECT_NOT_POISONED(x[8]);
  EXPECT_POISONED_O(x[9], originy);
  EXPECT_POISONED_O(x[10], originy);
  EXPECT_POISONED_O(x[11], originy);
}

TEST(MemorySanitizer, UnalignedStore64_precise2) {
  char x[12] __attribute__((aligned(8)));
  U8 y = 0;
  U4 originx1 = __LINE__;
  U4 originx2 = __LINE__;
  U4 originx3 = __LINE__;
  U4 originy = __LINE__;
  __msan_poison(x, sizeof(x));
  __msan_set_origin(x, 4, originx1);
  __msan_set_origin(x + 4, 4, originx2);
  __msan_set_origin(x + 8, 4, originx3);
  __msan_poison(((char *)&y) + 3, 3);
  __msan_set_origin(&y, sizeof(y), originy);

  __sanitizer_unaligned_store64(x + 2, y);
  EXPECT_POISONED_O(x[0], originx1);
  EXPECT_POISONED_O(x[1], originx1);
  EXPECT_NOT_POISONED(x[2]);
  EXPECT_NOT_POISONED(x[3]);

  EXPECT_NOT_POISONED(x[4]);
  EXPECT_POISONED_O(x[5], originy);
  EXPECT_POISONED_O(x[6], originy);
  EXPECT_POISONED_O(x[7], originy);

  EXPECT_NOT_POISONED(x[8]);
  EXPECT_NOT_POISONED(x[9]);
  EXPECT_POISONED_O(x[10], originx3);
  EXPECT_POISONED_O(x[11], originx3);
}

#if (defined(__x86_64__) && defined(__clang__))
namespace {
typedef U1 V16x8 __attribute__((__vector_size__(16)));
typedef U2 V8x16 __attribute__((__vector_size__(16)));
typedef U4 V4x32 __attribute__((__vector_size__(16)));
typedef U8 V2x64 __attribute__((__vector_size__(16)));
typedef U4 V8x32 __attribute__((__vector_size__(32)));
typedef U8 V4x64 __attribute__((__vector_size__(32)));
typedef U4 V2x32 __attribute__((__vector_size__(8)));
typedef U2 V4x16 __attribute__((__vector_size__(8)));
typedef U1 V8x8 __attribute__((__vector_size__(8)));

V8x16 shift_sse2_left_scalar(V8x16 x, U4 y) {
  return _mm_slli_epi16(x, y);
}

V8x16 shift_sse2_left(V8x16 x, V8x16 y) {
  return _mm_sll_epi16(x, y);
}

TEST(VectorShiftTest, sse2_left_scalar) {
  V8x16 v = {Poisoned<U2>(0, 3), Poisoned<U2>(0, 7), 2, 3, 4, 5, 6, 7};
  V8x16 u = shift_sse2_left_scalar(v, 2);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_NOT_POISONED(u[0] | (3U << 2));
  EXPECT_NOT_POISONED(u[1] | (7U << 2));
  u[0] = u[1] = 0;
  EXPECT_NOT_POISONED(u);
}

TEST(VectorShiftTest, sse2_left_scalar_by_uninit) {
  V8x16 v = {0, 1, 2, 3, 4, 5, 6, 7};
  V8x16 u = shift_sse2_left_scalar(v, Poisoned<U4>());
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[4]);
  EXPECT_POISONED(u[5]);
  EXPECT_POISONED(u[6]);
  EXPECT_POISONED(u[7]);
}

TEST(VectorShiftTest, sse2_left) {
  V8x16 v = {Poisoned<U2>(0, 3), Poisoned<U2>(0, 7), 2, 3, 4, 5, 6, 7};
  // Top 64 bits of shift count don't affect the result.
  V2x64 s = {2, Poisoned<U8>()};
  V8x16 u = shift_sse2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_NOT_POISONED(u[0] | (3U << 2));
  EXPECT_NOT_POISONED(u[1] | (7U << 2));
  u[0] = u[1] = 0;
  EXPECT_NOT_POISONED(u);
}

TEST(VectorShiftTest, sse2_left_by_uninit) {
  V8x16 v = {Poisoned<U2>(0, 3), Poisoned<U2>(0, 7), 2, 3, 4, 5, 6, 7};
  V2x64 s = {Poisoned<U8>(), Poisoned<U8>()};
  V8x16 u = shift_sse2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[4]);
  EXPECT_POISONED(u[5]);
  EXPECT_POISONED(u[6]);
  EXPECT_POISONED(u[7]);
}

#ifdef __AVX2__
V4x32 shift_avx2_left(V4x32 x, V4x32 y) {
  return _mm_sllv_epi32(x, y);
}
// This is variable vector shift that's only available starting with AVX2.
// V4x32 shift_avx2_left(V4x32 x, V4x32 y) {
TEST(VectorShiftTest, avx2_left) {
  V4x32 v = {Poisoned<U2>(0, 3), Poisoned<U2>(0, 7), 2, 3};
  V4x32 s = {2, Poisoned<U4>(), 3, Poisoned<U4>()};
  V4x32 u = shift_avx2_left(v, s);
  EXPECT_POISONED(u[0]);
  EXPECT_NOT_POISONED(u[0] | (~7U));
  EXPECT_POISONED(u[1]);
  EXPECT_POISONED(u[1] | (~31U));
  EXPECT_NOT_POISONED(u[2]);
  EXPECT_POISONED(u[3]);
  EXPECT_POISONED(u[3] | (~31U));
}
#endif // __AVX2__
} // namespace

TEST(VectorPackTest, sse2_packssdw_128) {
  const unsigned S2_max = (1 << 15) - 1;
  V4x32 a = {Poisoned<U4>(0, 0xFF0000), Poisoned<U4>(0, 0xFFFF0000),
             S2_max + 100, 4};
  V4x32 b = {Poisoned<U4>(0, 0xFF), S2_max + 10000, Poisoned<U4>(0, 0xFF00),
             S2_max};

  V8x16 c = _mm_packs_epi32(a, b);

  EXPECT_POISONED(c[0]);
  EXPECT_POISONED(c[1]);
  EXPECT_NOT_POISONED(c[2]);
  EXPECT_NOT_POISONED(c[3]);
  EXPECT_POISONED(c[4]);
  EXPECT_NOT_POISONED(c[5]);
  EXPECT_POISONED(c[6]);
  EXPECT_NOT_POISONED(c[7]);

  EXPECT_EQ(c[2], S2_max);
  EXPECT_EQ(c[3], 4);
  EXPECT_EQ(c[5], S2_max);
  EXPECT_EQ(c[7], S2_max);
}

TEST(VectorPackTest, mmx_packuswb) {
  const unsigned U1_max = (1 << 8) - 1;
  V4x16 a = {Poisoned<U2>(0, 0xFF00), Poisoned<U2>(0, 0xF000U), U1_max + 100,
             4};
  V4x16 b = {Poisoned<U2>(0, 0xFF), U1_max - 1, Poisoned<U2>(0, 0xF), U1_max};
  V8x8 c = _mm_packs_pu16(a, b);

  EXPECT_POISONED(c[0]);
  EXPECT_POISONED(c[1]);
  EXPECT_NOT_POISONED(c[2]);
  EXPECT_NOT_POISONED(c[3]);
  EXPECT_POISONED(c[4]);
  EXPECT_NOT_POISONED(c[5]);
  EXPECT_POISONED(c[6]);
  EXPECT_NOT_POISONED(c[7]);

  EXPECT_EQ(c[2], U1_max);
  EXPECT_EQ(c[3], 4);
  EXPECT_EQ(c[5], U1_max - 1);
  EXPECT_EQ(c[7], U1_max);
}

TEST(VectorSadTest, sse2_psad_bw) {
  V16x8 a = {Poisoned<U1>(), 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  V16x8 b = {100, 101, 102, 103, 104, 105, 106, 107,
             108, 109, 110, 111, 112, 113, 114, 115};
  V2x64 c = _mm_sad_epu8(a, b);

  EXPECT_POISONED(c[0]);
  EXPECT_NOT_POISONED(c[1]);

  EXPECT_EQ(800U, c[1]);
}

TEST(VectorMaddTest, mmx_pmadd_wd) {
  V4x16 a = {Poisoned<U2>(), 1, 2, 3};
  V4x16 b = {100, 101, 102, 103};
  V2x32 c = _mm_madd_pi16(a, b);

  EXPECT_POISONED(c[0]);
  EXPECT_NOT_POISONED(c[1]);

  EXPECT_EQ((unsigned)(2 * 102 + 3 * 103), c[1]);
}

TEST(VectorCmpTest, mm_cmpneq_ps) {
  V4x32 c;
  c = _mm_cmpneq_ps(V4x32{Poisoned<U4>(), 1, 2, 3}, V4x32{4, 5, Poisoned<U4>(), 6});
  EXPECT_POISONED(c[0]);
  EXPECT_NOT_POISONED(c[1]);
  EXPECT_POISONED(c[2]);
  EXPECT_NOT_POISONED(c[3]);

  c = _mm_cmpneq_ps(V4x32{0, 1, 2, 3}, V4x32{4, 5, 6, 7});
  EXPECT_NOT_POISONED(c);
}

TEST(VectorCmpTest, mm_cmpneq_sd) {
  V2x64 c;
  c = _mm_cmpneq_sd(V2x64{Poisoned<U8>(), 1}, V2x64{2, 3});
  EXPECT_POISONED(c[0]);
  c = _mm_cmpneq_sd(V2x64{1, 2}, V2x64{Poisoned<U8>(), 3});
  EXPECT_POISONED(c[0]);
  c = _mm_cmpneq_sd(V2x64{1, 2}, V2x64{3, 4});
  EXPECT_NOT_POISONED(c[0]);
  c = _mm_cmpneq_sd(V2x64{1, Poisoned<U8>()}, V2x64{2, Poisoned<U8>()});
  EXPECT_NOT_POISONED(c[0]);
  c = _mm_cmpneq_sd(V2x64{1, Poisoned<U8>()}, V2x64{1, Poisoned<U8>()});
  EXPECT_NOT_POISONED(c[0]);
}

TEST(VectorCmpTest, builtin_ia32_ucomisdlt) {
  U4 c;
  c = __builtin_ia32_ucomisdlt(V2x64{Poisoned<U8>(), 1}, V2x64{2, 3});
  EXPECT_POISONED(c);
  c = __builtin_ia32_ucomisdlt(V2x64{1, 2}, V2x64{Poisoned<U8>(), 3});
  EXPECT_POISONED(c);
  c = __builtin_ia32_ucomisdlt(V2x64{1, 2}, V2x64{3, 4});
  EXPECT_NOT_POISONED(c);
  c = __builtin_ia32_ucomisdlt(V2x64{1, Poisoned<U8>()}, V2x64{2, Poisoned<U8>()});
  EXPECT_NOT_POISONED(c);
  c = __builtin_ia32_ucomisdlt(V2x64{1, Poisoned<U8>()}, V2x64{1, Poisoned<U8>()});
  EXPECT_NOT_POISONED(c);
}

#endif // defined(__x86_64__) && defined(__clang__)

TEST(MemorySanitizerOrigins, SetGet) {
  EXPECT_EQ(TrackingOrigins(), !!__msan_get_track_origins());
  if (!TrackingOrigins()) return;
  int x;
  __msan_set_origin(&x, sizeof(x), 1234);
  EXPECT_ORIGIN(1234U, __msan_get_origin(&x));
  __msan_set_origin(&x, sizeof(x), 5678);
  EXPECT_ORIGIN(5678U, __msan_get_origin(&x));
  __msan_set_origin(&x, sizeof(x), 0);
  EXPECT_ORIGIN(0U, __msan_get_origin(&x));
}

namespace {
struct S {
  U4 dummy;
  U2 a;
  U2 b;
};

TEST(MemorySanitizerOrigins, InitializedStoreDoesNotChangeOrigin) {
  if (!TrackingOrigins()) return;

  S s;
  U4 origin = rand();
  s.a = *GetPoisonedO<U2>(0, origin);
  EXPECT_ORIGIN(origin, __msan_get_origin(&s.a));
  EXPECT_ORIGIN(origin, __msan_get_origin(&s.b));

  s.b = 42;
  EXPECT_ORIGIN(origin, __msan_get_origin(&s.a));
  EXPECT_ORIGIN(origin, __msan_get_origin(&s.b));
}
}  // namespace

template<class T, class BinaryOp>
ALWAYS_INLINE
void BinaryOpOriginTest(BinaryOp op) {
  U4 ox = rand();
  U4 oy = rand();
  T *x = GetPoisonedO<T>(0, ox, 0);
  T *y = GetPoisonedO<T>(1, oy, 0);
  T *z = GetPoisonedO<T>(2, 0, 0);

  *z = op(*x, *y);
  U4 origin = __msan_get_origin(z);
  EXPECT_POISONED_O(*z, origin);
  EXPECT_EQ(true, __msan_origin_is_descendant_or_same(origin, ox) ||
                      __msan_origin_is_descendant_or_same(origin, oy));

  // y is poisoned, x is not.
  *x = 10101;
  *y = *GetPoisonedO<T>(1, oy);
  break_optimization(x);
  __msan_set_origin(z, sizeof(*z), 0);
  *z = op(*x, *y);
  EXPECT_POISONED_O(*z, oy);
  EXPECT_ORIGIN(oy, __msan_get_origin(z));

  // x is poisoned, y is not.
  *x = *GetPoisonedO<T>(0, ox);
  *y = 10101010;
  break_optimization(y);
  __msan_set_origin(z, sizeof(*z), 0);
  *z = op(*x, *y);
  EXPECT_POISONED_O(*z, ox);
  EXPECT_ORIGIN(ox, __msan_get_origin(z));
}

template<class T> ALWAYS_INLINE T XOR(const T &a, const T&b) { return a ^ b; }
template<class T> ALWAYS_INLINE T ADD(const T &a, const T&b) { return a + b; }
template<class T> ALWAYS_INLINE T SUB(const T &a, const T&b) { return a - b; }
template<class T> ALWAYS_INLINE T MUL(const T &a, const T&b) { return a * b; }
template<class T> ALWAYS_INLINE T AND(const T &a, const T&b) { return a & b; }
template<class T> ALWAYS_INLINE T OR (const T &a, const T&b) { return a | b; }

TEST(MemorySanitizerOrigins, BinaryOp) {
  if (!TrackingOrigins()) return;
  BinaryOpOriginTest<S8>(XOR<S8>);
  BinaryOpOriginTest<U8>(ADD<U8>);
  BinaryOpOriginTest<S4>(SUB<S4>);
  BinaryOpOriginTest<S4>(MUL<S4>);
  BinaryOpOriginTest<U4>(OR<U4>);
  BinaryOpOriginTest<U4>(AND<U4>);
  BinaryOpOriginTest<double>(ADD<U4>);
  BinaryOpOriginTest<float>(ADD<S4>);
  BinaryOpOriginTest<double>(ADD<double>);
  BinaryOpOriginTest<float>(ADD<double>);
}

TEST(MemorySanitizerOrigins, Unary) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<U4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);

  EXPECT_POISONED_O((void*)*GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O((U8)*GetPoisonedO<void*>(0, __LINE__), __LINE__);
}

TEST(MemorySanitizerOrigins, EQ) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__) <= 11, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__) == 11, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<float>(0, __LINE__) == 1.1f, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<double>(0, __LINE__) == 1.1, __LINE__);
}

TEST(MemorySanitizerOrigins, DIV) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<U8>(0, __LINE__) / 100, __LINE__);
  unsigned o = __LINE__;
  EXPECT_UMR_O(volatile unsigned y = 100 / *GetPoisonedO<S4>(0, o, 1), o);
}

TEST(MemorySanitizerOrigins, SHIFT) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(*GetPoisonedO<U8>(0, __LINE__) >> 10, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__) >> 10, __LINE__);
  EXPECT_POISONED_O(*GetPoisonedO<S8>(0, __LINE__) << 10, __LINE__);
  EXPECT_POISONED_O(10U << *GetPoisonedO<U8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(-10 >> *GetPoisonedO<S8>(0, __LINE__), __LINE__);
  EXPECT_POISONED_O(-10 << *GetPoisonedO<S8>(0, __LINE__), __LINE__);
}

template<class T, int N>
void MemCpyTest() {
  int ox = __LINE__;
  T *x = new T[N];
  T *y = new T[N];
  T *z = new T[N];
  T *q = new T[N];
  __msan_poison(x, N * sizeof(T));
  __msan_set_origin(x, N * sizeof(T), ox);
  __msan_set_origin(y, N * sizeof(T), 777777);
  __msan_set_origin(z, N * sizeof(T), 888888);
  EXPECT_NOT_POISONED(x);
  memcpy(y, x, N * sizeof(T));
  EXPECT_POISONED_O(y[0], ox);
  EXPECT_POISONED_O(y[N/2], ox);
  EXPECT_POISONED_O(y[N-1], ox);
  EXPECT_NOT_POISONED(x);
#if !defined(__NetBSD__)
  void *res = mempcpy(q, x, N * sizeof(T));
  ASSERT_EQ(q + N, res);
  EXPECT_POISONED_O(q[0], ox);
  EXPECT_POISONED_O(q[N/2], ox);
  EXPECT_POISONED_O(q[N-1], ox);
  EXPECT_NOT_POISONED(x);
#endif
  memmove(z, x, N * sizeof(T));
  EXPECT_POISONED_O(z[0], ox);
  EXPECT_POISONED_O(z[N/2], ox);
  EXPECT_POISONED_O(z[N-1], ox);
}

TEST(MemorySanitizerOrigins, LargeMemCpy) {
  if (!TrackingOrigins()) return;
  MemCpyTest<U1, 10000>();
  MemCpyTest<U8, 10000>();
}

TEST(MemorySanitizerOrigins, SmallMemCpy) {
  if (!TrackingOrigins()) return;
  MemCpyTest<U8, 1>();
  MemCpyTest<U8, 2>();
  MemCpyTest<U8, 3>();
}

TEST(MemorySanitizerOrigins, Select) {
  if (!TrackingOrigins()) return;
  EXPECT_NOT_POISONED(g_one ? 1 : *GetPoisonedO<S4>(0, __LINE__));
  EXPECT_POISONED_O(*GetPoisonedO<S4>(0, __LINE__), __LINE__);
  S4 x;
  break_optimization(&x);
  x = g_1 ? *GetPoisonedO<S4>(0, __LINE__) : 0;

  EXPECT_POISONED_O(g_1 ? *GetPoisonedO<S4>(0, __LINE__) : 1, __LINE__);
  EXPECT_POISONED_O(g_0 ? 1 : *GetPoisonedO<S4>(0, __LINE__), __LINE__);
}

NOINLINE int RetvalOriginTest(U4 origin) {
  int *a = new int;
  break_optimization(a);
  __msan_set_origin(a, sizeof(*a), origin);
  int res = *a;
  delete a;
  return res;
}

TEST(MemorySanitizerOrigins, Retval) {
  if (!TrackingOrigins()) return;
  EXPECT_POISONED_O(RetvalOriginTest(__LINE__), __LINE__);
}

NOINLINE void ParamOriginTest(int param, U4 origin) {
  EXPECT_POISONED_O(param, origin);
}

TEST(MemorySanitizerOrigins, Param) {
  if (!TrackingOrigins()) return;
  int *a = new int;
  U4 origin = __LINE__;
  break_optimization(a);
  __msan_set_origin(a, sizeof(*a), origin);
  ParamOriginTest(*a, origin);
  delete a;
}

TEST(MemorySanitizerOrigins, Invoke) {
  if (!TrackingOrigins()) return;
  StructWithDtor s;  // Will cause the calls to become invokes.
  EXPECT_POISONED_O(RetvalOriginTest(__LINE__), __LINE__);
}

TEST(MemorySanitizerOrigins, strlen) {
  S8 alignment;
  break_optimization(&alignment);
  char x[4] = {'a', 'b', 0, 0};
  __msan_poison(&x[2], 1);
  U4 origin = __LINE__;
  __msan_set_origin(x, sizeof(x), origin);
  EXPECT_UMR_O(volatile unsigned y = strlen(x), origin);
}

TEST(MemorySanitizerOrigins, wcslen) {
  wchar_t w[3] = {'a', 'b', 0};
  U4 origin = __LINE__;
  __msan_set_origin(w, sizeof(w), origin);
  __msan_poison(&w[2], sizeof(wchar_t));
  EXPECT_UMR_O(volatile unsigned y = wcslen(w), origin);
}

#if MSAN_HAS_M128
TEST(MemorySanitizerOrigins, StoreIntrinsic) {
  __m128 x, y;
  U4 origin = __LINE__;
  __msan_set_origin(&x, sizeof(x), origin);
  __msan_poison(&x, sizeof(x));
  _mm_storeu_ps((float*)&y, x);
  EXPECT_POISONED_O(y, origin);
}
#endif

NOINLINE void RecursiveMalloc(int depth) {
  static int count;
  count++;
  if ((count % (1024 * 1024)) == 0)
    printf("RecursiveMalloc: %d\n", count);
  int *x1 = new int;
  int *x2 = new int;
  break_optimization(x1);
  break_optimization(x2);
  if (depth > 0) {
    RecursiveMalloc(depth-1);
    RecursiveMalloc(depth-1);
  }
  delete x1;
  delete x2;
}

TEST(MemorySanitizer, Select) {
  int x;
  int volatile* p = &x;
  int z = *p ? 1 : 0;
  EXPECT_POISONED(z);
}

TEST(MemorySanitizer, SelectPartial) {
  // Precise instrumentation of select.
  // Some bits of the result do not depend on select condition, and must stay
  // initialized even if select condition is not. These are the bits that are
  // equal and initialized in both left and right select arguments.
  U4 x = 0xFFFFABCDU;
  U4 x_s = 0xFFFF0000U;
  __msan_partial_poison(&x, &x_s, sizeof(x));
  U4 y = 0xAB00U;
  U1 cond = true;
  __msan_poison(&cond, sizeof(cond));
  U4 z = cond ? x : y;
  __msan_print_shadow(&z, sizeof(z));
  EXPECT_POISONED(z & 0xFFU);
  EXPECT_NOT_POISONED(z & 0xFF00U);
  EXPECT_POISONED(z & 0xFF0000U);
  EXPECT_POISONED(z & 0xFF000000U);
  EXPECT_EQ(0xAB00U, z & 0xFF00U);
}

TEST(MemorySanitizerStress, DISABLED_MallocStackTrace) {
  RecursiveMalloc(22);
}

TEST(MemorySanitizerAllocator, get_estimated_allocated_size) {
  size_t sizes[] = {0, 20, 5000, 1<<20};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
    size_t alloc_size = __sanitizer_get_estimated_allocated_size(sizes[i]);
    EXPECT_EQ(alloc_size, sizes[i]);
  }
}

TEST(MemorySanitizerAllocator, get_allocated_size_and_ownership) {
  char *array = reinterpret_cast<char*>(malloc(100));
  int *int_ptr = new int;

  EXPECT_TRUE(__sanitizer_get_ownership(array));
  EXPECT_EQ(100U, __sanitizer_get_allocated_size(array));

  EXPECT_TRUE(__sanitizer_get_ownership(int_ptr));
  EXPECT_EQ(sizeof(*int_ptr), __sanitizer_get_allocated_size(int_ptr));

  void *wild_addr = reinterpret_cast<void*>(0x1);
  EXPECT_FALSE(__sanitizer_get_ownership(wild_addr));
  EXPECT_EQ(0U, __sanitizer_get_allocated_size(wild_addr));

  EXPECT_FALSE(__sanitizer_get_ownership(array + 50));
  EXPECT_EQ(0U, __sanitizer_get_allocated_size(array + 50));

  // NULL is a valid argument for GetAllocatedSize but is not owned.
  EXPECT_FALSE(__sanitizer_get_ownership(NULL));
  EXPECT_EQ(0U, __sanitizer_get_allocated_size(NULL));

  free(array);
  EXPECT_FALSE(__sanitizer_get_ownership(array));
  EXPECT_EQ(0U, __sanitizer_get_allocated_size(array));

  delete int_ptr;
}

TEST(MemorySanitizer, MlockTest) {
  EXPECT_EQ(0, mlockall(MCL_CURRENT));
  EXPECT_EQ(0, mlock((void*)0x12345, 0x5678));
  EXPECT_EQ(0, munlockall());
  EXPECT_EQ(0, munlock((void*)0x987, 0x654));
}

// Test that LargeAllocator unpoisons memory before releasing it to the OS.
TEST(MemorySanitizer, LargeAllocatorUnpoisonsOnFree) {
  void *p = malloc(1024 * 1024);
  free(p);

  typedef void *(*mmap_fn)(void *, size_t, int, int, int, off_t);
  mmap_fn real_mmap = (mmap_fn)dlsym(RTLD_NEXT, "mmap");

  // Allocate the page that was released to the OS in free() with the real mmap,
  // bypassing the interceptor.
  char *q = (char *)real_mmap(p, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE((char *)0, q);

  ASSERT_TRUE(q <= p);
  ASSERT_TRUE(q + 4096 > p);

  EXPECT_NOT_POISONED(q[0]);
  EXPECT_NOT_POISONED(q[10]);
  EXPECT_NOT_POISONED(q[100]);

  munmap(q, 4096);
}

#if SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE
TEST(MemorySanitizer, MallocUsableSizeTest) {
  const size_t kArraySize = 100;
  char *array = Ident((char*)malloc(kArraySize));
  int *int_ptr = Ident(new int);
  EXPECT_EQ(0U, malloc_usable_size(NULL));
  EXPECT_EQ(kArraySize, malloc_usable_size(array));
  EXPECT_EQ(sizeof(int), malloc_usable_size(int_ptr));
  free(array);
  delete int_ptr;
}
#endif  // SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE

#ifdef __x86_64__
static bool HaveBmi() {
  U4 a = 0, b = 0, c = 0, d = 0;
  asm("cpuid\n\t" : "=a"(a), "=D"(b), "=c"(c), "=d"(d) : "a"(7));
  const U4 kBmi12Mask = (1U<<3) | (1U<<8);
  return (b & kBmi12Mask) == kBmi12Mask;
}

__attribute__((target("bmi,bmi2")))
static void TestBZHI() {
  EXPECT_NOT_POISONED(
      __builtin_ia32_bzhi_si(Poisoned<U4>(0xABCDABCD, 0xFF000000), 24));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_si(Poisoned<U4>(0xABCDABCD, 0xFF800000), 24));
  // Second operand saturates.
  EXPECT_POISONED(
      __builtin_ia32_bzhi_si(Poisoned<U4>(0xABCDABCD, 0x80000000), 240));
  // Any poison in the second operand poisons output.
  EXPECT_POISONED(
      __builtin_ia32_bzhi_si(0xABCDABCD, Poisoned<U4>(1, 1)));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_si(0xABCDABCD, Poisoned<U4>(1, 0x80000000)));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_si(0xABCDABCD, Poisoned<U4>(1, 0xFFFFFFFF)));

  EXPECT_NOT_POISONED(
      __builtin_ia32_bzhi_di(Poisoned<U8>(0xABCDABCDABCDABCD, 0xFF00000000000000ULL), 56));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_di(Poisoned<U8>(0xABCDABCDABCDABCD, 0xFF80000000000000ULL), 56));
  // Second operand saturates.
  EXPECT_POISONED(
      __builtin_ia32_bzhi_di(Poisoned<U8>(0xABCDABCDABCDABCD, 0x8000000000000000ULL), 240));
  // Any poison in the second operand poisons output.
  EXPECT_POISONED(
      __builtin_ia32_bzhi_di(0xABCDABCDABCDABCD, Poisoned<U8>(1, 1)));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_di(0xABCDABCDABCDABCD, Poisoned<U8>(1, 0x8000000000000000ULL)));
  EXPECT_POISONED(
      __builtin_ia32_bzhi_di(0xABCDABCDABCDABCD, Poisoned<U8>(1, 0xFFFFFFFF00000000ULL)));
}

ALWAYS_INLINE U4 bextr_imm(U4 start, U4 len) {
  start &= 0xFF;
  len &= 0xFF;
  return (len << 8) | start;
}

__attribute__((target("bmi,bmi2")))
static void TestBEXTR() {
  EXPECT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(0, 8)));
  EXPECT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(7, 8)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(8, 8)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(8, 800)));
  EXPECT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(7, 800)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u32(Poisoned<U4>(0xABCDABCD, 0xFF), bextr_imm(5, 0)));

  EXPECT_POISONED(
      __builtin_ia32_bextr_u32(0xABCDABCD, Poisoned<U4>(bextr_imm(7, 800), 1)));
  EXPECT_POISONED(__builtin_ia32_bextr_u32(
      0xABCDABCD, Poisoned<U4>(bextr_imm(7, 800), 0x80000000)));

  EXPECT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(0, 8)));
  EXPECT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(7, 8)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(8, 8)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(8, 800)));
  EXPECT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(7, 800)));
  EXPECT_NOT_POISONED(
      __builtin_ia32_bextr_u64(Poisoned<U8>(0xABCDABCD, 0xFF), bextr_imm(5, 0)));

  // Poison in the top half.
  EXPECT_NOT_POISONED(__builtin_ia32_bextr_u64(
      Poisoned<U8>(0xABCDABCD, 0xFF0000000000), bextr_imm(32, 8)));
  EXPECT_POISONED(__builtin_ia32_bextr_u64(
      Poisoned<U8>(0xABCDABCD, 0xFF0000000000), bextr_imm(32, 9)));

  EXPECT_POISONED(
      __builtin_ia32_bextr_u64(0xABCDABCD, Poisoned<U8>(bextr_imm(7, 800), 1)));
  EXPECT_POISONED(__builtin_ia32_bextr_u64(
      0xABCDABCD, Poisoned<U8>(bextr_imm(7, 800), 0x80000000)));
}

__attribute__((target("bmi,bmi2")))
static void TestPDEP() {
  U4 x = Poisoned<U4>(0, 0xFF00);
  EXPECT_NOT_POISONED(__builtin_ia32_pdep_si(x, 0xFF));
  EXPECT_POISONED(__builtin_ia32_pdep_si(x, 0x1FF));
  EXPECT_NOT_POISONED(__builtin_ia32_pdep_si(x, 0xFF00));
  EXPECT_POISONED(__builtin_ia32_pdep_si(x, 0x1FF00));

  EXPECT_NOT_POISONED(__builtin_ia32_pdep_si(x, 0x1FF00) & 0xFF);
  EXPECT_POISONED(__builtin_ia32_pdep_si(0, Poisoned<U4>(0xF, 1)));

  U8 y = Poisoned<U8>(0, 0xFF00);
  EXPECT_NOT_POISONED(__builtin_ia32_pdep_di(y, 0xFF));
  EXPECT_POISONED(__builtin_ia32_pdep_di(y, 0x1FF));
  EXPECT_NOT_POISONED(__builtin_ia32_pdep_di(y, 0xFF0000000000));
  EXPECT_POISONED(__builtin_ia32_pdep_di(y, 0x1FF000000000000));

  EXPECT_NOT_POISONED(__builtin_ia32_pdep_di(y, 0x1FF00) & 0xFF);
  EXPECT_POISONED(__builtin_ia32_pdep_di(0, Poisoned<U4>(0xF, 1)));
}

__attribute__((target("bmi,bmi2")))
static void TestPEXT() {
  U4 x = Poisoned<U4>(0, 0xFF00);
  EXPECT_NOT_POISONED(__builtin_ia32_pext_si(x, 0xFF));
  EXPECT_POISONED(__builtin_ia32_pext_si(x, 0x1FF));
  EXPECT_POISONED(__builtin_ia32_pext_si(x, 0x100));
  EXPECT_POISONED(__builtin_ia32_pext_si(x, 0x1000));
  EXPECT_NOT_POISONED(__builtin_ia32_pext_si(x, 0x10000));

  EXPECT_POISONED(__builtin_ia32_pext_si(0xFF00, Poisoned<U4>(0xFF, 1)));

  U8 y = Poisoned<U8>(0, 0xFF0000000000);
  EXPECT_NOT_POISONED(__builtin_ia32_pext_di(y, 0xFF00000000));
  EXPECT_POISONED(__builtin_ia32_pext_di(y, 0x1FF00000000));
  EXPECT_POISONED(__builtin_ia32_pext_di(y, 0x10000000000));
  EXPECT_POISONED(__builtin_ia32_pext_di(y, 0x100000000000));
  EXPECT_NOT_POISONED(__builtin_ia32_pext_di(y, 0x1000000000000));

  EXPECT_POISONED(__builtin_ia32_pext_di(0xFF00, Poisoned<U8>(0xFF, 1)));
}

TEST(MemorySanitizer, Bmi) {
  if (HaveBmi()) {
    TestBZHI();
    TestBEXTR();
    TestPDEP();
    TestPEXT();
  }
}
#endif // defined(__x86_64__)

namespace {
volatile long z;

__attribute__((noinline,optnone)) void f(long a, long b, long c, long d, long e, long f) {
  z = a + b + c + d + e + f;
}

__attribute__((noinline,optnone)) void throw_stuff() {
  throw 5;
}

TEST(MemorySanitizer, throw_catch) {
  long x;
  // Poison __msan_param_tls.
  __msan_poison(&x, sizeof(x));
  f(x, x, x, x, x, x);
  try {
    // This calls __gxx_personality_v0 through some libgcc_s function.
    // __gxx_personality_v0 is instrumented, libgcc_s is not; as a result,
    // __msan_param_tls is not updated and __gxx_personality_v0 can find
    // leftover poison from the previous call.
    // A suppression in msan_ignorelist.txt makes it work.
    throw_stuff();
  } catch (const int &e) {
    // pass
  }
}
} // namespace
