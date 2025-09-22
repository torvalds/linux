//===-- asan_test.cpp -----------------------------------------------------===//
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

#include <errno.h>
#include <stdarg.h>

#ifdef _LIBCPP_GET_C_LOCALE
#define SANITIZER_GET_C_LOCALE _LIBCPP_GET_C_LOCALE
#else
#if defined(__FreeBSD__)
#define SANITIZER_GET_C_LOCALE 0
#elif defined(__NetBSD__)
#define SANITIZER_GET_C_LOCALE LC_C_LOCALE
#endif
#endif

#if defined(__sun__) && defined(__svr4__)
using std::_setjmp;
using std::_longjmp;
#endif

NOINLINE void *malloc_fff(size_t size) {
  void *res = malloc/**/(size); break_optimization(0); return res;}
NOINLINE void *malloc_eee(size_t size) {
  void *res = malloc_fff(size); break_optimization(0); return res;}
NOINLINE void *malloc_ddd(size_t size) {
  void *res = malloc_eee(size); break_optimization(0); return res;}
NOINLINE void *malloc_ccc(size_t size) {
  void *res = malloc_ddd(size); break_optimization(0); return res;}
NOINLINE void *malloc_bbb(size_t size) {
  void *res = malloc_ccc(size); break_optimization(0); return res;}
NOINLINE void *malloc_aaa(size_t size) {
  void *res = malloc_bbb(size); break_optimization(0); return res;}

NOINLINE void free_ccc(void *p) { free(p); break_optimization(0);}
NOINLINE void free_bbb(void *p) { free_ccc(p); break_optimization(0);}
NOINLINE void free_aaa(void *p) { free_bbb(p); break_optimization(0);}

template<typename T>
NOINLINE void uaf_test(int size, int off) {
  void *p = malloc_aaa(size);
  free_aaa(p);
  for (int i = 1; i < 100; i++)
    free_aaa(malloc_aaa(i));
  fprintf(stderr, "writing %ld byte(s) at %p with offset %d\n",
          (long)sizeof(T), p, off);
  asan_write((T *)((char *)p + off));
}

TEST(AddressSanitizer, HasFeatureAddressSanitizerTest) {
#if defined(__has_feature) && __has_feature(address_sanitizer)
  bool asan = 1;
#elif defined(__SANITIZE_ADDRESS__)
  bool asan = 1;
#else
  bool asan = 0;
#endif
  EXPECT_EQ(true, asan);
}

TEST(AddressSanitizer, SimpleDeathTest) {
  EXPECT_DEATH(exit(1), "");
}

TEST(AddressSanitizer, VariousMallocsTest) {
  int *a = (int*)malloc(100 * sizeof(int));
  a[50] = 0;
  free(a);

  int *r = (int*)malloc(10);
  r = (int*)realloc(r, 2000 * sizeof(int));
  r[1000] = 0;
  free(r);

  int *b = new int[100];
  b[50] = 0;
  delete [] b;

  int *c = new int;
  *c = 0;
  delete c;

#if SANITIZER_TEST_HAS_POSIX_MEMALIGN
  void *pm = 0;
  // Valid allocation.
  int pm_res = posix_memalign(&pm, kPageSize, kPageSize);
  EXPECT_EQ(0, pm_res);
  EXPECT_NE(nullptr, pm);
  free(pm);
#endif  // SANITIZER_TEST_HAS_POSIX_MEMALIGN

#if SANITIZER_TEST_HAS_MEMALIGN
  int *ma = (int*)memalign(kPageSize, kPageSize);
  EXPECT_EQ(0U, (uintptr_t)ma % kPageSize);
  ma[123] = 0;
  free(ma);
#endif  // SANITIZER_TEST_HAS_MEMALIGN
}

TEST(AddressSanitizer, CallocTest) {
  int *a = (int*)calloc(100, sizeof(int));
  EXPECT_EQ(0, a[10]);
  free(a);
}

TEST(AddressSanitizer, CallocReturnsZeroMem) {
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
#if !defined(_WIN32)
      // FIXME: OOM on Windows. We should just make this a lit test
      // with quarantine size set to 1.
      free(Ident(malloc(Ident(1 << 27))));  // Try to drain the quarantine.
#endif
    }
  }
}

// No valloc on Windows or Android.
#if !defined(_WIN32) && !defined(__ANDROID__)
TEST(AddressSanitizer, VallocTest) {
  void *a = valloc(100);
  EXPECT_EQ(0U, (uintptr_t)a % kPageSize);
  free(a);
}
#endif

#if SANITIZER_TEST_HAS_PVALLOC
TEST(AddressSanitizer, PvallocTest) {
  char *a = (char*)pvalloc(kPageSize + 100);
  EXPECT_EQ(0U, (uintptr_t)a % kPageSize);
  a[kPageSize + 101] = 1;  // we should not report an error here.
  free(a);

  a = (char*)pvalloc(0);  // pvalloc(0) should allocate at least one page.
  EXPECT_EQ(0U, (uintptr_t)a % kPageSize);
  a[101] = 1;  // we should not report an error here.
  free(a);
}
#endif  // SANITIZER_TEST_HAS_PVALLOC

#if !defined(_WIN32)
// FIXME: Use an equivalent of pthread_setspecific on Windows.
void *TSDWorker(void *test_key) {
  if (test_key) {
    pthread_setspecific(*(pthread_key_t*)test_key, (void*)0xfeedface);
  }
  return NULL;
}

void TSDDestructor(void *tsd) {
  // Spawning a thread will check that the current thread id is not -1.
  pthread_t th;
  PTHREAD_CREATE(&th, NULL, TSDWorker, NULL);
  PTHREAD_JOIN(th, NULL);
}

// This tests triggers the thread-specific data destruction fiasco which occurs
// if we don't manage the TSD destructors ourselves. We create a new pthread
// key with a non-NULL destructor which is likely to be put after the destructor
// of AsanThread in the list of destructors.
// In this case the TSD for AsanThread will be destroyed before TSDDestructor
// is called for the child thread, and a CHECK will fail when we call
// pthread_create() to spawn the grandchild.
TEST(AddressSanitizer, DISABLED_TSDTest) {
  pthread_t th;
  pthread_key_t test_key;
  pthread_key_create(&test_key, TSDDestructor);
  PTHREAD_CREATE(&th, NULL, TSDWorker, &test_key);
  PTHREAD_JOIN(th, NULL);
  pthread_key_delete(test_key);
}
#endif

