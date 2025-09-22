// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__need_malloc_and_calloc)

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

#  include_next <stdlib.h>

#elif !defined(_LIBCPP_STDLIB_H)
#  define _LIBCPP_STDLIB_H

/*
    stdlib.h synopsis

Macros:

    EXIT_FAILURE
    EXIT_SUCCESS
    MB_CUR_MAX
    NULL
    RAND_MAX

Types:

    size_t
    div_t
    ldiv_t
    lldiv_t                                                               // C99

double    atof (const char* nptr);
int       atoi (const char* nptr);
long      atol (const char* nptr);
long long atoll(const char* nptr);                                        // C99
double             strtod  (const char* restrict nptr, char** restrict endptr);
float              strtof  (const char* restrict nptr, char** restrict endptr); // C99
long double        strtold (const char* restrict nptr, char** restrict endptr); // C99
long               strtol  (const char* restrict nptr, char** restrict endptr, int base);
long long          strtoll (const char* restrict nptr, char** restrict endptr, int base); // C99
unsigned long      strtoul (const char* restrict nptr, char** restrict endptr, int base);
unsigned long long strtoull(const char* restrict nptr, char** restrict endptr, int base); // C99
int rand(void);
void srand(unsigned int seed);
void* calloc(size_t nmemb, size_t size);
void free(void* ptr);
void* malloc(size_t size);
void* realloc(void* ptr, size_t size);
void abort(void);
int atexit(void (*func)(void));
void exit(int status);
void _Exit(int status);
char* getenv(const char* name);
int system(const char* string);
void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void qsort(void* base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
int         abs(      int j);
long        abs(     long j);
long long   abs(long long j);                                             // C++0X
long       labs(     long j);
long long llabs(long long j);                                             // C99
div_t     div(      int numer,       int denom);
ldiv_t    div(     long numer,      long denom);
lldiv_t   div(long long numer, long long denom);                          // C++0X
ldiv_t   ldiv(     long numer,      long denom);
lldiv_t lldiv(long long numer, long long denom);                          // C99
int mblen(const char* s, size_t n);
int mbtowc(wchar_t* restrict pwc, const char* restrict s, size_t n);
int wctomb(char* s, wchar_t wchar);
size_t mbstowcs(wchar_t* restrict pwcs, const char* restrict s, size_t n);
size_t wcstombs(char* restrict s, const wchar_t* restrict pwcs, size_t n);
int at_quick_exit(void (*func)(void))                                     // C++11
void quick_exit(int status);                                              // C++11
void *aligned_alloc(size_t alignment, size_t size);                       // C11

*/

#  include <__config>

#  if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#    pragma GCC system_header
#  endif

#  if __has_include_next(<stdlib.h>)
#    include_next <stdlib.h>
#  endif

#  ifdef __cplusplus
extern "C++" {
// abs

#    ifdef abs
#      undef abs
#    endif
#    ifdef labs
#      undef labs
#    endif
#    ifdef llabs
#      undef llabs
#    endif

// MSVCRT already has the correct prototype in <stdlib.h> if __cplusplus is defined
#    if !defined(_LIBCPP_MSVCRT)
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long abs(long __x) _NOEXCEPT { return __builtin_labs(__x); }
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long long abs(long long __x) _NOEXCEPT { return __builtin_llabs(__x); }
#    endif // !defined(_LIBCPP_MSVCRT)

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI float abs(float __lcpp_x) _NOEXCEPT {
  return __builtin_fabsf(__lcpp_x); // Use builtins to prevent needing math.h
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI double abs(double __lcpp_x) _NOEXCEPT {
  return __builtin_fabs(__lcpp_x);
}

_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI long double abs(long double __lcpp_x) _NOEXCEPT {
  return __builtin_fabsl(__lcpp_x);
}

// div

#    ifdef div
#      undef div
#    endif
#    ifdef ldiv
#      undef ldiv
#    endif
#    ifdef lldiv
#      undef lldiv
#    endif

// MSVCRT already has the correct prototype in <stdlib.h> if __cplusplus is defined
#    if !defined(_LIBCPP_MSVCRT)
inline _LIBCPP_HIDE_FROM_ABI ldiv_t div(long __x, long __y) _NOEXCEPT { return ::ldiv(__x, __y); }
#      if !(defined(__FreeBSD__) && !defined(__LONG_LONG_SUPPORTED))
inline _LIBCPP_HIDE_FROM_ABI lldiv_t div(long long __x, long long __y) _NOEXCEPT { return ::lldiv(__x, __y); }
#      endif
#    endif // _LIBCPP_MSVCRT
} // extern "C++"
#  endif   // __cplusplus

#endif // _LIBCPP_STDLIB_H
