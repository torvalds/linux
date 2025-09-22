//===-- msan_interceptors.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// Interceptors for standard library functions.
//
// FIXME: move as many interceptors as possible into
// sanitizer_common/sanitizer_common_interceptors.h
//===----------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "interception/interception.h"
#include "msan.h"
#include "msan_chained_origin_depot.h"
#include "msan_dl.h"
#include "msan_origin.h"
#include "msan_poisoning.h"
#include "msan_report.h"
#include "msan_thread.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_interface.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_errno_codes.h"
#include "sanitizer_common/sanitizer_glibc_version.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "sanitizer_common/sanitizer_vector.h"

#if SANITIZER_NETBSD
#define fstat __fstat50
#define gettimeofday __gettimeofday50
#define getrusage __getrusage50
#define tzset __tzset50
#endif

#include <stdarg.h>
// ACHTUNG! No other system header includes in this file.
// Ideally, we should get rid of stdarg.h as well.

using namespace __msan;

using __sanitizer::memory_order;
using __sanitizer::atomic_load;
using __sanitizer::atomic_store;
using __sanitizer::atomic_uintptr_t;

DECLARE_REAL(SIZE_T, strlen, const char *s)
DECLARE_REAL(SIZE_T, strnlen, const char *s, SIZE_T maxlen)
DECLARE_REAL(void *, memcpy, void *dest, const void *src, uptr n)
DECLARE_REAL(void *, memset, void *dest, int c, uptr n)

// True if this is a nested interceptor.
static THREADLOCAL int in_interceptor_scope;

void __msan_scoped_disable_interceptor_checks() { ++in_interceptor_scope; }
void __msan_scoped_enable_interceptor_checks() { --in_interceptor_scope; }

struct InterceptorScope {
  InterceptorScope() { ++in_interceptor_scope; }
  ~InterceptorScope() { --in_interceptor_scope; }
};

bool IsInInterceptorScope() {
  return in_interceptor_scope;
}

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !msan_inited; }
};

#define ENSURE_MSAN_INITED() do { \
  CHECK(!msan_init_is_running); \
  if (!msan_inited) { \
    __msan_init(); \
  } \
} while (0)

// Check that [x, x+n) range is unpoisoned.
#define CHECK_UNPOISONED_0(x, n)                                  \
  do {                                                            \
    sptr __offset = __msan_test_shadow(x, n);                     \
    if (__msan::IsInSymbolizerOrUnwider())                        \
      break;                                                      \
    if (__offset >= 0 && __msan::flags()->report_umrs) {          \
      GET_CALLER_PC_BP;                                           \
      ReportUMRInsideAddressRange(__func__, x, n, __offset);      \
      __msan::PrintWarningWithOrigin(                             \
          pc, bp, __msan_get_origin((const char *)x + __offset)); \
      if (__msan::flags()->halt_on_error) {                       \
        Printf("Exiting\n");                                      \
        Die();                                                    \
      }                                                           \
    }                                                             \
  } while (0)

// Check that [x, x+n) range is unpoisoned unless we are in a nested
// interceptor.
#define CHECK_UNPOISONED(x, n)                             \
  do {                                                     \
    if (!IsInInterceptorScope()) CHECK_UNPOISONED_0(x, n); \
  } while (0)

#define CHECK_UNPOISONED_STRING_OF_LEN(x, len, n)               \
  CHECK_UNPOISONED((x),                                         \
    common_flags()->strict_string_checks ? (len) + 1 : (n) )

#define CHECK_UNPOISONED_STRING(x, n)                           \
    CHECK_UNPOISONED_STRING_OF_LEN((x), internal_strlen(x), (n))

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(SIZE_T, fread_unlocked, void *ptr, SIZE_T size, SIZE_T nmemb,
            void *file) {
  ENSURE_MSAN_INITED();
  SIZE_T res = REAL(fread_unlocked)(ptr, size, nmemb, file);
  if (res > 0)
    __msan_unpoison(ptr, res *size);
  return res;
}
#define MSAN_MAYBE_INTERCEPT_FREAD_UNLOCKED INTERCEPT_FUNCTION(fread_unlocked)
#else
#define MSAN_MAYBE_INTERCEPT_FREAD_UNLOCKED
#endif

#if !SANITIZER_NETBSD
INTERCEPTOR(void *, mempcpy, void *dest, const void *src, SIZE_T n) {
  return (char *)__msan_memcpy(dest, src, n) + n;
}
#define MSAN_MAYBE_INTERCEPT_MEMPCPY INTERCEPT_FUNCTION(mempcpy)
#else
#define MSAN_MAYBE_INTERCEPT_MEMPCPY
#endif

INTERCEPTOR(void *, memccpy, void *dest, const void *src, int c, SIZE_T n) {
  ENSURE_MSAN_INITED();
  void *res = REAL(memccpy)(dest, src, c, n);
  CHECK(!res || (res >= dest && res <= (char *)dest + n));
  SIZE_T sz = res ? (char *)res - (char *)dest : n;
  CHECK_UNPOISONED(src, sz);
  __msan_unpoison(dest, sz);
  return res;
}

INTERCEPTOR(void *, bcopy, const void *src, void *dest, SIZE_T n) {
  return __msan_memmove(dest, src, n);
}