TEST(AddressSanitizer, UAF_char) {
  const char *uaf_string = "AddressSanitizer:.*heap-use-after-free";
  EXPECT_DEATH(uaf_test<U1>(1, 0), uaf_string);
  EXPECT_DEATH(uaf_test<U1>(10, 0), uaf_string);
  EXPECT_DEATH(uaf_test<U1>(10, 10), uaf_string);
  EXPECT_DEATH(uaf_test<U1>(kLargeMalloc, 0), uaf_string);
  EXPECT_DEATH(uaf_test<U1>(kLargeMalloc, kLargeMalloc / 2), uaf_string);
}

TEST(AddressSanitizer, UAF_long_double) {
  if (sizeof(long double) == sizeof(double)) return;
  long double *p = Ident(new long double[10]);
#if defined(_WIN32)
  // https://google.github.io/googletest/advanced.html#regular-expression-syntax
  // GoogleTest's regular expression engine on Windows does not support `[]`
  // brackets.
  EXPECT_DEATH(Ident(p)[12] = 0, "WRITE of size 10");
  EXPECT_DEATH(Ident(p)[0] = Ident(p)[12], "READ of size 10");
#else
  EXPECT_DEATH(Ident(p)[12] = 0, "WRITE of size 1[026]");
  EXPECT_DEATH(Ident(p)[0] = Ident(p)[12], "READ of size 1[026]");
#endif
  delete [] Ident(p);
}

#if !defined(_WIN32)
struct Packed5 {
  int x;
  char c;
} __attribute__((packed));
#else
# pragma pack(push, 1)
struct Packed5 {
  int x;
  char c;
};
# pragma pack(pop)
#endif

TEST(AddressSanitizer, UAF_Packed5) {
  static_assert(sizeof(Packed5) == 5, "Please check the keywords used");
  Packed5 *p = Ident(new Packed5[2]);
  EXPECT_DEATH(p[0] = p[3], "READ of size 5");
  EXPECT_DEATH(p[3] = p[0], "WRITE of size 5");
  delete [] Ident(p);
}

#if ASAN_HAS_IGNORELIST
TEST(AddressSanitizer, IgnoreTest) {
  int *x = Ident(new int);
  delete Ident(x);
  *x = 0;
}
#endif  // ASAN_HAS_IGNORELIST

struct StructWithBitField {
  int bf1:1;
  int bf2:1;
  int bf3:1;
  int bf4:29;
};

TEST(AddressSanitizer, BitFieldPositiveTest) {
  StructWithBitField *x = new StructWithBitField;
  delete Ident(x);
  EXPECT_DEATH(x->bf1 = 0, "use-after-free");
  EXPECT_DEATH(x->bf2 = 0, "use-after-free");
  EXPECT_DEATH(x->bf3 = 0, "use-after-free");
  EXPECT_DEATH(x->bf4 = 0, "use-after-free");
}

struct StructWithBitFields_8_24 {
  int a:8;
  int b:24;
};

TEST(AddressSanitizer, BitFieldNegativeTest) {
  StructWithBitFields_8_24 *x = Ident(new StructWithBitFields_8_24);
  x->a = 0;
  x->b = 0;
  delete Ident(x);
}

#if ASAN_NEEDS_SEGV
namespace {

const char kSEGVCrash[] = "AddressSanitizer: SEGV on unknown address";
const char kOverriddenSigactionHandler[] = "Test sigaction handler\n";
const char kOverriddenSignalHandler[] = "Test signal handler\n";

TEST(AddressSanitizer, WildAddressTest) {
  char *c = (char*)0x123;
  EXPECT_DEATH(*c = 0, kSEGVCrash);
}

void my_sigaction_sighandler(int, siginfo_t*, void*) {
  fprintf(stderr, kOverriddenSigactionHandler);
  exit(1);
}

void my_signal_sighandler(int signum) {
  fprintf(stderr, kOverriddenSignalHandler);
  exit(1);
}

TEST(AddressSanitizer, SignalTest) {
  struct sigaction sigact;
  memset(&sigact, 0, sizeof(sigact));
  sigact.sa_sigaction = my_sigaction_sighandler;
  sigact.sa_flags = SA_SIGINFO;
  char *c = (char *)0x123;

  EXPECT_DEATH(*c = 0, kSEGVCrash);

  // ASan should allow to set sigaction()...
  EXPECT_EQ(0, sigaction(SIGSEGV, &sigact, 0));
#ifdef __APPLE__
  EXPECT_EQ(0, sigaction(SIGBUS, &sigact, 0));
#endif
  EXPECT_DEATH(*c = 0, kOverriddenSigactionHandler);

  // ... and signal().
  EXPECT_NE(SIG_ERR, signal(SIGSEGV, my_signal_sighandler));
  EXPECT_DEATH(*c = 0, kOverriddenSignalHandler);
}
}  // namespace
#endif

static void TestLargeMalloc(size_t size) {
  char buff[1024];
  sprintf(buff, "is located 1 bytes before %lu-byte", (long)size);
  EXPECT_DEATH(Ident((char*)malloc(size))[-1] = 0, buff);
}

TEST(AddressSanitizer, LargeMallocTest) {
  const int max_size = (SANITIZER_WORDSIZE == 32) ? 1 << 26 : 1 << 28;
  for (int i = 113; i < max_size; i = i * 2 + 13) {
    TestLargeMalloc(i);
  }
}

#if !GTEST_USES_SIMPLE_RE
TEST(AddressSanitizer, HugeMallocTest) {
  if (SANITIZER_WORDSIZE != 64 || ASAN_AVOID_EXPENSIVE_TESTS) return;
  size_t n_megs = 4100;
  EXPECT_DEATH(Ident((char*)malloc(n_megs << 20))[-1] = 0,
               "is located 1 bytes before|"
               "AddressSanitizer failed to allocate");
}
#endif

#if SANITIZER_TEST_HAS_MEMALIGN
void MemalignRun(size_t align, size_t size, int idx) {
  char *p = (char *)memalign(align, size);
  Ident(p)[idx] = 0;
  free(p);
}

TEST(AddressSanitizer, memalign) {
  for (int align = 16; align <= (1 << 23); align *= 2) {
    size_t size = align * 5;
    EXPECT_DEATH(MemalignRun(align, size, -1),
                 "is located 1 bytes before");
    EXPECT_DEATH(MemalignRun(align, size, size + 1),
                 "is located 1 bytes after");
  }
}
#endif  // SANITIZER_TEST_HAS_MEMALIGN

void *ManyThreadsWorker(void *a) {
  for (int iter = 0; iter < 100; iter++) {
    for (size_t size = 100; size < 2000; size *= 2) {
      free(Ident(malloc(size)));
    }
  }
  return 0;
}

#if !defined(__aarch64__) && !defined(__powerpc64__)
// FIXME: Infinite loop in AArch64 (PR24389).
// FIXME: Also occasional hang on powerpc.  Maybe same problem as on AArch64?
TEST(AddressSanitizer, ManyThreadsTest) {
  const size_t kNumThreads =
      (SANITIZER_WORDSIZE == 32 || ASAN_AVOID_EXPENSIVE_TESTS) ? 30 : 1000;
  pthread_t t[kNumThreads];
  for (size_t i = 0; i < kNumThreads; i++) {
    PTHREAD_CREATE(&t[i], 0, ManyThreadsWorker, (void*)i);
  }
  for (size_t i = 0; i < kNumThreads; i++) {
    PTHREAD_JOIN(t[i], 0);
  }
}
#endif

TEST(AddressSanitizer, ReallocTest) {
  const int kMinElem = 5;
  int *ptr = (int*)malloc(sizeof(int) * kMinElem);
  ptr[3] = 3;
  for (int i = 0; i < 10000; i++) {
    ptr = (int*)realloc(ptr,
        (my_rand() % 1000 + kMinElem) * sizeof(int));
    EXPECT_EQ(3, ptr[3]);
  }
  free(ptr);
  // Realloc pointer returned by malloc(0).
  int *ptr2 = Ident((int*)malloc(0));
  ptr2 = Ident((int*)realloc(ptr2, sizeof(*ptr2)));
  *ptr2 = 42;
  EXPECT_EQ(42, *ptr2);
  free(ptr2);
}

TEST(AddressSanitizer, ReallocFreedPointerTest) {
  void *ptr = Ident(malloc(42));
  ASSERT_TRUE(NULL != ptr);
  free(ptr);
  EXPECT_DEATH(ptr = realloc(ptr, 77), "attempting double-free");
}

TEST(AddressSanitizer, ReallocInvalidPointerTest) {
  void *ptr = Ident(malloc(42));
  EXPECT_DEATH(ptr = realloc((int*)ptr + 1, 77), "attempting free.*not malloc");
  free(ptr);
}

TEST(AddressSanitizer, ZeroSizeMallocTest) {
  // Test that malloc(0) and similar functions don't return NULL.
  void *ptr = Ident(malloc(0));
  EXPECT_TRUE(NULL != ptr);
  free(ptr);
#if SANITIZER_TEST_HAS_POSIX_MEMALIGN
  int pm_res = posix_memalign(&ptr, 1<<20, 0);
  EXPECT_EQ(0, pm_res);
  EXPECT_TRUE(NULL != ptr);
  free(ptr);
#endif  // SANITIZER_TEST_HAS_POSIX_MEMALIGN
  int *int_ptr = new int[0];
  int *int_ptr2 = new int[0];
  EXPECT_TRUE(NULL != int_ptr);
  EXPECT_TRUE(NULL != int_ptr2);
  EXPECT_NE(int_ptr, int_ptr2);
  delete[] int_ptr;
  delete[] int_ptr2;
}

#if SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE
static const char *kMallocUsableSizeErrorMsg =
  "AddressSanitizer: attempting to call malloc_usable_size()";

TEST(AddressSanitizer, MallocUsableSizeTest) {
  const size_t kArraySize = 100;
  char *array = Ident((char*)malloc(kArraySize));
  int *int_ptr = Ident(new int);
  EXPECT_EQ(0U, malloc_usable_size(NULL));
  EXPECT_EQ(kArraySize, malloc_usable_size(array));
  EXPECT_EQ(sizeof(int), malloc_usable_size(int_ptr));
  EXPECT_DEATH(malloc_usable_size((void*)0x123), kMallocUsableSizeErrorMsg);
  EXPECT_DEATH(malloc_usable_size(array + kArraySize / 2),
               kMallocUsableSizeErrorMsg);
  free(array);
  EXPECT_DEATH(malloc_usable_size(array), kMallocUsableSizeErrorMsg);
  delete int_ptr;
}
#endif  // SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE

void WrongFree() {
  int *x = (int*)malloc(100 * sizeof(int));
  // Use the allocated memory, otherwise Clang will optimize it out.
  Ident(x);
  free(x + 1);
}

#if !defined(_WIN32)  // FIXME: This should be a lit test.
TEST(AddressSanitizer, WrongFreeTest) {
  EXPECT_DEATH(WrongFree(), ASAN_PCRE_DOTALL
               "ERROR: AddressSanitizer: attempting free.*not malloc"
               ".*is located 4 bytes inside of 400-byte region"
               ".*allocated by thread");
}
#endif

void DoubleFree() {
  int *x = (int*)malloc(100 * sizeof(int));
  fprintf(stderr, "DoubleFree: x=%p\n", (void *)x);
  free(x);
  free(x);
  fprintf(stderr, "should have failed in the second free(%p)\n", (void *)x);
  abort();
}

#if !defined(_WIN32)  // FIXME: This should be a lit test.
TEST(AddressSanitizer, DoubleFreeTest) {
  EXPECT_DEATH(DoubleFree(), ASAN_PCRE_DOTALL
               "ERROR: AddressSanitizer: attempting double-free"
               ".*is located 0 bytes inside of 400-byte region"
               ".*freed by thread T0 here"
               ".*previously allocated by thread T0 here");
}
#endif