INTERCEPTOR(int, posix_memalign, void **memptr, SIZE_T alignment, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  CHECK_NE(memptr, 0);
  int res = msan_posix_memalign(memptr, alignment, size, &stack);
  if (!res)
    __msan_unpoison(memptr, sizeof(*memptr));
  return res;
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(void *, memalign, SIZE_T alignment, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  return msan_memalign(alignment, size, &stack);
}
#define MSAN_MAYBE_INTERCEPT_MEMALIGN INTERCEPT_FUNCTION(memalign)
#else
#define MSAN_MAYBE_INTERCEPT_MEMALIGN
#endif

INTERCEPTOR(void *, aligned_alloc, SIZE_T alignment, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  return msan_aligned_alloc(alignment, size, &stack);
}

#if !SANITIZER_NETBSD
INTERCEPTOR(void *, __libc_memalign, SIZE_T alignment, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  void *ptr = msan_memalign(alignment, size, &stack);
  if (ptr)
    DTLS_on_libc_memalign(ptr, size);
  return ptr;
}
#define MSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN INTERCEPT_FUNCTION(__libc_memalign)
#else
#define MSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN
#endif

INTERCEPTOR(void *, valloc, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  return msan_valloc(size, &stack);
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(void *, pvalloc, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  return msan_pvalloc(size, &stack);
}
#define MSAN_MAYBE_INTERCEPT_PVALLOC INTERCEPT_FUNCTION(pvalloc)
#else
#define MSAN_MAYBE_INTERCEPT_PVALLOC
#endif

INTERCEPTOR(void, free, void *ptr) {
  if (UNLIKELY(!ptr))
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_MALLOC_STACK_TRACE;
  MsanDeallocate(&stack, ptr);
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(void, cfree, void *ptr) {
  if (UNLIKELY(!ptr))
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  GET_MALLOC_STACK_TRACE;
  MsanDeallocate(&stack, ptr);
}
#  define MSAN_MAYBE_INTERCEPT_CFREE INTERCEPT_FUNCTION(cfree)
#else
#define MSAN_MAYBE_INTERCEPT_CFREE
#endif

#if !SANITIZER_NETBSD
INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  return __sanitizer_get_allocated_size(ptr);
}
#define MSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE \
  INTERCEPT_FUNCTION(malloc_usable_size)
#else
#define MSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE
#endif

#if (!SANITIZER_FREEBSD && !SANITIZER_NETBSD) || __GLIBC_PREREQ(2, 33)
template <class T>
static NOINLINE void clear_mallinfo(T *sret) {
  ENSURE_MSAN_INITED();
  internal_memset(sret, 0, sizeof(*sret));
  __msan_unpoison(sret, sizeof(*sret));
}
#endif

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
// Interceptors use NRVO and assume that sret will be pre-allocated in
// caller frame.
INTERCEPTOR(__sanitizer_struct_mallinfo, mallinfo,) {
  __sanitizer_struct_mallinfo sret;
  clear_mallinfo(&sret);
  return sret;
}
#  define MSAN_MAYBE_INTERCEPT_MALLINFO INTERCEPT_FUNCTION(mallinfo)
#else
#  define MSAN_MAYBE_INTERCEPT_MALLINFO
#endif

#if __GLIBC_PREREQ(2, 33)
INTERCEPTOR(__sanitizer_struct_mallinfo2, mallinfo2) {
  __sanitizer_struct_mallinfo2 sret;
  clear_mallinfo(&sret);
  return sret;
}
#  define MSAN_MAYBE_INTERCEPT_MALLINFO2 INTERCEPT_FUNCTION(mallinfo2)
#else
#  define MSAN_MAYBE_INTERCEPT_MALLINFO2
#endif

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(int, mallopt, int cmd, int value) {
  return 0;
}
#define MSAN_MAYBE_INTERCEPT_MALLOPT INTERCEPT_FUNCTION(mallopt)
#else
#define MSAN_MAYBE_INTERCEPT_MALLOPT
#endif

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(void, malloc_stats, void) {
  // FIXME: implement, but don't call REAL(malloc_stats)!
}
#define MSAN_MAYBE_INTERCEPT_MALLOC_STATS INTERCEPT_FUNCTION(malloc_stats)
#else
#define MSAN_MAYBE_INTERCEPT_MALLOC_STATS
#endif

INTERCEPTOR(char *, strcpy, char *dest, const char *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T n = internal_strlen(src);
  CHECK_UNPOISONED_STRING(src + n, 0);
  char *res = REAL(strcpy)(dest, src);
  CopyShadowAndOrigin(dest, src, n + 1, &stack);
  return res;
}

INTERCEPTOR(char *, strncpy, char *dest, const char *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T copy_size = internal_strnlen(src, n);
  if (copy_size < n)
    copy_size++;  // trailing \0
  char *res = REAL(strncpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, copy_size, &stack);
  __msan_unpoison(dest + copy_size, n - copy_size);
  return res;
}

#if !SANITIZER_NETBSD
INTERCEPTOR(char *, stpcpy, char *dest, const char *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T n = internal_strlen(src);
  CHECK_UNPOISONED_STRING(src + n, 0);
  char *res = REAL(stpcpy)(dest, src);
  CopyShadowAndOrigin(dest, src, n + 1, &stack);
  return res;
}

INTERCEPTOR(char *, stpncpy, char *dest, const char *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T copy_size = Min(n, internal_strnlen(src, n) + 1);
  char *res = REAL(stpncpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, copy_size, &stack);
  __msan_unpoison(dest + copy_size, n - copy_size);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_STPCPY INTERCEPT_FUNCTION(stpcpy)
#  define MSAN_MAYBE_INTERCEPT_STPNCPY INTERCEPT_FUNCTION(stpncpy)
#else
#define MSAN_MAYBE_INTERCEPT_STPCPY
#  define MSAN_MAYBE_INTERCEPT_STPNCPY
#endif

INTERCEPTOR(char *, strdup, char *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  // On FreeBSD strdup() leverages strlen().
  InterceptorScope interceptor_scope;
  SIZE_T n = internal_strlen(src);
  CHECK_UNPOISONED_STRING(src + n, 0);
  char *res = REAL(strdup)(src);
  CopyShadowAndOrigin(res, src, n + 1, &stack);
  return res;
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(char *, __strdup, char *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T n = internal_strlen(src);
  CHECK_UNPOISONED_STRING(src + n, 0);
  char *res = REAL(__strdup)(src);
  CopyShadowAndOrigin(res, src, n + 1, &stack);
  return res;
}
#define MSAN_MAYBE_INTERCEPT___STRDUP INTERCEPT_FUNCTION(__strdup)
#else
#define MSAN_MAYBE_INTERCEPT___STRDUP
#endif

#if !SANITIZER_NETBSD
INTERCEPTOR(char *, gcvt, double number, SIZE_T ndigit, char *buf) {
  ENSURE_MSAN_INITED();
  char *res = REAL(gcvt)(number, ndigit, buf);
  SIZE_T n = internal_strlen(buf);
  __msan_unpoison(buf, n + 1);
  return res;
}
#define MSAN_MAYBE_INTERCEPT_GCVT INTERCEPT_FUNCTION(gcvt)
#else
#define MSAN_MAYBE_INTERCEPT_GCVT
#endif

INTERCEPTOR(char *, strcat, char *dest, const char *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T src_size = internal_strlen(src);
  SIZE_T dest_size = internal_strlen(dest);
  CHECK_UNPOISONED_STRING(src + src_size, 0);
  CHECK_UNPOISONED_STRING(dest + dest_size, 0);
  char *res = REAL(strcat)(dest, src);
  CopyShadowAndOrigin(dest + dest_size, src, src_size + 1, &stack);
  return res;
}

INTERCEPTOR(char *, strncat, char *dest, const char *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T dest_size = internal_strlen(dest);
  SIZE_T copy_size = internal_strnlen(src, n);
  CHECK_UNPOISONED_STRING(dest + dest_size, 0);
  char *res = REAL(strncat)(dest, src, n);
  CopyShadowAndOrigin(dest + dest_size, src, copy_size, &stack);
  __msan_unpoison(dest + dest_size + copy_size, 1); // \0
  return res;
}

// Hack: always pass nptr and endptr as part of __VA_ARGS_ to avoid having to
// deal with empty __VA_ARGS__ in the case of INTERCEPTOR_STRTO.
#define INTERCEPTOR_STRTO_BODY(ret_type, func, ...) \
  ENSURE_MSAN_INITED();                             \
  ret_type res = REAL(func)(__VA_ARGS__);           \
  __msan_unpoison(endptr, sizeof(*endptr));         \
  return res;

// On s390x, long double return values are passed via implicit reference,
// which needs to be unpoisoned.  We make the implicit pointer explicit.
#define INTERCEPTOR_STRTO_SRET_BODY(func, sret, ...) \
  ENSURE_MSAN_INITED();                              \
  REAL(func)(sret, __VA_ARGS__);                     \
  __msan_unpoison(sret, sizeof(*sret));              \
  __msan_unpoison(endptr, sizeof(*endptr));

#define INTERCEPTOR_STRTO(ret_type, func, char_type)                       \
  INTERCEPTOR(ret_type, func, const char_type *nptr, char_type **endptr) { \
    INTERCEPTOR_STRTO_BODY(ret_type, func, nptr, endptr);                  \
  }

#define INTERCEPTOR_STRTO_SRET(ret_type, func, char_type)                \
  INTERCEPTOR(void, func, ret_type *sret, const char_type *nptr,         \
              char_type **endptr) {                                      \
    INTERCEPTOR_STRTO_SRET_BODY(func, sret, nptr, endptr);               \
  }

#define INTERCEPTOR_STRTO_BASE(ret_type, func, char_type)                \
  INTERCEPTOR(ret_type, func, const char_type *nptr, char_type **endptr, \
              int base) {                                                \
    INTERCEPTOR_STRTO_BODY(ret_type, func, nptr, endptr, base);          \
  }

#define INTERCEPTOR_STRTO_LOC(ret_type, func, char_type)                 \
  INTERCEPTOR(ret_type, func, const char_type *nptr, char_type **endptr, \
              void *loc) {                                               \
    INTERCEPTOR_STRTO_BODY(ret_type, func, nptr, endptr, loc);           \
  }

#define INTERCEPTOR_STRTO_SRET_LOC(ret_type, func, char_type)            \
  INTERCEPTOR(void, func, ret_type *sret, const char_type *nptr,         \
              char_type **endptr, void *loc) {                           \
    INTERCEPTOR_STRTO_SRET_BODY(func, sret, nptr, endptr, loc);          \
  }

#define INTERCEPTOR_STRTO_BASE_LOC(ret_type, func, char_type)            \
  INTERCEPTOR(ret_type, func, const char_type *nptr, char_type **endptr, \
              int base, void *loc) {                                     \
    INTERCEPTOR_STRTO_BODY(ret_type, func, nptr, endptr, base, loc);     \
  }

#if SANITIZER_NETBSD
#define INTERCEPTORS_STRTO(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_LOC(ret_type, func##_l, char_type)

#define INTERCEPTORS_STRTO_SRET(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO_SRET(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_SRET_LOC(ret_type, func##_l, char_type)

#define INTERCEPTORS_STRTO_BASE(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO_BASE(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_BASE_LOC(ret_type, func##_l, char_type)

#else
#define INTERCEPTORS_STRTO(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_LOC(ret_type, func##_l, char_type)     \
  INTERCEPTOR_STRTO_LOC(ret_type, __##func##_l, char_type) \
  INTERCEPTOR_STRTO_LOC(ret_type, __##func##_internal, char_type)

#define INTERCEPTORS_STRTO_SRET(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO_SRET(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_SRET_LOC(ret_type, func##_l, char_type)     \
  INTERCEPTOR_STRTO_SRET_LOC(ret_type, __##func##_l, char_type) \
  INTERCEPTOR_STRTO_SRET_LOC(ret_type, __##func##_internal, char_type)

#define INTERCEPTORS_STRTO_BASE(ret_type, func, char_type)      \
  INTERCEPTOR_STRTO_BASE(ret_type, func, char_type)             \
  INTERCEPTOR_STRTO_BASE_LOC(ret_type, func##_l, char_type)     \
  INTERCEPTOR_STRTO_BASE_LOC(ret_type, __##func##_l, char_type) \
  INTERCEPTOR_STRTO_BASE_LOC(ret_type, __##func##_internal, char_type)
#endif

INTERCEPTORS_STRTO(double, strtod, char)
INTERCEPTORS_STRTO(float, strtof, char)
#ifdef __s390x__
INTERCEPTORS_STRTO_SRET(long double, strtold, char)
#else
INTERCEPTORS_STRTO(long double, strtold, char)
#endif
INTERCEPTORS_STRTO_BASE(long, strtol, char)
INTERCEPTORS_STRTO_BASE(long long, strtoll, char)
INTERCEPTORS_STRTO_BASE(unsigned long, strtoul, char)
INTERCEPTORS_STRTO_BASE(unsigned long long, strtoull, char)
INTERCEPTORS_STRTO_BASE(u64, strtouq, char)

INTERCEPTORS_STRTO(double, wcstod, wchar_t)
INTERCEPTORS_STRTO(float, wcstof, wchar_t)
#ifdef __s390x__
INTERCEPTORS_STRTO_SRET(long double, wcstold, wchar_t)
#else
INTERCEPTORS_STRTO(long double, wcstold, wchar_t)
#endif
INTERCEPTORS_STRTO_BASE(long, wcstol, wchar_t)
INTERCEPTORS_STRTO_BASE(long long, wcstoll, wchar_t)
INTERCEPTORS_STRTO_BASE(unsigned long, wcstoul, wchar_t)
INTERCEPTORS_STRTO_BASE(unsigned long long, wcstoull, wchar_t)

#if SANITIZER_GLIBC
INTERCEPTORS_STRTO(double, __isoc23_strtod, char)
INTERCEPTORS_STRTO(float, __isoc23_strtof, char)
#ifdef __s390x__
INTERCEPTORS_STRTO_SRET(long double, __isoc23_strtold, char)
#else
INTERCEPTORS_STRTO(long double, __isoc23_strtold, char)
#endif
INTERCEPTORS_STRTO_BASE(long, __isoc23_strtol, char)
INTERCEPTORS_STRTO_BASE(long long, __isoc23_strtoll, char)
INTERCEPTORS_STRTO_BASE(unsigned long, __isoc23_strtoul, char)
INTERCEPTORS_STRTO_BASE(unsigned long long, __isoc23_strtoull, char)
INTERCEPTORS_STRTO_BASE(u64, __isoc23_strtouq, char)

INTERCEPTORS_STRTO(double, __isoc23_wcstod, wchar_t)
INTERCEPTORS_STRTO(float, __isoc23_wcstof, wchar_t)
#ifdef __s390x__
INTERCEPTORS_STRTO_SRET(long double, __isoc23_wcstold, wchar_t)
#else
INTERCEPTORS_STRTO(long double, __isoc23_wcstold, wchar_t)
#endif
INTERCEPTORS_STRTO_BASE(long, __isoc23_wcstol, wchar_t)
INTERCEPTORS_STRTO_BASE(long long, __isoc23_wcstoll, wchar_t)
INTERCEPTORS_STRTO_BASE(unsigned long, __isoc23_wcstoul, wchar_t)
INTERCEPTORS_STRTO_BASE(unsigned long long, __isoc23_wcstoull, wchar_t)
#endif

#if SANITIZER_NETBSD
#define INTERCEPT_STRTO(func) \
  INTERCEPT_FUNCTION(func); \
  INTERCEPT_FUNCTION(func##_l);
#else
#define INTERCEPT_STRTO(func) \
  INTERCEPT_FUNCTION(func); \
  INTERCEPT_FUNCTION(func##_l); \
  INTERCEPT_FUNCTION(__##func##_l); \
  INTERCEPT_FUNCTION(__##func##_internal);

#define INTERCEPT_STRTO_VER(func, ver) \
  INTERCEPT_FUNCTION_VER(func, ver); \
  INTERCEPT_FUNCTION_VER(func##_l, ver); \
  INTERCEPT_FUNCTION_VER(__##func##_l, ver); \
  INTERCEPT_FUNCTION_VER(__##func##_internal, ver);
#endif


// FIXME: support *wprintf in common format interceptors.
INTERCEPTOR(int, vswprintf, void *str, uptr size, void *format, va_list ap) {
  ENSURE_MSAN_INITED();
  int res = REAL(vswprintf)(str, size, format, ap);
  if (res >= 0) {
    __msan_unpoison(str, 4 * (res + 1));
  }
  return res;
}

INTERCEPTOR(int, swprintf, void *str, uptr size, void *format, ...) {
  ENSURE_MSAN_INITED();
  va_list ap;
  va_start(ap, format);
  int res = vswprintf(str, size, format, ap);
  va_end(ap);
  return res;
}

#define INTERCEPTOR_STRFTIME_BODY(char_type, ret_type, func, s, ...) \
  ENSURE_MSAN_INITED();                                              \
  InterceptorScope interceptor_scope;                                \
  ret_type res = REAL(func)(s, __VA_ARGS__);                         \
  if (s) __msan_unpoison(s, sizeof(char_type) * (res + 1));          \
  return res;

INTERCEPTOR(SIZE_T, strftime, char *s, SIZE_T max, const char *format,
            __sanitizer_tm *tm) {
  INTERCEPTOR_STRFTIME_BODY(char, SIZE_T, strftime, s, max, format, tm);
}

INTERCEPTOR(SIZE_T, strftime_l, char *s, SIZE_T max, const char *format,
            __sanitizer_tm *tm, void *loc) {
  INTERCEPTOR_STRFTIME_BODY(char, SIZE_T, strftime_l, s, max, format, tm, loc);
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(SIZE_T, __strftime_l, char *s, SIZE_T max, const char *format,
            __sanitizer_tm *tm, void *loc) {
  INTERCEPTOR_STRFTIME_BODY(char, SIZE_T, __strftime_l, s, max, format, tm,
                            loc);
}
#define MSAN_MAYBE_INTERCEPT___STRFTIME_L INTERCEPT_FUNCTION(__strftime_l)
#else
#define MSAN_MAYBE_INTERCEPT___STRFTIME_L
#endif

INTERCEPTOR(SIZE_T, wcsftime, wchar_t *s, SIZE_T max, const wchar_t *format,
            __sanitizer_tm *tm) {
  INTERCEPTOR_STRFTIME_BODY(wchar_t, SIZE_T, wcsftime, s, max, format, tm);
}

INTERCEPTOR(SIZE_T, wcsftime_l, wchar_t *s, SIZE_T max, const wchar_t *format,
            __sanitizer_tm *tm, void *loc) {
  INTERCEPTOR_STRFTIME_BODY(wchar_t, SIZE_T, wcsftime_l, s, max, format, tm,
                            loc);
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(SIZE_T, __wcsftime_l, wchar_t *s, SIZE_T max, const wchar_t *format,
            __sanitizer_tm *tm, void *loc) {
  INTERCEPTOR_STRFTIME_BODY(wchar_t, SIZE_T, __wcsftime_l, s, max, format, tm,
                            loc);
}
#define MSAN_MAYBE_INTERCEPT___WCSFTIME_L INTERCEPT_FUNCTION(__wcsftime_l)
#else
#define MSAN_MAYBE_INTERCEPT___WCSFTIME_L
#endif

INTERCEPTOR(int, mbtowc, wchar_t *dest, const char *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  int res = REAL(mbtowc)(dest, src, n);
  if (res != -1 && dest) __msan_unpoison(dest, sizeof(wchar_t));
  return res;
}

INTERCEPTOR(SIZE_T, mbrtowc, wchar_t *dest, const char *src, SIZE_T n,
            void *ps) {
  ENSURE_MSAN_INITED();
  SIZE_T res = REAL(mbrtowc)(dest, src, n, ps);
  if (res != (SIZE_T)-1 && dest) __msan_unpoison(dest, sizeof(wchar_t));
  return res;
}

// wchar_t *wmemcpy(wchar_t *dest, const wchar_t *src, SIZE_T n);
INTERCEPTOR(wchar_t *, wmemcpy, wchar_t *dest, const wchar_t *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  wchar_t *res = REAL(wmemcpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, n * sizeof(wchar_t), &stack);
  return res;
}

#if !SANITIZER_NETBSD
INTERCEPTOR(wchar_t *, wmempcpy, wchar_t *dest, const wchar_t *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  wchar_t *res = REAL(wmempcpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, n * sizeof(wchar_t), &stack);
  return res;
}
#define MSAN_MAYBE_INTERCEPT_WMEMPCPY INTERCEPT_FUNCTION(wmempcpy)
#else
#define MSAN_MAYBE_INTERCEPT_WMEMPCPY
#endif

INTERCEPTOR(wchar_t *, wmemset, wchar_t *s, wchar_t c, SIZE_T n) {
  CHECK(MEM_IS_APP(s));
  ENSURE_MSAN_INITED();
  wchar_t *res = REAL(wmemset)(s, c, n);
  __msan_unpoison(s, n * sizeof(wchar_t));
  return res;
}

INTERCEPTOR(wchar_t *, wmemmove, wchar_t *dest, const wchar_t *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  wchar_t *res = REAL(wmemmove)(dest, src, n);
  MoveShadowAndOrigin(dest, src, n * sizeof(wchar_t), &stack);
  return res;
}

INTERCEPTOR(int, wcscmp, const wchar_t *s1, const wchar_t *s2) {
  ENSURE_MSAN_INITED();
  int res = REAL(wcscmp)(s1, s2);
  return res;
}

INTERCEPTOR(int, gettimeofday, void *tv, void *tz) {
  ENSURE_MSAN_INITED();
  int res = REAL(gettimeofday)(tv, tz);
  if (tv)
    __msan_unpoison(tv, 16);
  if (tz)
    __msan_unpoison(tz, 8);
  return res;
}

#if !SANITIZER_NETBSD
INTERCEPTOR(char *, fcvt, double x, int a, int *b, int *c) {
  ENSURE_MSAN_INITED();
  char *res = REAL(fcvt)(x, a, b, c);
  __msan_unpoison(b, sizeof(*b));
  __msan_unpoison(c, sizeof(*c));
  if (res)
    __msan_unpoison(res, internal_strlen(res) + 1);
  return res;
}
#define MSAN_MAYBE_INTERCEPT_FCVT INTERCEPT_FUNCTION(fcvt)
#else
#define MSAN_MAYBE_INTERCEPT_FCVT
#endif

INTERCEPTOR(char *, getenv, char *name) {
  if (msan_init_is_running)
    return REAL(getenv)(name);
  ENSURE_MSAN_INITED();
  char *res = REAL(getenv)(name);
  if (res)
    __msan_unpoison(res, internal_strlen(res) + 1);
  return res;
}

extern char **environ;

static void UnpoisonEnviron() {
  char **envp = environ;
  for (; *envp; ++envp) {
    __msan_unpoison(envp, sizeof(*envp));
    __msan_unpoison(*envp, internal_strlen(*envp) + 1);
  }
  // Trailing NULL pointer.
  __msan_unpoison(envp, sizeof(*envp));
}

INTERCEPTOR(int, setenv, const char *name, const char *value, int overwrite) {
  ENSURE_MSAN_INITED();
  CHECK_UNPOISONED_STRING(name, 0);
  int res = REAL(setenv)(name, value, overwrite);
  if (!res) UnpoisonEnviron();
  return res;
}

INTERCEPTOR(int, putenv, char *string) {
  ENSURE_MSAN_INITED();
  int res = REAL(putenv)(string);
  if (!res) UnpoisonEnviron();
  return res;
}

#define SANITIZER_STAT_LINUX (SANITIZER_LINUX && __GLIBC_PREREQ(2, 33))
#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_STAT_LINUX
INTERCEPTOR(int, fstat, int fd, void *buf) {
  ENSURE_MSAN_INITED();
  int res = REAL(fstat)(fd, buf);
  if (!res)
    __msan_unpoison(buf, __sanitizer::struct_stat_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_FSTAT MSAN_INTERCEPT_FUNC(fstat)
#else
#define MSAN_MAYBE_INTERCEPT_FSTAT
#endif

#if SANITIZER_STAT_LINUX
INTERCEPTOR(int, fstat64, int fd, void *buf) {
  ENSURE_MSAN_INITED();
  int res = REAL(fstat64)(fd, buf);
  if (!res)
    __msan_unpoison(buf, __sanitizer::struct_stat64_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_FSTAT64 MSAN_INTERCEPT_FUNC(fstat64)
#else
#  define MSAN_MAYBE_INTERCEPT_FSTAT64
#endif

#if SANITIZER_GLIBC
INTERCEPTOR(int, __fxstat, int magic, int fd, void *buf) {
  ENSURE_MSAN_INITED();
  int res = REAL(__fxstat)(magic, fd, buf);
  if (!res)
    __msan_unpoison(buf, __sanitizer::struct_stat_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT___FXSTAT MSAN_INTERCEPT_FUNC(__fxstat)
#else
#define MSAN_MAYBE_INTERCEPT___FXSTAT
#endif

#if SANITIZER_GLIBC
INTERCEPTOR(int, __fxstat64, int magic, int fd, void *buf) {
  ENSURE_MSAN_INITED();
  int res = REAL(__fxstat64)(magic, fd, buf);
  if (!res)
    __msan_unpoison(buf, __sanitizer::struct_stat64_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT___FXSTAT64 MSAN_INTERCEPT_FUNC(__fxstat64)
#else
#  define MSAN_MAYBE_INTERCEPT___FXSTAT64
#endif

#if SANITIZER_FREEBSD || SANITIZER_NETBSD || SANITIZER_STAT_LINUX
INTERCEPTOR(int, fstatat, int fd, char *pathname, void *buf, int flags) {
  ENSURE_MSAN_INITED();
  int res = REAL(fstatat)(fd, pathname, buf, flags);
  if (!res) __msan_unpoison(buf, __sanitizer::struct_stat_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_FSTATAT MSAN_INTERCEPT_FUNC(fstatat)
#else
#  define MSAN_MAYBE_INTERCEPT_FSTATAT
#endif

#if SANITIZER_STAT_LINUX
INTERCEPTOR(int, fstatat64, int fd, char *pathname, void *buf, int flags) {
  ENSURE_MSAN_INITED();
  int res = REAL(fstatat64)(fd, pathname, buf, flags);
  if (!res)
    __msan_unpoison(buf, __sanitizer::struct_stat64_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_FSTATAT64 MSAN_INTERCEPT_FUNC(fstatat64)
#else
#  define MSAN_MAYBE_INTERCEPT_FSTATAT64
#endif

#if SANITIZER_GLIBC
INTERCEPTOR(int, __fxstatat, int magic, int fd, char *pathname, void *buf,
            int flags) {
  ENSURE_MSAN_INITED();
  int res = REAL(__fxstatat)(magic, fd, pathname, buf, flags);
  if (!res) __msan_unpoison(buf, __sanitizer::struct_stat_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT___FXSTATAT MSAN_INTERCEPT_FUNC(__fxstatat)
#else
#  define MSAN_MAYBE_INTERCEPT___FXSTATAT
#endif

#if SANITIZER_GLIBC
INTERCEPTOR(int, __fxstatat64, int magic, int fd, char *pathname, void *buf,
            int flags) {
  ENSURE_MSAN_INITED();
  int res = REAL(__fxstatat64)(magic, fd, pathname, buf, flags);
  if (!res) __msan_unpoison(buf, __sanitizer::struct_stat64_sz);
  return res;
}
#  define MSAN_MAYBE_INTERCEPT___FXSTATAT64 MSAN_INTERCEPT_FUNC(__fxstatat64)
#else
#  define MSAN_MAYBE_INTERCEPT___FXSTATAT64
#endif

INTERCEPTOR(int, pipe, int pipefd[2]) {
  if (msan_init_is_running)
    return REAL(pipe)(pipefd);
  ENSURE_MSAN_INITED();
  int res = REAL(pipe)(pipefd);
  if (!res)
    __msan_unpoison(pipefd, sizeof(int[2]));
  return res;
}

INTERCEPTOR(int, pipe2, int pipefd[2], int flags) {
  ENSURE_MSAN_INITED();
  int res = REAL(pipe2)(pipefd, flags);
  if (!res)
    __msan_unpoison(pipefd, sizeof(int[2]));
  return res;
}

INTERCEPTOR(int, socketpair, int domain, int type, int protocol, int sv[2]) {
  ENSURE_MSAN_INITED();
  int res = REAL(socketpair)(domain, type, protocol, sv);
  if (!res)
    __msan_unpoison(sv, sizeof(int[2]));
  return res;
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(char *, fgets_unlocked, char *s, int size, void *stream) {
  ENSURE_MSAN_INITED();
  char *res = REAL(fgets_unlocked)(s, size, stream);
  if (res)
    __msan_unpoison(s, internal_strlen(s) + 1);
  return res;
}
#define MSAN_MAYBE_INTERCEPT_FGETS_UNLOCKED INTERCEPT_FUNCTION(fgets_unlocked)
#else
#define MSAN_MAYBE_INTERCEPT_FGETS_UNLOCKED
#endif

#define INTERCEPTOR_GETRLIMIT_BODY(func, resource, rlim)  \
  if (msan_init_is_running)                               \
    return REAL(getrlimit)(resource, rlim);               \
  ENSURE_MSAN_INITED();                                   \
  int res = REAL(func)(resource, rlim);                   \
  if (!res)                                               \
    __msan_unpoison(rlim, __sanitizer::struct_rlimit_sz); \
  return res

INTERCEPTOR(int, getrlimit, int resource, void *rlim) {
  INTERCEPTOR_GETRLIMIT_BODY(getrlimit, resource, rlim);
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(int, __getrlimit, int resource, void *rlim) {
  INTERCEPTOR_GETRLIMIT_BODY(__getrlimit, resource, rlim);
}

INTERCEPTOR(int, getrlimit64, int resource, void *rlim) {
  if (msan_init_is_running) return REAL(getrlimit64)(resource, rlim);
  ENSURE_MSAN_INITED();
  int res = REAL(getrlimit64)(resource, rlim);
  if (!res) __msan_unpoison(rlim, __sanitizer::struct_rlimit64_sz);
  return res;
}

INTERCEPTOR(int, prlimit, int pid, int resource, void *new_rlimit,
            void *old_rlimit) {
  if (msan_init_is_running)
    return REAL(prlimit)(pid, resource, new_rlimit, old_rlimit);
  ENSURE_MSAN_INITED();
  CHECK_UNPOISONED(new_rlimit, __sanitizer::struct_rlimit_sz);
  int res = REAL(prlimit)(pid, resource, new_rlimit, old_rlimit);
  if (!res) __msan_unpoison(old_rlimit, __sanitizer::struct_rlimit_sz);
  return res;
}

INTERCEPTOR(int, prlimit64, int pid, int resource, void *new_rlimit,
            void *old_rlimit) {
  if (msan_init_is_running)
    return REAL(prlimit64)(pid, resource, new_rlimit, old_rlimit);
  ENSURE_MSAN_INITED();
  CHECK_UNPOISONED(new_rlimit, __sanitizer::struct_rlimit64_sz);
  int res = REAL(prlimit64)(pid, resource, new_rlimit, old_rlimit);
  if (!res) __msan_unpoison(old_rlimit, __sanitizer::struct_rlimit64_sz);
  return res;
}

#define MSAN_MAYBE_INTERCEPT___GETRLIMIT INTERCEPT_FUNCTION(__getrlimit)
#define MSAN_MAYBE_INTERCEPT_GETRLIMIT64 INTERCEPT_FUNCTION(getrlimit64)
#define MSAN_MAYBE_INTERCEPT_PRLIMIT INTERCEPT_FUNCTION(prlimit)
#define MSAN_MAYBE_INTERCEPT_PRLIMIT64 INTERCEPT_FUNCTION(prlimit64)
#else
#define MSAN_MAYBE_INTERCEPT___GETRLIMIT
#define MSAN_MAYBE_INTERCEPT_GETRLIMIT64
#define MSAN_MAYBE_INTERCEPT_PRLIMIT
#define MSAN_MAYBE_INTERCEPT_PRLIMIT64
#endif

INTERCEPTOR(int, gethostname, char *name, SIZE_T len) {
  ENSURE_MSAN_INITED();
  int res = REAL(gethostname)(name, len);
  if (!res || (res == -1 && errno == errno_ENAMETOOLONG)) {
    SIZE_T real_len = internal_strnlen(name, len);
    if (real_len < len)
      ++real_len;
    __msan_unpoison(name, real_len);
  }
  return res;
}

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(int, epoll_wait, int epfd, void *events, int maxevents,
    int timeout) {
  ENSURE_MSAN_INITED();
  int res = REAL(epoll_wait)(epfd, events, maxevents, timeout);
  if (res > 0) {
    __msan_unpoison(events, __sanitizer::struct_epoll_event_sz * res);
  }
  return res;
}
#define MSAN_MAYBE_INTERCEPT_EPOLL_WAIT INTERCEPT_FUNCTION(epoll_wait)
#else
#define MSAN_MAYBE_INTERCEPT_EPOLL_WAIT
#endif

#if !SANITIZER_FREEBSD && !SANITIZER_NETBSD
INTERCEPTOR(int, epoll_pwait, int epfd, void *events, int maxevents,
    int timeout, void *sigmask) {
  ENSURE_MSAN_INITED();
  int res = REAL(epoll_pwait)(epfd, events, maxevents, timeout, sigmask);
  if (res > 0) {
    __msan_unpoison(events, __sanitizer::struct_epoll_event_sz * res);
  }
  return res;
}
#define MSAN_MAYBE_INTERCEPT_EPOLL_PWAIT INTERCEPT_FUNCTION(epoll_pwait)
#else
#define MSAN_MAYBE_INTERCEPT_EPOLL_PWAIT
#endif

INTERCEPTOR(void *, calloc, SIZE_T nmemb, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  return msan_calloc(nmemb, size, &stack);
}

INTERCEPTOR(void *, realloc, void *ptr, SIZE_T size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  GET_MALLOC_STACK_TRACE;
  return msan_realloc(ptr, size, &stack);
}

INTERCEPTOR(void *, reallocarray, void *ptr, SIZE_T nmemb, SIZE_T size) {
  GET_MALLOC_STACK_TRACE;
  return msan_reallocarray(ptr, nmemb, size, &stack);
}

INTERCEPTOR(void *, malloc, SIZE_T size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  GET_MALLOC_STACK_TRACE;
  return msan_malloc(size, &stack);
}

void __msan_allocated_memory(const void *data, uptr size) {
  if (flags()->poison_in_malloc) {
    GET_MALLOC_STACK_TRACE;
    stack.tag = STACK_TRACE_TAG_POISON;
    PoisonMemory(data, size, &stack);
  }
}

void __msan_copy_shadow(void *dest, const void *src, uptr n) {
  GET_STORE_STACK_TRACE;
  MoveShadowAndOrigin(dest, src, n, &stack);
}

void __sanitizer_dtor_callback(const void *data, uptr size) {
  if (flags()->poison_in_dtor) {
    GET_MALLOC_STACK_TRACE;
    stack.tag = STACK_TRACE_TAG_POISON;
    PoisonMemory(data, size, &stack);
  }
}

void __sanitizer_dtor_callback_fields(const void *data, uptr size) {
  if (flags()->poison_in_dtor) {
    GET_MALLOC_STACK_TRACE;
    stack.tag = STACK_TRACE_TAG_FIELDS;
    PoisonMemory(data, size, &stack);
  }
}

void __sanitizer_dtor_callback_vptr(const void *data) {
  if (flags()->poison_in_dtor) {
    GET_MALLOC_STACK_TRACE;
    stack.tag = STACK_TRACE_TAG_VPTR;
    PoisonMemory(data, sizeof(void *), &stack);
  }
}

template <class Mmap>
static void *mmap_interceptor(Mmap real_mmap, void *addr, SIZE_T length,
                              int prot, int flags, int fd, OFF64_T offset) {
  SIZE_T rounded_length = RoundUpTo(length, GetPageSize());
  void *end_addr = (char *)addr + (rounded_length - 1);
  if (addr && (!MEM_IS_APP(addr) || !MEM_IS_APP(end_addr))) {
    if (flags & map_fixed) {
      errno = errno_EINVAL;
      return (void *)-1;
    } else {
      addr = nullptr;
    }
  }
  void *res = real_mmap(addr, length, prot, flags, fd, offset);
  if (res != (void *)-1) {
    void *end_res = (char *)res + (rounded_length - 1);
    if (MEM_IS_APP(res) && MEM_IS_APP(end_res)) {
      __msan_unpoison(res, rounded_length);
    } else {
      // Application has attempted to map more memory than is supported by
      // MSAN. Act as if we ran out of memory.
      internal_munmap(res, length);
      errno = errno_ENOMEM;
      return (void *)-1;
    }
  }
  return res;
}

INTERCEPTOR(int, getrusage, int who, void *usage) {
  ENSURE_MSAN_INITED();
  int res = REAL(getrusage)(who, usage);
  if (res == 0) {
    __msan_unpoison(usage, __sanitizer::struct_rusage_sz);
  }
  return res;
}

class SignalHandlerScope {
 public:
  SignalHandlerScope() {
    if (MsanThread *t = GetCurrentThread())
      t->EnterSignalHandler();
  }
  ~SignalHandlerScope() {
    if (MsanThread *t = GetCurrentThread())
      t->LeaveSignalHandler();
  }
};

// sigactions_mu guarantees atomicity of sigaction() and signal() calls.
// Access to sigactions[] is gone with relaxed atomics to avoid data race with
// the signal handler.
const int kMaxSignals = 1024;
static atomic_uintptr_t sigactions[kMaxSignals];
static StaticSpinMutex sigactions_mu;

static void SignalHandler(int signo) {
  SignalHandlerScope signal_handler_scope;
  ScopedThreadLocalStateBackup stlsb;
  UnpoisonParam(1);

  typedef void (*signal_cb)(int x);
  signal_cb cb =
      (signal_cb)atomic_load(&sigactions[signo], memory_order_relaxed);
  cb(signo);
}

static void SignalAction(int signo, void *si, void *uc) {
  SignalHandlerScope signal_handler_scope;
  ScopedThreadLocalStateBackup stlsb;
  UnpoisonParam(3);
  __msan_unpoison(si, sizeof(__sanitizer_sigaction));
  __msan_unpoison(uc, ucontext_t_sz(uc));

  typedef void (*sigaction_cb)(int, void *, void *);
  sigaction_cb cb =
      (sigaction_cb)atomic_load(&sigactions[signo], memory_order_relaxed);
  cb(signo, si, uc);
  CHECK_UNPOISONED(uc, ucontext_t_sz(uc));
}

static void read_sigaction(const __sanitizer_sigaction *act) {
  CHECK_UNPOISONED(&act->sa_flags, sizeof(act->sa_flags));
  if (act->sa_flags & __sanitizer::sa_siginfo)
    CHECK_UNPOISONED(&act->sigaction, sizeof(act->sigaction));
  else
    CHECK_UNPOISONED(&act->handler, sizeof(act->handler));
  CHECK_UNPOISONED(&act->sa_mask, sizeof(act->sa_mask));
}

extern "C" int pthread_attr_init(void *attr);
extern "C" int pthread_attr_destroy(void *attr);

static void *MsanThreadStartFunc(void *arg) {
  MsanThread *t = (MsanThread *)arg;
  SetCurrentThread(t);
  t->Init();
  SetSigProcMask(&t->starting_sigset_, nullptr);
  return t->ThreadStart();
}

INTERCEPTOR(int, pthread_create, void *th, void *attr, void *(*callback)(void*),
            void * param) {
  ENSURE_MSAN_INITED(); // for GetTlsSize()
  __sanitizer_pthread_attr_t myattr;
  if (!attr) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }

  AdjustStackSize(attr);

  MsanThread *t = MsanThread::Create(callback, param);
  ScopedBlockSignals block(&t->starting_sigset_);
  int res = REAL(pthread_create)(th, attr, MsanThreadStartFunc, t);

  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  if (!res) {
    __msan_unpoison(th, __sanitizer::pthread_t_sz);
  }
  return res;
}

INTERCEPTOR(int, pthread_key_create, __sanitizer_pthread_key_t *key,
            void (*dtor)(void *value)) {
  if (msan_init_is_running) return REAL(pthread_key_create)(key, dtor);
  ENSURE_MSAN_INITED();
  int res = REAL(pthread_key_create)(key, dtor);
  if (!res && key)
    __msan_unpoison(key, sizeof(*key));
  return res;
}

#if SANITIZER_NETBSD
INTERCEPTOR(int, __libc_thr_keycreate, __sanitizer_pthread_key_t *m,
            void (*dtor)(void *value))
ALIAS(WRAP(pthread_key_create));
#endif

INTERCEPTOR(int, pthread_join, void *thread, void **retval) {
  ENSURE_MSAN_INITED();
  int res = REAL(pthread_join)(thread, retval);
  if (!res && retval)
    __msan_unpoison(retval, sizeof(*retval));
  return res;
}

#if SANITIZER_GLIBC
INTERCEPTOR(int, pthread_tryjoin_np, void *thread, void **retval) {
  ENSURE_MSAN_INITED();
  int res = REAL(pthread_tryjoin_np)(thread, retval);
  if (!res && retval)
    __msan_unpoison(retval, sizeof(*retval));
  return res;
}

INTERCEPTOR(int, pthread_timedjoin_np, void *thread, void **retval,
            const struct timespec *abstime) {
  int res = REAL(pthread_timedjoin_np)(thread, retval, abstime);
  if (!res && retval)
    __msan_unpoison(retval, sizeof(*retval));
  return res;
}
#endif

DEFINE_INTERNAL_PTHREAD_FUNCTIONS

extern char *tzname[2];

INTERCEPTOR(void, tzset, int fake) {
  ENSURE_MSAN_INITED();
  InterceptorScope interceptor_scope;
  REAL(tzset)(fake);
  if (tzname[0])
    __msan_unpoison(tzname[0], internal_strlen(tzname[0]) + 1);
  if (tzname[1])
    __msan_unpoison(tzname[1], internal_strlen(tzname[1]) + 1);
  return;
}

struct MSanAtExitRecord {
  void (*func)(void *arg);
  void *arg;
};

struct InterceptorContext {
  Mutex atexit_mu;
  Vector<struct MSanAtExitRecord *> AtExitStack;

  InterceptorContext()
      : AtExitStack() {
  }
};

alignas(64) static char interceptor_placeholder[sizeof(InterceptorContext)];
InterceptorContext *interceptor_ctx() {
  return reinterpret_cast<InterceptorContext*>(&interceptor_placeholder[0]);
}

void MSanAtExitWrapper() {
  MSanAtExitRecord *r;
  {
    Lock l(&interceptor_ctx()->atexit_mu);

    uptr element = interceptor_ctx()->AtExitStack.Size() - 1;
    r = interceptor_ctx()->AtExitStack[element];
    interceptor_ctx()->AtExitStack.PopBack();
  }

  UnpoisonParam(1);
  ((void(*)())r->func)();
  InternalFree(r);
}

void MSanCxaAtExitWrapper(void *arg) {
  UnpoisonParam(1);
  MSanAtExitRecord *r = (MSanAtExitRecord *)arg;
  // libc before 2.27 had race which caused occasional double handler execution
  // https://sourceware.org/ml/libc-alpha/2017-08/msg01204.html
  if (!r->func)
    return;
  r->func(r->arg);
  r->func = nullptr;
}

static int setup_at_exit_wrapper(void(*f)(), void *arg, void *dso);

// Unpoison argument shadow for C++ module destructors.
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
  if (msan_init_is_running) return REAL(__cxa_atexit)(func, arg, dso_handle);
  return setup_at_exit_wrapper((void(*)())func, arg, dso_handle);
}

// Unpoison argument shadow for C++ module destructors.
INTERCEPTOR(int, atexit, void (*func)()) {
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  if (msan_init_is_running)
    return REAL(__cxa_atexit)((void (*)(void *a))func, 0, 0);
  return setup_at_exit_wrapper((void(*)())func, 0, 0);
}

static int setup_at_exit_wrapper(void(*f)(), void *arg, void *dso) {
  ENSURE_MSAN_INITED();
  MSanAtExitRecord *r =
      (MSanAtExitRecord *)InternalAlloc(sizeof(MSanAtExitRecord));
  r->func = (void(*)(void *a))f;
  r->arg = arg;
  int res;
  if (!dso) {
    // NetBSD does not preserve the 2nd argument if dso is equal to 0
    // Store ctx in a local stack-like structure

    Lock l(&interceptor_ctx()->atexit_mu);

    res = REAL(__cxa_atexit)((void (*)(void *a))MSanAtExitWrapper, 0, 0);
    if (!res) {
      interceptor_ctx()->AtExitStack.PushBack(r);
    }
  } else {
    res = REAL(__cxa_atexit)(MSanCxaAtExitWrapper, r, dso);
  }
  return res;
}

// NetBSD ships with openpty(3) in -lutil, that needs to be prebuilt explicitly
// with MSan.
#if SANITIZER_LINUX
INTERCEPTOR(int, openpty, int *aparent, int *aworker, char *name,
            const void *termp, const void *winp) {
  ENSURE_MSAN_INITED();
  InterceptorScope interceptor_scope;
  int res = REAL(openpty)(aparent, aworker, name, termp, winp);
  if (!res) {
    __msan_unpoison(aparent, sizeof(*aparent));
    __msan_unpoison(aworker, sizeof(*aworker));
  }
  return res;
}
#define MSAN_MAYBE_INTERCEPT_OPENPTY INTERCEPT_FUNCTION(openpty)
#else
#define MSAN_MAYBE_INTERCEPT_OPENPTY
#endif

// NetBSD ships with forkpty(3) in -lutil, that needs to be prebuilt explicitly
// with MSan.
#if SANITIZER_LINUX
INTERCEPTOR(int, forkpty, int *aparent, char *name, const void *termp,
            const void *winp) {
  ENSURE_MSAN_INITED();
  InterceptorScope interceptor_scope;
  int res = REAL(forkpty)(aparent, name, termp, winp);
  if (res != -1)
    __msan_unpoison(aparent, sizeof(*aparent));
  return res;
}
#define MSAN_MAYBE_INTERCEPT_FORKPTY INTERCEPT_FUNCTION(forkpty)
#else
#define MSAN_MAYBE_INTERCEPT_FORKPTY
#endif

struct MSanInterceptorContext {
  bool in_interceptor_scope;
};

namespace __msan {

int OnExit() {
  // FIXME: ask frontend whether we need to return failure.
  return 0;
}

} // namespace __msan

// A version of CHECK_UNPOISONED using a saved scope value. Used in common
// interceptors.
#define CHECK_UNPOISONED_CTX(ctx, x, n)                         \
  do {                                                          \
    if (!((MSanInterceptorContext *)ctx)->in_interceptor_scope) \
      CHECK_UNPOISONED_0(x, n);                                 \
  } while (0)

#define MSAN_INTERCEPT_FUNC(name)                                       \
  do {                                                                  \
    if (!INTERCEPT_FUNCTION(name))                                      \
      VReport(1, "MemorySanitizer: failed to intercept '%s'\n", #name); \
  } while (0)

#define MSAN_INTERCEPT_FUNC_VER(name, ver)                                 \
  do {                                                                     \
    if (!INTERCEPT_FUNCTION_VER(name, ver))                                \
      VReport(1, "MemorySanitizer: failed to intercept '%s@@%s'\n", #name, \
              ver);                                                        \
  } while (0)
#define MSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)             \
  do {                                                                      \
    if (!INTERCEPT_FUNCTION_VER(name, ver) && !INTERCEPT_FUNCTION(name))    \
      VReport(1, "MemorySanitizer: failed to intercept '%s@@%s' or '%s'\n", \
              #name, ver, #name);                                           \
  } while (0)

#define COMMON_INTERCEPT_FUNCTION(name) MSAN_INTERCEPT_FUNC(name)
#define COMMON_INTERCEPT_FUNCTION_VER(name, ver) \
  MSAN_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
  MSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_UNPOISON_PARAM(count)  \
  UnpoisonParam(count)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) \
  __msan_unpoison(ptr, size)
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) \
  CHECK_UNPOISONED_CTX(ctx, ptr, size)
#define COMMON_INTERCEPTOR_INITIALIZE_RANGE(ptr, size) \
  __msan_unpoison(ptr, size)
#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)              \
  if (msan_init_is_running)                                   \
    return REAL(func)(__VA_ARGS__);                           \
  ENSURE_MSAN_INITED();                                       \
  MSanInterceptorContext msan_ctx = {IsInInterceptorScope()}; \
  ctx = (void *)&msan_ctx;                                    \
  (void)ctx;                                                  \
  InterceptorScope interceptor_scope;                         \
  __msan_unpoison(__errno_location(), sizeof(int));
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
  do {                                            \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
  do {                                                      \
  } while (false)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) \
  do {                                                \
  } while (false)  // FIXME
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name) \
  do {                                                         \
  } while (false)  // FIXME
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
#define COMMON_INTERCEPTOR_ON_EXIT(ctx) OnExit()
#define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle)                    \
  do {                                                                         \
    link_map *map = GET_LINK_MAP_BY_DLOPEN_HANDLE((handle));                   \
    if (filename && map)                                                       \
      ForEachMappedRegion(map, __msan_unpoison);                               \
  } while (false)

#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!msan_inited)

#define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end)                           \
  if (MsanThread *t = GetCurrentThread()) {                                    \
    *begin = t->tls_begin();                                                   \
    *end = t->tls_end();                                                       \
  } else {                                                                     \
    *begin = *end = 0;                                                         \
  }

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
  {                                                         \
    (void)ctx;                                              \
    return __msan_memset(block, c, size);                   \
  }
#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
  {                                                          \
    (void)ctx;                                               \
    return __msan_memmove(to, from, size);                   \
  }
#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
  {                                                         \
    (void)ctx;                                              \
    return __msan_memcpy(to, from, size);                   \
  }

#define COMMON_INTERCEPTOR_COPY_STRING(ctx, to, from, size) \
  do {                                                      \
    GET_STORE_STACK_TRACE;                                  \
    CopyShadowAndOrigin(to, from, size, &stack);            \
    __msan_unpoison(to + size, 1);                          \
  } while (false)

#define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, length, prot, flags, fd, \
                                     offset)                                   \
  do {                                                                         \
    return mmap_interceptor(REAL(mmap), addr, sz, prot, flags, fd, off);       \
  } while (false)

#include "sanitizer_common/sanitizer_platform_interceptors.h"
#include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"
#include "sanitizer_common/sanitizer_common_interceptors.inc"

static uptr signal_impl(int signo, uptr cb);
static int sigaction_impl(int signo, const __sanitizer_sigaction *act,
                          __sanitizer_sigaction *oldact);

#define SIGNAL_INTERCEPTOR_SIGACTION_IMPL(signo, act, oldact) \
  { return sigaction_impl(signo, act, oldact); }

#define SIGNAL_INTERCEPTOR_SIGNAL_IMPL(func, signo, handler) \
  {                                                          \
    handler = signal_impl(signo, handler);                   \
    InterceptorScope interceptor_scope;                      \
    return REAL(func)(signo, handler);                       \
  }

#define SIGNAL_INTERCEPTOR_ENTER() ENSURE_MSAN_INITED()

#include "sanitizer_common/sanitizer_signal_interceptors.inc"

static int sigaction_impl(int signo, const __sanitizer_sigaction *act,
                          __sanitizer_sigaction *oldact) {
  ENSURE_MSAN_INITED();
  if (signo <= 0 || signo >= kMaxSignals) {
    errno = errno_EINVAL;
    return -1;
  }
  if (act) read_sigaction(act);
  int res;
  if (flags()->wrap_signals) {
    SpinMutexLock lock(&sigactions_mu);
    uptr old_cb = atomic_load(&sigactions[signo], memory_order_relaxed);
    __sanitizer_sigaction new_act;
    __sanitizer_sigaction *pnew_act = act ? &new_act : nullptr;
    if (act) {
      REAL(memcpy)(pnew_act, act, sizeof(__sanitizer_sigaction));
      uptr cb = (uptr)pnew_act->sigaction;
      uptr new_cb = (pnew_act->sa_flags & __sanitizer::sa_siginfo)
                        ? (uptr)SignalAction
                        : (uptr)SignalHandler;
      if (cb != __sanitizer::sig_ign && cb != __sanitizer::sig_dfl) {
        atomic_store(&sigactions[signo], cb, memory_order_relaxed);
        pnew_act->sigaction = (decltype(pnew_act->sigaction))new_cb;
      }
    }
    res = REAL(SIGACTION_SYMNAME)(signo, pnew_act, oldact);
    if (res == 0 && oldact) {
      uptr cb = (uptr)oldact->sigaction;
      if (cb == (uptr)SignalAction || cb == (uptr)SignalHandler) {
        oldact->sigaction = (decltype(oldact->sigaction))old_cb;
      }
    }
  } else {
    res = REAL(SIGACTION_SYMNAME)(signo, act, oldact);
  }

  if (res == 0 && oldact) {
    __msan_unpoison(oldact, sizeof(__sanitizer_sigaction));
  }
  return res;
}

static uptr signal_impl(int signo, uptr cb) {
  ENSURE_MSAN_INITED();
  if (signo <= 0 || signo >= kMaxSignals) {
    errno = errno_EINVAL;
    return -1;
  }
  if (flags()->wrap_signals) {
    SpinMutexLock lock(&sigactions_mu);
    if (cb != __sanitizer::sig_ign && cb != __sanitizer::sig_dfl) {
      atomic_store(&sigactions[signo], cb, memory_order_relaxed);
      cb = (uptr)&SignalHandler;
    }
  }
  return cb;
}

#define COMMON_SYSCALL_PRE_READ_RANGE(p, s) CHECK_UNPOISONED(p, s)
#define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) \
  do {                                       \
  } while (false)
#define COMMON_SYSCALL_POST_READ_RANGE(p, s) \
  do {                                       \
  } while (false)
#define COMMON_SYSCALL_POST_WRITE_RANGE(p, s) __msan_unpoison(p, s)
#include "sanitizer_common/sanitizer_common_syscalls.inc"
#include "sanitizer_common/sanitizer_syscalls_netbsd.inc"

INTERCEPTOR(const char *, strsignal, int sig) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, strsignal, sig);
  const char *res = REAL(strsignal)(sig);
  if (res)
    __msan_unpoison(res, internal_strlen(res) + 1);
  return res;
}

INTERCEPTOR(int, dladdr, void *addr, void *info) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, dladdr, addr, info);
  int res = REAL(dladdr)(addr, info);
  if (res != 0)
    UnpoisonDllAddrInfo(info);
  return res;
}

#if SANITIZER_GLIBC
INTERCEPTOR(int, dladdr1, void *addr, void *info, void **extra_info,
            int flags) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, dladdr1, addr, info, extra_info, flags);
  int res = REAL(dladdr1)(addr, info, extra_info, flags);
  if (res != 0) {
    UnpoisonDllAddrInfo(info);
    UnpoisonDllAddr1ExtraInfo(extra_info, flags);
  }
  return res;
}
#  define MSAN_MAYBE_INTERCEPT_DLADDR1 MSAN_INTERCEPT_FUNC(dladdr1)
#else
#define MSAN_MAYBE_INTERCEPT_DLADDR1
#endif

INTERCEPTOR(char *, dlerror, int fake) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, dlerror, fake);
  char *res = REAL(dlerror)(fake);
  if (res)
    __msan_unpoison(res, internal_strlen(res) + 1);
  return res;
}

typedef int (*dl_iterate_phdr_cb)(__sanitizer_dl_phdr_info *info, SIZE_T size,
                                  void *data);
struct dl_iterate_phdr_data {
  dl_iterate_phdr_cb callback;
  void *data;
};

static int msan_dl_iterate_phdr_cb(__sanitizer_dl_phdr_info *info, SIZE_T size,
                                   void *data) {
  if (info) {
    __msan_unpoison(info, size);
    if (info->dlpi_phdr && info->dlpi_phnum)
      __msan_unpoison(info->dlpi_phdr, struct_ElfW_Phdr_sz * info->dlpi_phnum);
    if (info->dlpi_name)
      __msan_unpoison(info->dlpi_name, internal_strlen(info->dlpi_name) + 1);
  }
  dl_iterate_phdr_data *cbdata = (dl_iterate_phdr_data *)data;
  UnpoisonParam(3);
  return cbdata->callback(info, size, cbdata->data);
}

INTERCEPTOR(void *, shmat, int shmid, const void *shmaddr, int shmflg) {
  ENSURE_MSAN_INITED();
  void *p = REAL(shmat)(shmid, shmaddr, shmflg);
  if (p != (void *)-1) {
    __sanitizer_shmid_ds ds;
    int res = REAL(shmctl)(shmid, shmctl_ipc_stat, &ds);
    if (!res) {
      __msan_unpoison(p, ds.shm_segsz);
    }
  }
  return p;
}

INTERCEPTOR(int, dl_iterate_phdr, dl_iterate_phdr_cb callback, void *data) {
  void *ctx;
  COMMON_INTERCEPTOR_ENTER(ctx, dl_iterate_phdr, callback, data);
  dl_iterate_phdr_data cbdata;
  cbdata.callback = callback;
  cbdata.data = data;
  int res = REAL(dl_iterate_phdr)(msan_dl_iterate_phdr_cb, (void *)&cbdata);
  return res;
}

// wchar_t *wcschr(const wchar_t *wcs, wchar_t wc);
INTERCEPTOR(wchar_t *, wcschr, void *s, wchar_t wc, void *ps) {
  ENSURE_MSAN_INITED();
  wchar_t *res = REAL(wcschr)(s, wc, ps);
  return res;
}

// wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
INTERCEPTOR(wchar_t *, wcscpy, wchar_t *dest, const wchar_t *src) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  wchar_t *res = REAL(wcscpy)(dest, src);
  CopyShadowAndOrigin(dest, src, sizeof(wchar_t) * (internal_wcslen(src) + 1),
                      &stack);
  return res;
}

INTERCEPTOR(wchar_t *, wcsncpy, wchar_t *dest, const wchar_t *src, SIZE_T n) {
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  SIZE_T copy_size = internal_wcsnlen(src, n);
  if (copy_size < n) copy_size++;           // trailing \0
  wchar_t *res = REAL(wcsncpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, copy_size * sizeof(wchar_t), &stack);
  __msan_unpoison(dest + copy_size, (n - copy_size) * sizeof(wchar_t));
  return res;
}

// These interface functions reside here so that they can use
// REAL(memset), etc.
void __msan_unpoison(const void *a, uptr size) {
  if (!MEM_IS_APP(a)) return;
  SetShadow(a, size, 0);
}

void __msan_poison(const void *a, uptr size) {
  if (!MEM_IS_APP(a)) return;
  SetShadow(a, size, __msan::flags()->poison_heap_with_zeroes ? 0 : -1);
}

void __msan_poison_stack(void *a, uptr size) {
  if (!MEM_IS_APP(a)) return;
  SetShadow(a, size, __msan::flags()->poison_stack_with_zeroes ? 0 : -1);
}

void __msan_unpoison_param(uptr n) { UnpoisonParam(n); }

void __msan_clear_and_unpoison(void *a, uptr size) {
  REAL(memset)(a, 0, size);
  SetShadow(a, size, 0);
}

void *__msan_memcpy(void *dest, const void *src, SIZE_T n) {
  if (!msan_inited) return internal_memcpy(dest, src, n);
  if (msan_init_is_running || __msan::IsInSymbolizerOrUnwider())
    return REAL(memcpy)(dest, src, n);
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  void *res = REAL(memcpy)(dest, src, n);
  CopyShadowAndOrigin(dest, src, n, &stack);
  return res;
}

void *__msan_memset(void *s, int c, SIZE_T n) {
  if (!msan_inited) return internal_memset(s, c, n);
  if (msan_init_is_running) return REAL(memset)(s, c, n);
  ENSURE_MSAN_INITED();
  void *res = REAL(memset)(s, c, n);
  __msan_unpoison(s, n);
  return res;
}

void *__msan_memmove(void *dest, const void *src, SIZE_T n) {
  if (!msan_inited) return internal_memmove(dest, src, n);
  if (msan_init_is_running) return REAL(memmove)(dest, src, n);
  ENSURE_MSAN_INITED();
  GET_STORE_STACK_TRACE;
  void *res = REAL(memmove)(dest, src, n);
  MoveShadowAndOrigin(dest, src, n, &stack);
  return res;
}

void __msan_unpoison_string(const char* s) {
  if (!MEM_IS_APP(s)) return;
  __msan_unpoison(s, internal_strlen(s) + 1);
}

namespace __msan {

void InitializeInterceptors() {
  static int inited = 0;
  CHECK_EQ(inited, 0);

  __interception::DoesNotSupportStaticLinking();

  new(interceptor_ctx()) InterceptorContext();

  InitializeCommonInterceptors();
  InitializeSignalInterceptors();

  INTERCEPT_FUNCTION(posix_memalign);
  MSAN_MAYBE_INTERCEPT_MEMALIGN;
  MSAN_MAYBE_INTERCEPT___LIBC_MEMALIGN;
  INTERCEPT_FUNCTION(valloc);
  MSAN_MAYBE_INTERCEPT_PVALLOC;
  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(reallocarray);
  INTERCEPT_FUNCTION(free);
  MSAN_MAYBE_INTERCEPT_CFREE;
  MSAN_MAYBE_INTERCEPT_MALLOC_USABLE_SIZE;
  MSAN_MAYBE_INTERCEPT_MALLINFO;
  MSAN_MAYBE_INTERCEPT_MALLINFO2;
  MSAN_MAYBE_INTERCEPT_MALLOPT;
  MSAN_MAYBE_INTERCEPT_MALLOC_STATS;
  INTERCEPT_FUNCTION(fread);
  MSAN_MAYBE_INTERCEPT_FREAD_UNLOCKED;
  INTERCEPT_FUNCTION(memccpy);
  MSAN_MAYBE_INTERCEPT_MEMPCPY;
  INTERCEPT_FUNCTION(bcopy);
  INTERCEPT_FUNCTION(wmemset);
  INTERCEPT_FUNCTION(wmemcpy);
  MSAN_MAYBE_INTERCEPT_WMEMPCPY;
  INTERCEPT_FUNCTION(wmemmove);
  INTERCEPT_FUNCTION(strcpy);
  MSAN_MAYBE_INTERCEPT_STPCPY;
  MSAN_MAYBE_INTERCEPT_STPNCPY;
  INTERCEPT_FUNCTION(strdup);
  MSAN_MAYBE_INTERCEPT___STRDUP;
  INTERCEPT_FUNCTION(strncpy);
  MSAN_MAYBE_INTERCEPT_GCVT;
  INTERCEPT_FUNCTION(strcat);
  INTERCEPT_FUNCTION(strncat);
  INTERCEPT_STRTO(strtod);
  INTERCEPT_STRTO(strtof);
#ifdef SANITIZER_NLDBL_VERSION
  INTERCEPT_STRTO_VER(strtold, SANITIZER_NLDBL_VERSION);
#else
  INTERCEPT_STRTO(strtold);
#endif
  INTERCEPT_STRTO(strtol);
  INTERCEPT_STRTO(strtoul);
  INTERCEPT_STRTO(strtoll);
  INTERCEPT_STRTO(strtoull);
  INTERCEPT_STRTO(strtouq);
  INTERCEPT_STRTO(wcstod);
  INTERCEPT_STRTO(wcstof);
#ifdef SANITIZER_NLDBL_VERSION
  INTERCEPT_STRTO_VER(wcstold, SANITIZER_NLDBL_VERSION);
#else
  INTERCEPT_STRTO(wcstold);
#endif
  INTERCEPT_STRTO(wcstol);
  INTERCEPT_STRTO(wcstoul);
  INTERCEPT_STRTO(wcstoll);
  INTERCEPT_STRTO(wcstoull);
#if SANITIZER_GLIBC
  INTERCEPT_STRTO(__isoc23_strtod);
  INTERCEPT_STRTO(__isoc23_strtof);
  INTERCEPT_STRTO(__isoc23_strtold);
  INTERCEPT_STRTO(__isoc23_strtol);
  INTERCEPT_STRTO(__isoc23_strtoul);
  INTERCEPT_STRTO(__isoc23_strtoll);
  INTERCEPT_STRTO(__isoc23_strtoull);
  INTERCEPT_STRTO(__isoc23_strtouq);
  INTERCEPT_STRTO(__isoc23_wcstod);
  INTERCEPT_STRTO(__isoc23_wcstof);
  INTERCEPT_STRTO(__isoc23_wcstold);
  INTERCEPT_STRTO(__isoc23_wcstol);
  INTERCEPT_STRTO(__isoc23_wcstoul);
  INTERCEPT_STRTO(__isoc23_wcstoll);
  INTERCEPT_STRTO(__isoc23_wcstoull);
#endif

#ifdef SANITIZER_NLDBL_VERSION
  INTERCEPT_FUNCTION_VER(vswprintf, SANITIZER_NLDBL_VERSION);
  INTERCEPT_FUNCTION_VER(swprintf, SANITIZER_NLDBL_VERSION);
#else
  INTERCEPT_FUNCTION(vswprintf);
  INTERCEPT_FUNCTION(swprintf);
#endif
  INTERCEPT_FUNCTION(strftime);
  INTERCEPT_FUNCTION(strftime_l);
  MSAN_MAYBE_INTERCEPT___STRFTIME_L;
  INTERCEPT_FUNCTION(wcsftime);
  INTERCEPT_FUNCTION(wcsftime_l);
  MSAN_MAYBE_INTERCEPT___WCSFTIME_L;
  INTERCEPT_FUNCTION(mbtowc);
  INTERCEPT_FUNCTION(mbrtowc);
  INTERCEPT_FUNCTION(wcslen);
  INTERCEPT_FUNCTION(wcsnlen);
  INTERCEPT_FUNCTION(wcschr);
  INTERCEPT_FUNCTION(wcscpy);
  INTERCEPT_FUNCTION(wcsncpy);
  INTERCEPT_FUNCTION(wcscmp);
  INTERCEPT_FUNCTION(getenv);
  INTERCEPT_FUNCTION(setenv);
  INTERCEPT_FUNCTION(putenv);
  INTERCEPT_FUNCTION(gettimeofday);
  MSAN_MAYBE_INTERCEPT_FCVT;
  MSAN_MAYBE_INTERCEPT_FSTAT;
  MSAN_MAYBE_INTERCEPT_FSTAT64;
  MSAN_MAYBE_INTERCEPT___FXSTAT;
  MSAN_MAYBE_INTERCEPT_FSTATAT;
  MSAN_MAYBE_INTERCEPT_FSTATAT64;
  MSAN_MAYBE_INTERCEPT___FXSTATAT;
  MSAN_MAYBE_INTERCEPT___FXSTAT64;
  MSAN_MAYBE_INTERCEPT___FXSTATAT64;
  INTERCEPT_FUNCTION(pipe);
  INTERCEPT_FUNCTION(pipe2);
  INTERCEPT_FUNCTION(socketpair);
  MSAN_MAYBE_INTERCEPT_FGETS_UNLOCKED;
  INTERCEPT_FUNCTION(getrlimit);
  MSAN_MAYBE_INTERCEPT___GETRLIMIT;
  MSAN_MAYBE_INTERCEPT_GETRLIMIT64;
  MSAN_MAYBE_INTERCEPT_PRLIMIT;
  MSAN_MAYBE_INTERCEPT_PRLIMIT64;
  INTERCEPT_FUNCTION(gethostname);
  MSAN_MAYBE_INTERCEPT_EPOLL_WAIT;
  MSAN_MAYBE_INTERCEPT_EPOLL_PWAIT;
  INTERCEPT_FUNCTION(strsignal);
  INTERCEPT_FUNCTION(dladdr);
  MSAN_MAYBE_INTERCEPT_DLADDR1;
  INTERCEPT_FUNCTION(dlerror);
  INTERCEPT_FUNCTION(dl_iterate_phdr);
  INTERCEPT_FUNCTION(getrusage);
#if defined(__mips__)
  INTERCEPT_FUNCTION_VER(pthread_create, "GLIBC_2.2");
#else
  INTERCEPT_FUNCTION(pthread_create);
#endif
  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(pthread_key_create);
#if SANITIZER_GLIBC
  INTERCEPT_FUNCTION(pthread_tryjoin_np);
  INTERCEPT_FUNCTION(pthread_timedjoin_np);
#endif

#if SANITIZER_NETBSD
  INTERCEPT_FUNCTION(__libc_thr_keycreate);
#endif

  INTERCEPT_FUNCTION(pthread_join);
  INTERCEPT_FUNCTION(tzset);
  INTERCEPT_FUNCTION(atexit);
  INTERCEPT_FUNCTION(__cxa_atexit);
  INTERCEPT_FUNCTION(shmat);
  MSAN_MAYBE_INTERCEPT_OPENPTY;
  MSAN_MAYBE_INTERCEPT_FORKPTY;

  inited = 1;
}
} // namespace __msan