template<int kSize>
NOINLINE void SizedStackTest() {
  char a[kSize];
  char  *A = Ident((char*)&a);
  const char *expected_death = "AddressSanitizer: stack-buffer-";
  for (size_t i = 0; i < kSize; i++)
    A[i] = i;
  EXPECT_DEATH(A[-1] = 0, expected_death);
  EXPECT_DEATH(A[-5] = 0, expected_death);
  EXPECT_DEATH(A[kSize] = 0, expected_death);
  EXPECT_DEATH(A[kSize + 1] = 0, expected_death);
  EXPECT_DEATH(A[kSize + 5] = 0, expected_death);
  if (kSize > 16)
    EXPECT_DEATH(A[kSize + 31] = 0, expected_death);
}

TEST(AddressSanitizer, SimpleStackTest) {
  SizedStackTest<1>();
  SizedStackTest<2>();
  SizedStackTest<3>();
  SizedStackTest<4>();
  SizedStackTest<5>();
  SizedStackTest<6>();
  SizedStackTest<7>();
  SizedStackTest<16>();
  SizedStackTest<25>();
  SizedStackTest<34>();
  SizedStackTest<43>();
  SizedStackTest<51>();
  SizedStackTest<62>();
  SizedStackTest<64>();
  SizedStackTest<128>();
}

#if !defined(_WIN32)
// FIXME: It's a bit hard to write multi-line death test expectations
// in a portable way.  Anyways, this should just be turned into a lit test.
TEST(AddressSanitizer, ManyStackObjectsTest) {
  char XXX[10];
  char YYY[20];
  char ZZZ[30];
  Ident(XXX);
  Ident(YYY);
  EXPECT_DEATH(Ident(ZZZ)[-1] = 0, ASAN_PCRE_DOTALL "XXX.*YYY.*ZZZ");
}
#endif

#if 0  // This test requires online symbolizer.
// Moved to lit_tests/stack-oob-frames.cpp.
// Reenable here once we have online symbolizer by default.
NOINLINE static void Frame0(int frame, char *a, char *b, char *c) {
  char d[4] = {0};
  char *D = Ident(d);
  switch (frame) {
    case 3: a[5]++; break;
    case 2: b[5]++; break;
    case 1: c[5]++; break;
    case 0: D[5]++; break;
  }
}
NOINLINE static void Frame1(int frame, char *a, char *b) {
  char c[4] = {0}; Frame0(frame, a, b, c);
  break_optimization(0);
}
NOINLINE static void Frame2(int frame, char *a) {
  char b[4] = {0}; Frame1(frame, a, b);
  break_optimization(0);
}
NOINLINE static void Frame3(int frame) {
  char a[4] = {0}; Frame2(frame, a);
  break_optimization(0);
}

TEST(AddressSanitizer, GuiltyStackFrame0Test) {
  EXPECT_DEATH(Frame3(0), "located .*in frame <.*Frame0");
}
TEST(AddressSanitizer, GuiltyStackFrame1Test) {
  EXPECT_DEATH(Frame3(1), "located .*in frame <.*Frame1");
}
TEST(AddressSanitizer, GuiltyStackFrame2Test) {
  EXPECT_DEATH(Frame3(2), "located .*in frame <.*Frame2");
}
TEST(AddressSanitizer, GuiltyStackFrame3Test) {
  EXPECT_DEATH(Frame3(3), "located .*in frame <.*Frame3");
}
#endif

NOINLINE void LongJmpFunc1(jmp_buf buf) {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  longjmp(buf, 1);
}

NOINLINE void TouchStackFunc() {
  int a[100];  // long array will intersect with redzones from LongJmpFunc1.
  int *A = Ident(a);
  for (int i = 0; i < 100; i++)
    A[i] = i*i;
}

// Test that we handle longjmp and do not report false positives on stack.
TEST(AddressSanitizer, LongJmpTest) {
  static jmp_buf buf;
  if (!setjmp(buf)) {
    LongJmpFunc1(buf);
  } else {
    TouchStackFunc();
  }
}

#if !defined(_WIN32)  // Only basic longjmp is available on Windows.
NOINLINE void UnderscopeLongJmpFunc1(jmp_buf buf) {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  _longjmp(buf, 1);
}

NOINLINE void SigLongJmpFunc1(sigjmp_buf buf) {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  siglongjmp(buf, 1);
}

#if !defined(__ANDROID__) && !defined(__arm__) && !defined(__aarch64__) && \
    !defined(__mips__) && !defined(__mips64) && !defined(__s390__) &&      \
    !defined(__riscv) && !defined(__loongarch__)
NOINLINE void BuiltinLongJmpFunc1(jmp_buf buf) {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  __builtin_longjmp((void**)buf, 1);
}

// Does not work on ARM:
// https://github.com/google/sanitizers/issues/185
TEST(AddressSanitizer, BuiltinLongJmpTest) {
  static jmp_buf buf;
  if (!__builtin_setjmp((void**)buf)) {
    BuiltinLongJmpFunc1(buf);
  } else {
    TouchStackFunc();
  }
}
#endif  // !defined(__ANDROID__) && !defined(__arm__) &&
        // !defined(__aarch64__) && !defined(__mips__) &&
        // !defined(__mips64) && !defined(__s390__) &&
        // !defined(__riscv) && !defined(__loongarch__)

TEST(AddressSanitizer, UnderscopeLongJmpTest) {
  static jmp_buf buf;
  if (!_setjmp(buf)) {
    UnderscopeLongJmpFunc1(buf);
  } else {
    TouchStackFunc();
  }
}

TEST(AddressSanitizer, SigLongJmpTest) {
  static sigjmp_buf buf;
  if (!sigsetjmp(buf, 1)) {
    SigLongJmpFunc1(buf);
  } else {
    TouchStackFunc();
  }
}
#endif

// FIXME: Why does clang-cl define __EXCEPTIONS?
#if defined(__EXCEPTIONS) && !defined(_WIN32)
NOINLINE void ThrowFunc() {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  ASAN_THROW(1);
}

TEST(AddressSanitizer, CxxExceptionTest) {
  if (ASAN_UAR) return;
  // TODO(kcc): this test crashes on 32-bit for some reason...
  if (SANITIZER_WORDSIZE == 32) return;
  try {
    ThrowFunc();
  } catch(...) {}
  TouchStackFunc();
}
#endif

void *ThreadStackReuseFunc1(void *unused) {
  // create three red zones for these two stack objects.
  int a;
  int b;

  int *A = Ident(&a);
  int *B = Ident(&b);
  *A = *B;
  pthread_exit(0);
  return 0;
}

void *ThreadStackReuseFunc2(void *unused) {
  TouchStackFunc();
  return 0;
}

#if !defined(__thumb__)
TEST(AddressSanitizer, ThreadStackReuseTest) {
  pthread_t t;
  PTHREAD_CREATE(&t, 0, ThreadStackReuseFunc1, 0);
  PTHREAD_JOIN(t, 0);
  PTHREAD_CREATE(&t, 0, ThreadStackReuseFunc2, 0);
  PTHREAD_JOIN(t, 0);
}
#endif

#if defined(__SSE2__)
#include <emmintrin.h>
TEST(AddressSanitizer, Store128Test) {
  char *a = Ident((char*)malloc(Ident(12)));
  char *p = a;
  if (((uintptr_t)a % 16) != 0)
    p = a + 8;
  assert(((uintptr_t)p % 16) == 0);
  __m128i value_wide = _mm_set1_epi16(0x1234);
  EXPECT_DEATH(_mm_store_si128((__m128i*)p, value_wide),
               "AddressSanitizer: heap-buffer-overflow");
  EXPECT_DEATH(_mm_store_si128((__m128i*)p, value_wide),
               "WRITE of size 16");
  EXPECT_DEATH(_mm_store_si128((__m128i*)p, value_wide),
               "located 0 bytes after 12-byte");
  free(a);
}
#endif

// FIXME: All tests that use this function should be turned into lit tests.
std::string RightOOBErrorMessage(int oob_distance, bool is_write) {
  assert(oob_distance >= 0);
  char expected_str[100];
  sprintf(expected_str, ASAN_PCRE_DOTALL
#if !GTEST_USES_SIMPLE_RE
          "buffer-overflow.*%s.*"
#endif
          "located %d bytes after",
#if !GTEST_USES_SIMPLE_RE
          is_write ? "WRITE" : "READ",
#endif
          oob_distance);
  return std::string(expected_str);
}

std::string RightOOBWriteMessage(int oob_distance) {
  return RightOOBErrorMessage(oob_distance, /*is_write*/true);
}

std::string RightOOBReadMessage(int oob_distance) {
  return RightOOBErrorMessage(oob_distance, /*is_write*/false);
}

// FIXME: All tests that use this function should be turned into lit tests.
std::string LeftOOBErrorMessage(int oob_distance, bool is_write) {
  assert(oob_distance > 0);
  char expected_str[100];
  sprintf(expected_str,
#if !GTEST_USES_SIMPLE_RE
          ASAN_PCRE_DOTALL "%s.*"
#endif
          "located %d bytes before",
#if !GTEST_USES_SIMPLE_RE
          is_write ? "WRITE" : "READ",
#endif
          oob_distance);
  return std::string(expected_str);
}

std::string LeftOOBWriteMessage(int oob_distance) {
  return LeftOOBErrorMessage(oob_distance, /*is_write*/true);
}

std::string LeftOOBReadMessage(int oob_distance) {
  return LeftOOBErrorMessage(oob_distance, /*is_write*/false);
}

std::string LeftOOBAccessMessage(int oob_distance) {
  assert(oob_distance > 0);
  char expected_str[100];
  sprintf(expected_str, "located %d bytes before", oob_distance);
  return std::string(expected_str);
}

char* MallocAndMemsetString(size_t size, char ch) {
  char *s = Ident((char*)malloc(size));
  memset(s, ch, size);
  return s;
}

char* MallocAndMemsetString(size_t size) {
  return MallocAndMemsetString(size, 'z');
}

#if SANITIZER_GLIBC
#define READ_TEST(READ_N_BYTES)                                          \
  char *x = new char[10];                                                \
  int fd = open("/proc/self/stat", O_RDONLY);                            \
  ASSERT_GT(fd, 0);                                                      \
  EXPECT_DEATH(READ_N_BYTES,                                             \
               ASAN_PCRE_DOTALL                                          \
               "AddressSanitizer: heap-buffer-overflow"                  \
               ".* is located 0 bytes after 10-byte region");  \
  close(fd);                                                             \
  delete [] x;                                                           \

TEST(AddressSanitizer, pread) {
  READ_TEST(pread(fd, x, 15, 0));
}

TEST(AddressSanitizer, pread64) {
  READ_TEST(pread64(fd, x, 15, 0));
}

TEST(AddressSanitizer, read) {
  READ_TEST(read(fd, x, 15));
}
#endif  // SANITIZER_GLIBC

// This test case fails
// Clang optimizes memcpy/memset calls which lead to unaligned access
TEST(AddressSanitizer, DISABLED_MemIntrinsicUnalignedAccessTest) {
  int size = Ident(4096);
  char *s = Ident((char*)malloc(size));
  EXPECT_DEATH(memset(s + size - 1, 0, 2), RightOOBWriteMessage(0));
  free(s);
}

NOINLINE static int LargeFunction(bool do_bad_access) {
  int *x = new int[100];
  x[0]++;
  x[1]++;
  x[2]++;
  x[3]++;
  x[4]++;
  x[5]++;
  x[6]++;
  x[7]++;
  x[8]++;
  x[9]++;

  x[do_bad_access ? 100 : 0]++; int res = __LINE__;

  x[10]++;
  x[11]++;
  x[12]++;
  x[13]++;
  x[14]++;
  x[15]++;
  x[16]++;
  x[17]++;
  x[18]++;
  x[19]++;

  delete[] x;
  return res;
}

// Test the we have correct debug info for the failing instruction.
// This test requires the in-process symbolizer to be enabled by default.
TEST(AddressSanitizer, DISABLED_LargeFunctionSymbolizeTest) {
  int failing_line = LargeFunction(false);
  char expected_warning[128];
  sprintf(expected_warning, "LargeFunction.*asan_test.*:%d", failing_line);
  EXPECT_DEATH(LargeFunction(true), expected_warning);
}

// Check that we unwind and symbolize correctly.
TEST(AddressSanitizer, DISABLED_MallocFreeUnwindAndSymbolizeTest) {
  int *a = (int*)malloc_aaa(sizeof(int));
  *a = 1;
  free_aaa(a);
  EXPECT_DEATH(*a = 1, "free_ccc.*free_bbb.*free_aaa.*"
               "malloc_fff.*malloc_eee.*malloc_ddd");
}

static bool TryToSetThreadName(const char *name) {
#if defined(__linux__) && defined(PR_SET_NAME)
  return 0 == prctl(PR_SET_NAME, (unsigned long)name, 0, 0, 0);
#else
  return false;
#endif
}

void *ThreadedTestAlloc(void *a) {
  EXPECT_EQ(true, TryToSetThreadName("AllocThr"));
  int **p = (int**)a;
  *p = new int;
  return 0;
}

void *ThreadedTestFree(void *a) {
  EXPECT_EQ(true, TryToSetThreadName("FreeThr"));
  int **p = (int**)a;
  delete *p;
  return 0;
}

void *ThreadedTestUse(void *a) {
  EXPECT_EQ(true, TryToSetThreadName("UseThr"));
  int **p = (int**)a;
  **p = 1;
  return 0;
}

void ThreadedTestSpawn() {
  pthread_t t;
  int *x;
  PTHREAD_CREATE(&t, 0, ThreadedTestAlloc, &x);
  PTHREAD_JOIN(t, 0);
  PTHREAD_CREATE(&t, 0, ThreadedTestFree, &x);
  PTHREAD_JOIN(t, 0);
  PTHREAD_CREATE(&t, 0, ThreadedTestUse, &x);
  PTHREAD_JOIN(t, 0);
}

#if !defined(_WIN32)  // FIXME: This should be a lit test.
TEST(AddressSanitizer, ThreadedTest) {
  EXPECT_DEATH(ThreadedTestSpawn(),
               ASAN_PCRE_DOTALL
               "Thread T.*created"
               ".*Thread T.*created"
               ".*Thread T.*created");
}
#endif

void *ThreadedTestFunc(void *unused) {
  // Check if prctl(PR_SET_NAME) is supported. Return if not.
  if (!TryToSetThreadName("TestFunc"))
    return 0;
  EXPECT_DEATH(ThreadedTestSpawn(),
               ASAN_PCRE_DOTALL
               "WRITE .*thread T. .UseThr."
               ".*freed by thread T. .FreeThr. here:"
               ".*previously allocated by thread T. .AllocThr. here:"
               ".*Thread T. .UseThr. created by T.*TestFunc"
               ".*Thread T. .FreeThr. created by T"
               ".*Thread T. .AllocThr. created by T"
               "");
  return 0;
}

TEST(AddressSanitizer, ThreadNamesTest) {
  // Run ThreadedTestFunc in a separate thread because it tries to set a
  // thread name and we don't want to change the main thread's name.
  pthread_t t;
  PTHREAD_CREATE(&t, 0, ThreadedTestFunc, 0);
  PTHREAD_JOIN(t, 0);
}

#if ASAN_NEEDS_SEGV
TEST(AddressSanitizer, ShadowGapTest) {
#if SANITIZER_WORDSIZE == 32
  char *addr = (char*)0x23000000;
#else
# if defined(__powerpc64__)
  char *addr = (char*)0x024000800000;
# elif defined(__s390x__)
  char *addr = (char*)0x11000000000000;
# else
  char *addr = (char*)0x0000100000080000;
# endif
#endif
  EXPECT_DEATH(*addr = 1, "AddressSanitizer: (SEGV|BUS) on unknown");
}
#endif  // ASAN_NEEDS_SEGV

extern "C" {
NOINLINE static void UseThenFreeThenUse() {
  char *x = Ident((char*)malloc(8));
  *x = 1;
  free_aaa(x);
  *x = 2;
}
}

TEST(AddressSanitizer, UseThenFreeThenUseTest) {
  EXPECT_DEATH(UseThenFreeThenUse(), "freed by thread");
}

TEST(AddressSanitizer, StrDupTest) {
  free(strdup(Ident("123")));
}

// Currently we create and poison redzone at right of global variables.
static char static110[110];
const char ConstGlob[7] = {1, 2, 3, 4, 5, 6, 7};
static const char StaticConstGlob[3] = {9, 8, 7};

TEST(AddressSanitizer, GlobalTest) {
  static char func_static15[15];

  static char fs1[10];
  static char fs2[10];
  static char fs3[10];

  glob5[Ident(0)] = 0;
  glob5[Ident(1)] = 0;
  glob5[Ident(2)] = 0;
  glob5[Ident(3)] = 0;
  glob5[Ident(4)] = 0;

  EXPECT_DEATH(glob5[Ident(5)] = 0,
               "0 bytes after global variable.*glob5.* size 5");
  EXPECT_DEATH(glob5[Ident(5+6)] = 0,
               "6 bytes after global variable.*glob5.* size 5");
  Ident(static110);  // avoid optimizations
  static110[Ident(0)] = 0;
  static110[Ident(109)] = 0;
  EXPECT_DEATH(static110[Ident(110)] = 0,
               "0 bytes after global variable");
  EXPECT_DEATH(static110[Ident(110+7)] = 0,
               "7 bytes after global variable");

  Ident(func_static15);  // avoid optimizations
  func_static15[Ident(0)] = 0;
  EXPECT_DEATH(func_static15[Ident(15)] = 0,
               "0 bytes after global variable");
  EXPECT_DEATH(func_static15[Ident(15 + 9)] = 0,
               "9 bytes after global variable");

  Ident(fs1);
  Ident(fs2);
  Ident(fs3);

  // We don't create left redzones, so this is not 100% guaranteed to fail.
  // But most likely will.
  EXPECT_DEATH(fs2[Ident(-1)] = 0, "is located.* global variable");

  EXPECT_DEATH(Ident(Ident(ConstGlob)[8]),
               "is located 1 bytes after .*ConstGlob");
  EXPECT_DEATH(Ident(Ident(StaticConstGlob)[5]),
               "is located 2 bytes after .*StaticConstGlob");

  // call stuff from another file.
  GlobalsTest(0);
}

TEST(AddressSanitizer, GlobalStringConstTest) {
  static const char *zoo = "FOOBAR123";
  const char *p = Ident(zoo);
  EXPECT_DEATH(Ident(p[15]), "is ascii string 'FOOBAR123'");
}

TEST(AddressSanitizer, FileNameInGlobalReportTest) {
  static char zoo[10];
  const char *p = Ident(zoo);
  // The file name should be present in the report.
  EXPECT_DEATH(Ident(p[15]), "zoo.*asan_test.");
}

int *ReturnsPointerToALocalObject() {
  int a = 0;
  return Ident(&a);
}

#if ASAN_UAR == 1
TEST(AddressSanitizer, LocalReferenceReturnTest) {
  int *(*f)() = Ident(ReturnsPointerToALocalObject);
  int *p = f();
  // Call 'f' a few more times, 'p' should still be poisoned.
  for (int i = 0; i < 32; i++)
    f();
  EXPECT_DEATH(*p = 1, "AddressSanitizer: stack-use-after-return");
  EXPECT_DEATH(*p = 1, "is located.*in frame .*ReturnsPointerToALocal");
}
#endif

template <int kSize>
NOINLINE static void FuncWithStack() {
  char x[kSize];
  Ident(x)[0] = 0;
  Ident(x)[kSize-1] = 0;
}

static void LotsOfStackReuse() {
  int LargeStack[10000];
  Ident(LargeStack)[0] = 0;
  for (int i = 0; i < 10000; i++) {
    FuncWithStack<128 * 1>();
    FuncWithStack<128 * 2>();
    FuncWithStack<128 * 4>();
    FuncWithStack<128 * 8>();
    FuncWithStack<128 * 16>();
    FuncWithStack<128 * 32>();
    FuncWithStack<128 * 64>();
    FuncWithStack<128 * 128>();
    FuncWithStack<128 * 256>();
    FuncWithStack<128 * 512>();
    Ident(LargeStack)[0] = 0;
  }
}

TEST(AddressSanitizer, StressStackReuseTest) {
  LotsOfStackReuse();
}

TEST(AddressSanitizer, ThreadedStressStackReuseTest) {
  const int kNumThreads = 20;
  pthread_t t[kNumThreads];
  for (int i = 0; i < kNumThreads; i++) {
    PTHREAD_CREATE(&t[i], 0, (void* (*)(void *x))LotsOfStackReuse, 0);
  }
  for (int i = 0; i < kNumThreads; i++) {
    PTHREAD_JOIN(t[i], 0);
  }
}

// pthread_exit tries to perform unwinding stuff that leads to dlopen'ing
// libgcc_s.so. dlopen in its turn calls malloc to store "libgcc_s.so" string
// that confuses LSan on Thumb because it fails to understand that this
// allocation happens in dynamic linker and should be ignored.
#if !defined(__thumb__)
static void *PthreadExit(void *a) {
  pthread_exit(0);
  return 0;
}

TEST(AddressSanitizer, PthreadExitTest) {
  pthread_t t;
  for (int i = 0; i < 1000; i++) {
    PTHREAD_CREATE(&t, 0, PthreadExit, 0);
    PTHREAD_JOIN(t, 0);
  }
}
#endif

// FIXME: Why does clang-cl define __EXCEPTIONS?
#if defined(__EXCEPTIONS) && !defined(_WIN32)
NOINLINE static void StackReuseAndException() {
  int large_stack[1000];
  Ident(large_stack);
  ASAN_THROW(1);
}

// TODO(kcc): support exceptions with use-after-return.
TEST(AddressSanitizer, DISABLED_StressStackReuseAndExceptionsTest) {
  for (int i = 0; i < 10000; i++) {
    try {
    StackReuseAndException();
    } catch(...) {
    }
  }
}
#endif

#if !defined(_WIN32)
TEST(AddressSanitizer, MlockTest) {
#if !defined(__ANDROID__) || __ANDROID_API__ >= 17
  EXPECT_EQ(0, mlockall(MCL_CURRENT));
#endif
  EXPECT_EQ(0, mlock((void*)0x12345, 0x5678));
#if !defined(__ANDROID__) || __ANDROID_API__ >= 17
  EXPECT_EQ(0, munlockall());
#endif
  EXPECT_EQ(0, munlock((void*)0x987, 0x654));
}
#endif

struct LargeStruct {
  int foo[100];
};

// Test for bug http://llvm.org/bugs/show_bug.cgi?id=11763.
// Struct copy should not cause asan warning even if lhs == rhs.
TEST(AddressSanitizer, LargeStructCopyTest) {
  LargeStruct a;
  *Ident(&a) = *Ident(&a);
}

ATTRIBUTE_NO_SANITIZE_ADDRESS
static void NoSanitizeAddress() {
  char *foo = new char[10];
  Ident(foo)[10] = 0;
  delete [] foo;
}

TEST(AddressSanitizer, AttributeNoSanitizeAddressTest) {
  Ident(NoSanitizeAddress)();
}

// The new/delete/etc mismatch checks don't work on Android,
//   as calls to new/delete go through malloc/free.
// OS X support is tracked here:
//   https://github.com/google/sanitizers/issues/131
// Windows support is tracked here:
//   https://github.com/google/sanitizers/issues/309
#if !defined(__ANDROID__) && \
    !defined(__APPLE__) && \
    !defined(_WIN32)
static std::string MismatchStr(const std::string &str) {
  return std::string("AddressSanitizer: alloc-dealloc-mismatch \\(") + str;
}

static std::string MismatchOrNewDeleteTypeStr(const std::string &mismatch_str) {
  return "(" + MismatchStr(mismatch_str) +
         ")|(AddressSanitizer: new-delete-type-mismatch)";
}

TEST(AddressSanitizer, AllocDeallocMismatch) {
  EXPECT_DEATH(free(Ident(new int)),
               MismatchStr("operator new vs free"));
  EXPECT_DEATH(free(Ident(new int[2])),
               MismatchStr("operator new \\[\\] vs free"));
  EXPECT_DEATH(
      delete (Ident(new int[2])),
      MismatchOrNewDeleteTypeStr("operator new \\[\\] vs operator delete"));
  EXPECT_DEATH(delete (Ident((int *)malloc(2 * sizeof(int)))),
               MismatchOrNewDeleteTypeStr("malloc vs operator delete"));
  EXPECT_DEATH(delete [] (Ident(new int)),
               MismatchStr("operator new vs operator delete \\[\\]"));
  EXPECT_DEATH(delete [] (Ident((int*)malloc(2 * sizeof(int)))),
               MismatchStr("malloc vs operator delete \\[\\]"));
}
#endif

// ------------------ demo tests; run each one-by-one -------------
// e.g. --gtest_filter=*DemoOOBLeftHigh --gtest_also_run_disabled_tests
TEST(AddressSanitizer, DISABLED_DemoThreadedTest) {
  ThreadedTestSpawn();
}

void *SimpleBugOnSTack(void *x = 0) {
  char a[20];
  Ident(a)[20] = 0;
  return 0;
}

TEST(AddressSanitizer, DISABLED_DemoStackTest) {
  SimpleBugOnSTack();
}

TEST(AddressSanitizer, DISABLED_DemoThreadStackTest) {
  pthread_t t;
  PTHREAD_CREATE(&t, 0, SimpleBugOnSTack, 0);
  PTHREAD_JOIN(t, 0);
}

TEST(AddressSanitizer, DISABLED_DemoUAFLowIn) {
  uaf_test<U1>(10, 0);
}
TEST(AddressSanitizer, DISABLED_DemoUAFLowLeft) {
  uaf_test<U1>(10, -2);
}
TEST(AddressSanitizer, DISABLED_DemoUAFLowRight) {
  uaf_test<U1>(10, 10);
}

TEST(AddressSanitizer, DISABLED_DemoUAFHigh) {
  uaf_test<U1>(kLargeMalloc, 0);
}

TEST(AddressSanitizer, DISABLED_DemoOOM) {
  size_t size = SANITIZER_WORDSIZE == 64 ? (size_t)(1ULL << 40) : (0xf0000000);
  printf("%p\n", malloc(size));
}

TEST(AddressSanitizer, DISABLED_DemoDoubleFreeTest) {
  DoubleFree();
}

TEST(AddressSanitizer, DISABLED_DemoNullDerefTest) {
  int *a = 0;
  Ident(a)[10] = 0;
}

TEST(AddressSanitizer, DISABLED_DemoFunctionStaticTest) {
  static char a[100];
  static char b[100];
  static char c[100];
  Ident(a);
  Ident(b);
  Ident(c);
  Ident(a)[5] = 0;
  Ident(b)[105] = 0;
  Ident(a)[5] = 0;
}

TEST(AddressSanitizer, DISABLED_DemoTooMuchMemoryTest) {
  const size_t kAllocSize = (1 << 28) - 1024;
  size_t total_size = 0;
  while (true) {
    void *x = malloc(kAllocSize);
    memset(x, 0, kAllocSize);
    total_size += kAllocSize;
    fprintf(stderr, "total: %ldM %p\n", (long)total_size >> 20, x);
  }
}

#if !defined(__NetBSD__) && !defined(__i386__)
// https://github.com/google/sanitizers/issues/66
TEST(AddressSanitizer, BufferOverflowAfterManyFrees) {
  for (int i = 0; i < 1000000; i++) {
    delete [] (Ident(new char [8644]));
  }
  char *x = new char[8192];
  EXPECT_DEATH(x[Ident(8192)] = 0, "AddressSanitizer: heap-buffer-overflow");
  delete [] Ident(x);
}
#endif


// Test that instrumentation of stack allocations takes into account
// AllocSize of a type, and not its StoreSize (16 vs 10 bytes for long double).
// See http://llvm.org/bugs/show_bug.cgi?id=12047 for more details.
TEST(AddressSanitizer, LongDoubleNegativeTest) {
  long double a, b;
  static long double c;
  memcpy(Ident(&a), Ident(&b), sizeof(long double));
  memcpy(Ident(&c), Ident(&b), sizeof(long double));
}

#if !defined(_WIN32)
TEST(AddressSanitizer, pthread_getschedparam) {
  int policy;
  struct sched_param param;
  EXPECT_DEATH(
      pthread_getschedparam(pthread_self(), &policy, Ident(&param) + 2),
      "AddressSanitizer: stack-buffer-.*flow");
  EXPECT_DEATH(
      pthread_getschedparam(pthread_self(), Ident(&policy) - 1, &param),
      "AddressSanitizer: stack-buffer-.*flow");
  int res = pthread_getschedparam(pthread_self(), &policy, &param);
  ASSERT_EQ(0, res);
}
#endif

#if SANITIZER_TEST_HAS_PRINTF_L
static int vsnprintf_l_wrapper(char *s, size_t n,
                               locale_t l, const char *format, ...) {
  va_list va;
  va_start(va, format);
  int res = vsnprintf_l(s, n , l, format, va);
  va_end(va);
  return res;
}

TEST(AddressSanitizer, snprintf_l) {
  char buff[5];
  // Check that snprintf_l() works fine with Asan.
  int res = snprintf_l(buff, 5, SANITIZER_GET_C_LOCALE, "%s", "snprintf_l()");
  EXPECT_EQ(12, res);
  // Check that vsnprintf_l() works fine with Asan.
  res = vsnprintf_l_wrapper(buff, 5, SANITIZER_GET_C_LOCALE, "%s",
                            "vsnprintf_l()");
  EXPECT_EQ(13, res);

  EXPECT_DEATH(
      snprintf_l(buff, 10, SANITIZER_GET_C_LOCALE, "%s", "snprintf_l()"),
      "AddressSanitizer: stack-buffer-overflow");
  EXPECT_DEATH(vsnprintf_l_wrapper(buff, 10, SANITIZER_GET_C_LOCALE, "%s",
                                   "vsnprintf_l()"),
               "AddressSanitizer: stack-buffer-overflow");
}
#endif
